module;
#include <map>
#include <memory>
#include <vector>

export module Parser;

import Token;

namespace ast {
export class Expr {
public:
  virtual ~Expr() = default;
};

export class NumberExpr : public Expr {
  double value;

public:
  NumberExpr(double value) : value(value) {}
};

export class VariableExpr : public Expr {
  std::string name;

public:
  VariableExpr(std::string name) : name(name) {}
};

export class BinaryExpr : public Expr {
  std::string op;
  std::unique_ptr<Expr> lhs, rhs;

public:
  BinaryExpr(std::string op, std::unique_ptr<Expr> lhs,
             std::unique_ptr<Expr> rhs)
      : op(op), lhs(std::move(lhs)), rhs(std::move(rhs)) {}
};

export class CallExpr : public Expr {
  std::string callee;
  std::vector<std::unique_ptr<Expr>> arguments;

public:
  CallExpr(std::string callee, std::vector<std::unique_ptr<Expr>> arguments)
      : callee(std::move(callee)), arguments(std::move(arguments)) {}
};

export class Prototype {
  std::string name;
  std::vector<std::string> arguments;

public:
  Prototype(std::string name, std::vector<std::string> arguments)
      : name(std::move(name)), arguments(std::move(arguments)) {}

  const std::string &get_name() const { return name; }
};

export class Function {
  std::unique_ptr<Prototype> prototype;
  std::unique_ptr<Expr> body;

public:
  Function(std::unique_ptr<Prototype> prototype, std::unique_ptr<Expr> body)
      : prototype(std::move(prototype)), body(std::move(body)) {}
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
        std::make_unique<ast::Prototype>("", std::vector<std::string>{}),
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
    return parse_expression();
  }
};
