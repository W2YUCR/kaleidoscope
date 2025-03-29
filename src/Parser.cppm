module;
#include <stdexcept>

#include <map>
#include <memory>
#include <vector>

#include "KaleidoscopeJIT.hpp"
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/Error.h>

export module Parser;

import Token;
using namespace llvm;

export struct CodegenException : std::exception {
  char const *what() const noexcept override { return "codegen error"; }
};

namespace ast {
export class Prototype;
}

export struct CodegenContext {
  std::unique_ptr<LLVMContext> ctx;
  std::unique_ptr<Module> module;
  std::unique_ptr<IRBuilder<>> builder;
  std::map<std::string, Value *> named_values;
  std::map<std::string, std::shared_ptr<ast::Prototype>> *prototypes;

  CodegenContext(
      orc::KaleidoscopeJIT *jit,
      std::map<std::string, std::shared_ptr<ast::Prototype>> *prototypes)
      : prototypes(prototypes) {
    ctx = std::make_unique<LLVMContext>();
    module = std::make_unique<Module>("my jit", *ctx);
    builder = std::make_unique<IRBuilder<>>(*ctx);
    module->setDataLayout(jit->getDataLayout());
    // todo: add the optimization passes
  }

  llvm::Function *get_proto(std::string name);
};

namespace ast {
export class Expr {
public:
  virtual ~Expr() = default;
  virtual Value *codegen(CodegenContext &ctx) = 0;
};

export class NumberExpr : public Expr {
  double value;

public:
  NumberExpr(double value) : value(value) {}
  Value *codegen(CodegenContext &ctx) override {
    return ConstantFP::get(*ctx.ctx, APFloat(value));
  }
};

export class VariableExpr : public Expr {
  std::string name;

public:
  VariableExpr(std::string name) : name(name) {}
  Value *codegen(CodegenContext &ctx) override {
    auto e = ctx.named_values.find(name);
    if (e == ctx.named_values.end())
      throw CodegenException{};
    return e->second;
  }
};

export class BinaryExpr : public Expr {
  std::string op;
  std::unique_ptr<Expr> lhs, rhs;

public:
  BinaryExpr(std::string op, std::unique_ptr<Expr> lhs,
             std::unique_ptr<Expr> rhs)
      : op(op), lhs(std::move(lhs)), rhs(std::move(rhs)) {}
  Value *codegen(CodegenContext &ctx) override {
    auto lh = lhs->codegen(ctx);
    auto rh = rhs->codegen(ctx);

    static auto const cg =
        std::map<std::string, std::function<Value *(CodegenContext & ctx,
                                                    Value *, Value *)>>{
            {"+",
             [](CodegenContext &ctx, Value *lh, Value *rh) {
               return ctx.builder->CreateFAdd(lh, rh, "addtmp");
             }},
            {"-",
             [](CodegenContext &ctx, Value *lh, Value *rh) {
               return ctx.builder->CreateFSub(lh, rh, "addtmp");
             }},
            {"*",
             [](CodegenContext &ctx, Value *lh, Value *rh) {
               return ctx.builder->CreateFMul(lh, rh, "addtmp");
             }},
            {"<",
             [](CodegenContext &ctx, Value *lh, Value *rh) {
               lh = ctx.builder->CreateFCmpULT(lh, rh, "cmptmp");
               // Convert bool 0/1 to double 0.0 or 1.0
               return ctx.builder->CreateUIToFP(lh, Type::getDoubleTy(*ctx.ctx),
                                                "booltmp");
             }},
        };

    if (auto e = cg.find(op); e != cg.end()) {
      return e->second(ctx, lh, rh);
    }
    throw CodegenException{};
  }
};

export class CallExpr : public Expr {
  std::string callee;
  std::vector<std::unique_ptr<Expr>> arguments;

public:
  CallExpr(std::string callee, std::vector<std::unique_ptr<Expr>> arguments)
      : callee(std::move(callee)), arguments(std::move(arguments)) {}

  Value *codegen(CodegenContext &ctx) override {
    // Look up the name in the global module table.
    auto fn = ctx.get_proto(callee);
    if (!fn)
      throw CodegenException{};

    // If argument mismatch error.
    if (fn->arg_size() != arguments.size())
      throw CodegenException{};

    std::vector<Value *> args;
    for (auto &arg : arguments) {
      args.push_back(arg->codegen(ctx));
    }

    return ctx.builder->CreateCall(fn, args, "calltmp");
  }
};

export class Prototype : public Expr {
  std::string name;
  std::vector<std::string> arguments;

public:
  Prototype(std::string name, std::vector<std::string> arguments)
      : name(std::move(name)), arguments(std::move(arguments)) {}

  const std::string &get_name() const { return name; }
  Function *codegen(CodegenContext &ctx) override {
    std::vector<Type *> parameters(arguments.size(),
                                   Type::getDoubleTy(*ctx.ctx));

    auto func_type =
        FunctionType::get(Type::getDoubleTy(*ctx.ctx), parameters, false);

    auto func = Function::Create(func_type, Function::ExternalLinkage, name,
                                 ctx.module.get());

    auto arg = arguments.begin();
    for (auto &func_arg : func->args())
      func_arg.setName(*arg++);

    return func;
  }
};

export class Function : public Expr {
  std::shared_ptr<Prototype> prototype;
  std::unique_ptr<Expr> body;

public:
  Function(std::unique_ptr<Prototype> prototype, std::unique_ptr<Expr> body)
      : prototype(std::move(prototype)), body(std::move(body)) {}

  std::string const &get_name() const { return prototype->get_name(); }

  Value *codegen(CodegenContext &ctx) override {
    (*ctx.prototypes)[prototype->get_name()] = prototype;
    auto func = ctx.get_proto(prototype->get_name());
    if (!func)
      func = prototype->codegen(ctx);

    auto *block = BasicBlock::Create(*ctx.ctx, "entry", func);
    ctx.builder->SetInsertPoint(block);

    // Record the function arguments in the NamedValues map.
    ctx.named_values.clear();
    for (auto &arg : func->args())
      ctx.named_values[std::string(arg.getName())] = &arg;

    Value *ret;
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
};

} // namespace ast

export class Parser {
public:
  struct ParseError : std::exception {
    virtual char const *what() const noexcept { return "parser error"; }
  };

private:
  std::map<std::string, int> binary_precedence;

  Token current;
  bool peeking;
  std::istream *stream;

public:
  Token &peek() {
    if (peeking)
      return current;
    peeking = true;
    *stream >> current;
    return current;
  }

  Token &next() {
    peek();
    peeking = false;
    return current;
  }

  // <NumberExpr> ::= <number>
  std::unique_ptr<ast::NumberExpr> parse_number() {
    return std::make_unique<ast::NumberExpr>(next().number);
  }

  std::unique_ptr<ast::Expr> parse_parenthesized() {
    next();

    auto expr = parse_expression();
    if (!expr)
      throw ParseError{};

    if (next().type != Token::TypeRpar)
      throw ParseError{};

    return expr;
  }

  // either a variable or a call
  std ::unique_ptr<ast::Expr> parse_identifier() {
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

  std ::unique_ptr<ast::Expr> parse_primary() {
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

  int get_token_precedence(std::string const &token) {
    if (auto e = binary_precedence.find(token); e != binary_precedence.end()) {
      return e->second;
    }
    return -1;
  }

  std::unique_ptr<ast::Expr> parse_expression() {
    auto lhs = parse_primary();

    return parse_binary_rhs(0, std::move(lhs));
  }

  std::unique_ptr<ast::Expr> parse_binary_rhs(int lhs_prec,
                                              std::unique_ptr<ast::Expr> lhs) {
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

      lhs =
          std::make_unique<ast::BinaryExpr>(op, std::move(lhs), std::move(rhs));
    }
  }

  std::unique_ptr<ast::Prototype> parse_prototype() {
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

  std::unique_ptr<ast::Function> parse_definition() {
    next();
    return std::make_unique<ast::Function>(parse_prototype(),
                                           parse_expression());
  }

  std::unique_ptr<ast::Prototype> parse_extern() {
    next();
    return parse_prototype();
  }

  std::unique_ptr<ast::Function> parse_top_level() {
    return std::make_unique<ast::Function>(
        std::make_unique<ast::Prototype>("__anon_expr",
                                         std::vector<std::string>{}),
        parse_expression());
  }

public:
  Parser()
      : binary_precedence{{"<", 10}, {"+", 20}, {"-", 20}, {"*", 40}},
        peeking(false) {}

  std::unique_ptr<ast::Expr> parse(std::istream &s) {
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
};

llvm::Function *CodegenContext::get_proto(std::string name) {
  if (auto *func = module->getFunction(name))
    return func;

  if (auto e = prototypes->find(name); e != prototypes->end())
    return e->second.get()->codegen(*this);

  return nullptr;
}
