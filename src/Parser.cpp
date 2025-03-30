#define KALEIDOSCOPE_DEFINE
#include "Parser.hpp"
using namespace std;

namespace ast {

NumberExpr::NumberExpr(double value) : value(value) {}

llvm::Value *NumberExpr::codegen(CodegenContext &ctx) {
  return llvm::ConstantFP::get(*ctx.ctx, llvm::APFloat(value));
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
