#include "Parser.hpp"
#include "kaleidoscope/compiler/Token.hpp"
#include <llvm/ADT/APFloat.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <memory>
using namespace std;

namespace ast {

Expr::~Expr() = default;

NumberExpr::NumberExpr(double value) : value(value) {}

llvm::Value *NumberExpr::codegen(CodegenContext &ctx) {
  return llvm::ConstantFP::get(*ctx.ctx, llvm::APFloat(value));
}

VariableExpr::VariableExpr(std::string name) : name(name) {}

BinaryExpr::BinaryExpr(std::string op, std::unique_ptr<Expr> lhs,
                       std::unique_ptr<Expr> rhs)
    : op(op), lhs(std::move(lhs)), rhs(std::move(rhs)) {}

llvm::Value *VariableExpr::codegen(CodegenContext &ctx) {
  auto e = ctx.named_values.find(name);
  if (e == ctx.named_values.end())
    throw CodegenException{};
  return e->second;
}

llvm::Value *BinaryExpr::codegen(CodegenContext &ctx) {
  auto lh = lhs->codegen(ctx);
  auto rh = rhs->codegen(ctx);

  static auto const cg =
      std::map<std::string,
               std::function<llvm::Value *(CodegenContext & ctx, llvm::Value *,
                                           llvm::Value *)>>{
          {"+",
           [](CodegenContext &ctx, llvm::Value *lh, llvm::Value *rh) {
             return ctx.builder->CreateFAdd(lh, rh, "addtmp");
           }},
          {"-",
           [](CodegenContext &ctx, llvm::Value *lh, llvm::Value *rh) {
             return ctx.builder->CreateFSub(lh, rh, "addtmp");
           }},
          {"*",
           [](CodegenContext &ctx, llvm::Value *lh, llvm::Value *rh) {
             return ctx.builder->CreateFMul(lh, rh, "addtmp");
           }},
          {"<",
           [](CodegenContext &ctx, llvm::Value *lh, llvm::Value *rh) {
             lh = ctx.builder->CreateFCmpULT(lh, rh, "cmptmp");
             // Convert bool 0/1 to double 0.0 or 1.0
             return ctx.builder->CreateUIToFP(
                 lh, llvm::Type::getDoubleTy(*ctx.ctx), "booltmp");
           }},
      };

  if (auto e = cg.find(op); e != cg.end()) {
    return e->second(ctx, lh, rh);
  }
  throw CodegenException{};
}

CallExpr::CallExpr(std::string callee,
                   std::vector<std::unique_ptr<Expr>> arguments)
    : callee(std::move(callee)), arguments(std::move(arguments)) {}

llvm::Value *CallExpr::codegen(CodegenContext &ctx) {
  // Look up the name in the global module table.
  auto fn = ctx.get_proto(callee);
  if (!fn)
    throw CodegenException{};

  // If argument mismatch error.
  if (fn->arg_size() != arguments.size())
    throw CodegenException{};

  std::vector<llvm::Value *> args;
  for (auto &arg : arguments) {
    args.push_back(arg->codegen(ctx));
  }

  return ctx.builder->CreateCall(fn, args, "calltmp");
}

Prototype::Prototype(std::string name, std::vector<std::string> arguments)
    : name(std::move(name)), arguments(std::move(arguments)) {}

const std::string &Prototype::get_name() const { return name; }

llvm::Function *Prototype::codegen(CodegenContext &ctx) {
  std::vector<llvm::Type *> parameters(arguments.size(),
                                       llvm::Type::getDoubleTy(*ctx.ctx));

  auto func_type = llvm::FunctionType::get(llvm::Type::getDoubleTy(*ctx.ctx),
                                           parameters, false);

  auto func = llvm::Function::Create(func_type, llvm::Function::ExternalLinkage,
                                     name, ctx.module.get());

  auto arg = arguments.begin();
  for (auto &func_arg : func->args())
    func_arg.setName(*arg++);

  return func;
}

Function::Function(std::unique_ptr<Prototype> prototype,
                   std::unique_ptr<Expr> body)
    : prototype(std::move(prototype)), body(std::move(body)) {}

std::string const &Function::get_name() const { return prototype->get_name(); }

llvm::Value *Function::codegen(CodegenContext &ctx) {
  (*ctx.prototypes)[prototype->get_name()] = prototype;
  auto func = ctx.get_proto(prototype->get_name());
  if (!func)
    func = prototype->codegen(ctx);

  auto *block = llvm::BasicBlock::Create(*ctx.ctx, "entry", func);
  ctx.builder->SetInsertPoint(block);

  // Record the function arguments in the NamedValues map.
  ctx.named_values.clear();
  for (auto &arg : func->args())
    ctx.named_values[std::string(arg.getName())] = &arg;

  llvm::Value *ret;
  try {
    ret = body->codegen(ctx);
  } catch (CodegenException c) {
    func->eraseFromParent();
    throw c;
  }

  ctx.builder->CreateRet(ret);

  verifyFunction(*func);

  return func;
}

If::If(std::unique_ptr<Expr> condition, std::unique_ptr<Expr> then_expr,
       std::unique_ptr<Expr> else_expr)
    : condition(std::move(condition)), then_expr(std::move(then_expr)),
      else_expr(std::move(else_expr)) {};

llvm::Value *If::codegen(CodegenContext &ctx) {
  // the function containing the if statement
  auto function = ctx.builder->GetInsertBlock()->getParent();

  auto then_block = llvm::BasicBlock::Create(*ctx.ctx, "then");
  auto else_block = llvm::BasicBlock::Create(*ctx.ctx, "else");

  // the block both then and else will go to after completing
  auto merge_block = llvm::BasicBlock::Create(*ctx.ctx, "ifcont");

  // generate code to choose whick block to go to
  auto cond = condition->codegen(ctx);
  auto cond_bool = ctx.builder->CreateFCmpONE(
      cond, llvm::ConstantFP::get(*ctx.ctx, llvm::APFloat(0.0)), "ifcond");
  ctx.builder->CreateCondBr(cond_bool, then_block, else_block);

  // generate then block
  function->insert(function->end(), then_block);
  ctx.builder->SetInsertPoint(then_block);
  auto then_value = then_expr->codegen(ctx);
  assert(then_value);
  ctx.builder->CreateBr(merge_block);
  auto then_end = ctx.builder->GetInsertBlock();

  // generate else block
  function->insert(function->end(), else_block);
  ctx.builder->SetInsertPoint(else_block);
  auto else_value = else_expr->codegen(ctx);
  assert(else_value);
  ctx.builder->CreateBr(merge_block);
  auto else_end = ctx.builder->GetInsertBlock();

  // generate merge block
  function->insert(function->end(), merge_block);
  ctx.builder->SetInsertPoint(merge_block);
  auto phi =
      ctx.builder->CreatePHI(llvm::Type::getDoubleTy(*ctx.ctx), 2, "iftmp");
  phi->addIncoming(then_value, then_end);
  phi->addIncoming(else_value, else_end);
  return phi;
}

For::For(const std::string &loop_var_name, std::unique_ptr<Expr> start,
         std::unique_ptr<Expr> end, std::unique_ptr<Expr> step,
         std::unique_ptr<Expr> body)
    : loop_var_name(loop_var_name), start(std::move(start)),
      end(::std::move(end)), step(std::move(step)), body(std::move(body)) {}

llvm::Value *For::codegen(CodegenContext &ctx) {
  auto function = ctx.builder->GetInsertBlock()->getParent();

  auto loop = llvm::BasicBlock::Create(*ctx.ctx, "loop");
  auto after_loop = llvm::BasicBlock::Create(*ctx.ctx, "afterloop");

  // generate the entry into the loop
  auto start_val = start->codegen(ctx);
  auto entry = ctx.builder->GetInsertBlock();
  assert(start_val);

  ctx.builder->CreateBr(loop);

  // Loop body
  function->insert(function->end(), loop);
  ctx.builder->SetInsertPoint(loop);
  auto loop_var = ctx.builder->CreatePHI(llvm::Type::getDoubleTy(*ctx.ctx), 2);
  loop_var->addIncoming(start_val, entry);

  // shadow outer variable with the loop variable's name
  llvm::Value *shadow = loop_var;
  std::swap(shadow, ctx.named_values[loop_var_name]);

  body->codegen(ctx);

  // compute the next loop variable value
  auto *step_value = step ? step->codegen(ctx)
                          : llvm::ConstantFP::get(*ctx.ctx, llvm::APFloat(1.0));
  assert(step_value);

  auto next_var = ctx.builder->CreateFAdd(loop_var, step_value, "nextvalue");
  auto end_var = end->codegen(ctx);
  auto end_cond = ctx.builder->CreateFCmpONE(
      end_var, llvm::ConstantFP::get(*ctx.ctx, llvm::APFloat(0.0)), "loopcond");
  ctx.builder->CreateCondBr(end_cond, loop, after_loop);

  // give the next value to the next iteration
  auto loop_end = ctx.builder->GetInsertBlock();
  loop_var->addIncoming(next_var, loop_end);

  // generate exit of the loop
  function->insert(function->end(), after_loop);
  ctx.builder->SetInsertPoint(after_loop);

  // unshadow outer variable
  if (shadow) {
    ctx.named_values[loop_var_name] = shadow;
  } else {
    ctx.named_values.erase(loop_var_name);
  }

  // value of a loop is 0.0
  return llvm::Constant::getNullValue(llvm::Type::getDoubleTy(*ctx.ctx));
}

} // namespace ast

Token &Parser::peek() {
  if (peeking)
    return current;
  peeking = true;
  *stream >> current;
  return current;
}

Token &Parser::next() {
  peek();
  peeking = false;
  return current;
}

// <NumberExpr> ::= <number>
std::unique_ptr<ast::NumberExpr> Parser::parse_number() {
  return std::make_unique<ast::NumberExpr>(next().number);
}

std::unique_ptr<ast::Expr> Parser::parse_parenthesized() {
  next();

  auto expr = parse_expression();
  if (!expr)
    throw ParseError{};

  if (next().type != Token::TypeRpar)
    throw ParseError{};

  return expr;
}

// either a variable or a call
std ::unique_ptr<ast::Expr> Parser::parse_identifier() {
  std::string identifier = next().literal;

  if (peek().type != Token::TypeLpar)
    return std::make_unique<ast::VariableExpr>(identifier);
  next();

  std::vector<std::unique_ptr<ast::Expr>> args;
  if (peek().type != Token::TypeRpar) {
    for (;;) {
      if (auto expr = parse_expression()) {
        args.push_back(std::move(expr));
      } else {
        throw ParseError{};
      }

      if (peek().type == Token::TypeRpar)
        break;

      if (peek().type != Token::TypeComma)
        throw ParseError{};
      next();
    }
  }
  next();

  return std::make_unique<ast::CallExpr>(std::move(identifier),
                                         std::move(args));
}

std ::unique_ptr<ast::If> Parser::parse_if() {
  assert(peek().type = Token::TypeIf);
  next();

  auto cond = parse_expression();
  assert(cond);

  if (peek().type != Token::TypeThen)
    throw ParseError{};
  next();

  auto then = parse_expression();
  assert(then);

  if (peek().type != Token::TypeElse)
    throw ParseError{};
  next();

  auto else_expr = parse_expression();
  assert(else_expr);

  return std::make_unique<ast::If>(std::move(cond), std::move(then),
                                   std::move(else_expr));
}

std::unique_ptr<ast::For> Parser::parse_for() {
  assert(peek().type == Token::TypeFor);
  next();

  if (peek().type != Token::TypeIdentifier)
    throw ParseError{};

  auto loop_var = next().literal;

  if (!(peek().type == Token::TypeOperator && peek().literal == "="))
    throw ParseError{};
  next();

  auto start = parse_expression();

  if (peek().type != Token::TypeComma)
    throw ParseError{};
  next();

  auto end = parse_expression();
  assert(end);

  std::unique_ptr<ast::Expr> step;
  if (peek().type == Token::TypeComma) {
    next();

    step = parse_expression();
    assert(step);
  }

  if (peek().type != Token::TypeIn)
    throw ParseError{};
  next();

  auto body = parse_expression();
  assert(body);

  return std::make_unique<ast::For>(loop_var, std::move(start), std::move(end),
                                    std::move(step), std::move(body));
}

std ::unique_ptr<ast::Expr> Parser::parse_primary() {
  switch (peek().type) {
  default:
    throw ParseError{};
  case Token::TypeIdentifier:
    return parse_identifier();
  case Token::TypeNumber:
    return parse_number();
  case Token::TypeLpar:
    return parse_parenthesized();
  case Token::TypeIf:
    return parse_if();
  case Token::TypeFor:
    return parse_for();
  }
}

int Parser::get_token_precedence(std::string const &token) {
  if (auto e = binary_precedence.find(token); e != binary_precedence.end()) {
    return e->second;
  }
  return -1;
}

std::unique_ptr<ast::Expr> Parser::parse_expression() {
  auto lhs = parse_primary();

  return parse_binary_rhs(0, std::move(lhs));
}

std::unique_ptr<ast::Expr>
Parser::parse_binary_rhs(int lhs_prec, std::unique_ptr<ast::Expr> lhs) {
  for (;;) {
    int op_prec = get_token_precedence(peek().literal);
    if (op_prec < lhs_prec)
      return lhs;

    auto op = next().literal;

    auto rhs = parse_primary();

    int next_prec = get_token_precedence(peek().literal);
    if (op_prec < next_prec) {
      rhs = parse_binary_rhs(op_prec + 1, std::move(rhs));
    }

    lhs = std::make_unique<ast::BinaryExpr>(op, std::move(lhs), std::move(rhs));
  }
}

std::unique_ptr<ast::Prototype> Parser::parse_prototype() {
  if (peek().type != Token::TypeIdentifier)
    throw ParseError{};

  std::string name = next().literal;

  if (peek().type != Token::TypeLpar)
    throw ParseError{};
  next();

  std::vector<std::string> args;
  while (peek().type == Token::TypeIdentifier)
    args.push_back(next().literal);

  if (peek().type != Token::TypeRpar)
    throw ParseError{};
  next();

  return std::make_unique<ast::Prototype>(std::move(name), std::move(args));
}

std::unique_ptr<ast::Function> Parser::parse_definition() {
  next();
  return std::make_unique<ast::Function>(parse_prototype(), parse_expression());
}

std::unique_ptr<ast::Prototype> Parser::parse_extern() {
  next();
  return parse_prototype();
}

std::unique_ptr<ast::Function> Parser::parse_top_level() {
  return std::make_unique<ast::Function>(
      std::make_unique<ast::Prototype>("__anon_expr",
                                       std::vector<std::string>{}),
      parse_expression());
}

char const *Parser::ParseError::what() const noexcept { return "parser error"; }

Parser::Parser()
    : binary_precedence{{"<", 10}, {"+", 20}, {"-", 20}, {"*", 40}},
      peeking(false) {}

std::unique_ptr<ast::Expr> Parser::parse(std::istream &s) {
  stream = &s;
  while (peek().type == Token::TypeSemicolon) {
    next();
  }

  switch (peek().type) {
  case Token::TypeDef:
    return parse_definition();
  case Token::TypeExtern:
    return parse_extern();
  default:
    return parse_top_level();
  }
}

llvm::Function *CodegenContext::get_proto(std::string name) {
  if (auto *func = module->getFunction(name))
    return func;

  if (auto e = prototypes->find(name); e != prototypes->end())
    return e->second.get()->codegen(*this);

  return nullptr;
}
