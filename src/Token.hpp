#ifndef TOKEN_HPP
#define TOKEN_HPP

#include <cctype>
#include <compare>
#include <istream>
#include <ostream>
#include <string>

class Token {
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

  bool operator==(const Token &) const;
  bool operator!=(const Token &) const;

  friend std::istream &operator>>(std::istream &s, Token &t);

  friend std::ostream &operator<<(std::ostream &s, Token &t) {
    return s << "{" << t.type << ", \"" << t.literal << "\", " << t.number
             << "}";
  }

  friend std::ostream &operator<<(std::ostream &s, Type &t) {
    return s << type_str[t];
  }
};

#endif
