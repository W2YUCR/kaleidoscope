#ifndef PARSER_HPP
#define PARSER_HPP

#include "Token.hpp"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Verifier.h"
#include <map>
#include <memory>
#include <vector>

#ifdef KALEIDOSCOPE_DEFINE
#define KALEIDOSCOPE_BODY(x) x
#else
#define KALEIDOSCOPE_BODY(x) ;
#endif

struct CodegenException : std::exception {
  char const *what() const noexcept override { return "codegen error"; }
};

namespace ast {
class Prototype;
}

struct CodegenContext {
  std::unique_ptr<llvm::LLVMContext> ctx;
  std::unique_ptr<llvm::Module> module;
  std::unique_ptr<llvm::IRBuilder<>> builder;
  std::map<std::string, llvm::Value *> named_values;
  std::map<std::string, std::shared_ptr<ast::Prototype>> *prototypes;

  CodegenContext(
      llvm::orc::LLJIT *jit,
      std::map<std::string, std::shared_ptr<ast::Prototype>> *prototypes)
      : prototypes(prototypes) {
    ctx = std::make_unique<llvm::LLVMContext>();
    module = std::make_unique<llvm::Module>("my jit", *ctx);
    builder = std::make_unique<llvm::IRBuilder<>>(*ctx);
    module->setDataLayout(jit->getDataLayout());
    // todo: add the optimization passes
  }

  llvm::Function *get_proto(std::string name);
};

namespace ast {
class Expr {
public:
  virtual ~Expr() = default;
  virtual llvm::Value *codegen(CodegenContext &ctx) = 0;
};

class NumberExpr : public Expr {
  double value;

public:
  NumberExpr(double value);
  llvm::Value *codegen(CodegenContext &ctx) override;
};

class VariableExpr : public Expr {
  std::string name;

public:
  VariableExpr(std::string name) : name(name) {}
  llvm::Value *codegen(CodegenContext &ctx) override {
    auto e = ctx.named_values.find(name);
    if (e == ctx.named_values.end())
      throw CodegenException{};
    return e->second;
  }
};

class BinaryExpr : public Expr {
  std::string op;
  std::unique_ptr<Expr> lhs, rhs;

public:
  BinaryExpr(std::string op, std::unique_ptr<Expr> lhs,
             std::unique_ptr<Expr> rhs)
      : op(op), lhs(std::move(lhs)), rhs(std::move(rhs)) {}
  llvm::Value *codegen(CodegenContext &ctx) override {
    auto lh = lhs->codegen(ctx);
    auto rh = rhs->codegen(ctx);

    static auto const cg =
        std::map<std::string,
                 std::function<llvm::Value *(CodegenContext & ctx,
                                             llvm::Value *, llvm::Value *)>>{
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
};

class CallExpr : public Expr {
  std::string callee;
  std::vector<std::unique_ptr<Expr>> arguments;

public:
  CallExpr(std::string callee, std::vector<std::unique_ptr<Expr>> arguments)
      : callee(std::move(callee)), arguments(std::move(arguments)) {}

  llvm::Value *codegen(CodegenContext &ctx) override {
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
};

class Prototype : public Expr {
  std::string name;
  std::vector<std::string> arguments;

public:
  Prototype(std::string name, std::vector<std::string> arguments)
      : name(std::move(name)), arguments(std::move(arguments)) {}

  const std::string &get_name() const { return name; }
  llvm::Function *codegen(CodegenContext &ctx) override {
    std::vector<llvm::Type *> parameters(arguments.size(),
                                         llvm::Type::getDoubleTy(*ctx.ctx));

    auto func_type = llvm::FunctionType::get(llvm::Type::getDoubleTy(*ctx.ctx),
                                             parameters, false);

    auto func = llvm::Function::Create(
        func_type, llvm::Function::ExternalLinkage, name, ctx.module.get());

    auto arg = arguments.begin();
    for (auto &func_arg : func->args())
      func_arg.setName(*arg++);

    return func;
  }
};

class Function : public Expr {
  std::shared_ptr<Prototype> prototype;
  std::unique_ptr<Expr> body;

public:
  Function(std::unique_ptr<Prototype> prototype, std::unique_ptr<Expr> body)
      : prototype(std::move(prototype)), body(std::move(body)) {}

  std::string const &get_name() const { return prototype->get_name(); }

  llvm::Value *codegen(CodegenContext &ctx) override {
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
};

} // namespace ast

class Parser {
public:
  struct ParseError : std::exception {
    virtual char const *what() const noexcept;
  };

private:
  std::map<std::string, int> binary_precedence;

  Token current;
  bool peeking;
  std::istream *stream;

public:
  Token &peek();
  Token &next();

  // <NumberExpr> ::= <number>
  std::unique_ptr<ast::NumberExpr> parse_number();
  std::unique_ptr<ast::Expr> parse_parenthesized();

  // either a variable or a call
  std ::unique_ptr<ast::Expr> parse_identifier();
  std ::unique_ptr<ast::Expr> parse_primary();
  int get_token_precedence(std::string const &token);
  std::unique_ptr<ast::Expr> parse_expression();
  std::unique_ptr<ast::Expr> parse_binary_rhs(int lhs_prec,
                                              std::unique_ptr<ast::Expr> lhs);
  std::unique_ptr<ast::Prototype> parse_prototype();
  std::unique_ptr<ast::Function> parse_definition();
  std::unique_ptr<ast::Prototype> parse_extern();
  std::unique_ptr<ast::Function> parse_top_level();

public:
  Parser();

  std::unique_ptr<ast::Expr> parse(std::istream &s);
};

#undef KALEIDOSCOPE_BODY

#endif
