module;
#include <cctype>
#include <istream>
#include <ostream>
#include <string>

export module Token;

using namespace std;

export class Token {
#define TOKENTYPES                                                             \
  TT(TypeError)                                                                \
  TT(TypeEOF)                                                                  \
  TT(TypeDef)                                                                  \
  TT(TypeExtern)                                                               \
  TT(TypeIdentifier)                                                           \
  TT(TypeNumber)                                                               \
  TT(TypeLpar)                                                                 \
  TT(TypeRpar)                                                                 \
  TT(TypeOperator)                                                             \
  TT(TypeSemicolon)                                                            \
  TT(TypeComma)

#define TT(x) #x,
  constexpr static char const *const type_str[] = {TOKENTYPES};
#undef TT

public:
  enum Type {
#define TT(x) x,
    TOKENTYPES
#undef TT
  } type;

  std::string literal;
  double number;

  bool operator<=>(const Token &) const = default;

  friend std::istream &operator>>(std::istream &s, Token &t) {
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
        t.type = TypeEOF;
        t.literal = "EOF";
      }
      return s;
    }

    // identifier: [a-Z][0-9a-Z]*
    if (isalpha(c)) {
      t.type = TypeIdentifier;
      while (isalnum(c)) {
        t.literal.push_back(c);
        c = s.get();
      }
      s.unget();
      s.clear();

      if (t.literal == "def")
        t.type = TypeDef;
      if (t.literal == "extern")
        t.type = TypeExtern;
      return s;
    }

    // number: [0-9]*(\.[0-9]*)?
    if (isdigit(c) || c == '.') {
      t.type = TypeNumber;

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
      t.type = TypeLpar;
      t.literal = '(';
      return s;
    case ')':
      t.type = TypeRpar;
      t.literal = ')';
      return s;
    case ';':
      t.type = TypeSemicolon;
      t.literal = ';';
      return s;
    case ',':
      t.type = TypeComma;
      t.literal = ',';
      return s;
    }

    t.type = TypeOperator;

    do {
      t.literal.push_back(c);
      c = s.get();
    } while (!(s.eof() || isspace(c) || isalnum(c) || c == '.' || c == '(' ||
               c == ')' || c == ';'));
    s.unget();
    s.clear();

    return s;
  }

  friend std::ostream &operator<<(std::ostream &s, Token &t) {
    return s << "{" << t.type << ", \"" << t.literal << "\", " << t.number
             << "}";
  }

  friend std::ostream &operator<<(std::ostream &s, Type &t) {
    return s << type_str[t];
  }
};
