#include "Token.hpp"

bool Token::operator==(const Token &that) const {
  return this->type == that.type && this->literal == that.literal &&
         this->number == that.number;
};
bool Token::operator!=(const Token &that) const { return !(*this == that); };

std::istream &operator>>(std::istream &s, Token &t) {
  if (s.fail())
    return s;
  t = {};
  char c;

  // Get the first non-whitespace character
  do {
    c = s.get();
  } while (isspace(c));

  if (s.eof()) {
    {
      t.type = Token::TypeEOF;
      t.literal = "EOF";
    }
    return s;
  }

  // identifier: [a-Z][0-9a-Z]*
  if (isalpha(c)) {
    t.type = Token::TypeIdentifier;
    while (isalnum(c)) {
      t.literal.push_back(c);
      c = s.get();
    }
    s.unget();
    s.clear();

    if (t.literal == "def")
      t.type = Token::TypeDef;
    if (t.literal == "extern")
      t.type = Token::TypeExtern;
    return s;
  }

  // number: [0-9]*(\.[0-9]*)?
  if (isdigit(c) || c == '.') {
    t.type = Token::TypeNumber;

    while (isdigit(c)) {
      t.literal.push_back(c);
      c = s.get();
    }

    if (c == '.') {
      do {
        t.literal.push_back(c);
        c = s.get();
      } while (isdigit(c));
    }

    t.number = stod(t.literal);
    s.unget();
    s.clear();
    return s;
  }

  switch (c) {
  case '(':
    t.type = Token::TypeLpar;
    t.literal = '(';
    return s;
  case ')':
    t.type = Token::TypeRpar;
    t.literal = ')';
    return s;
  case ';':
    t.type = Token::TypeSemicolon;
    t.literal = ';';
    return s;
  case ',':
    t.type = Token::TypeComma;
    t.literal = ',';
    return s;
  }

  t.type = Token::TypeOperator;

  do {
    t.literal.push_back(c);
    c = s.get();
  } while (!(s.eof() || isspace(c) || isalnum(c) || c == '.' || c == '(' ||
             c == ')' || c == ';'));
  s.unget();
  s.clear();

  return s;
}
