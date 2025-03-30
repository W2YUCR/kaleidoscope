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
  virtual ~Expr();
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
  VariableExpr(std::string name);
  llvm::Value *codegen(CodegenContext &ctx) override;
};

class BinaryExpr : public Expr {
  std::string op;
  std::unique_ptr<Expr> lhs, rhs;

public:
  BinaryExpr(std::string op, std::unique_ptr<Expr> lhs,
             std::unique_ptr<Expr> rhs);

  llvm::Value *codegen(CodegenContext &ctx) override;
};

class CallExpr : public Expr {
  std::string callee;
  std::vector<std::unique_ptr<Expr>> arguments;

public:
  CallExpr(std::string callee, std::vector<std::unique_ptr<Expr>> arguments);

  llvm::Value *codegen(CodegenContext &ctx) override;
};

class Prototype : public Expr {
  std::string name;
  std::vector<std::string> arguments;

public:
  Prototype(std::string name, std::vector<std::string> arguments);

  const std::string &get_name() const;
  llvm::Function *codegen(CodegenContext &ctx) override;
};

class Function : public Expr {
  std::shared_ptr<Prototype> prototype;
  std::unique_ptr<Expr> body;

public:
  Function(std::unique_ptr<Prototype> prototype, std::unique_ptr<Expr> body);

  std::string const &get_name() const;
  llvm::Value *codegen(CodegenContext &ctx) override;
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

#endif
