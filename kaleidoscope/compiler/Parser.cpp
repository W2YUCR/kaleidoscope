#include "Parser.hpp"
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
