#ifndef TOKEN_HPP
#define TOKEN_HPP

#include <istream>
#include <ostream>
#include <string>

class Token {
#define TOKENTYPES                                                             \
  TT(Error)                                                                    \
  TT(EOF)                                                                      \
  TT(Def)                                                                      \
  TT(Extern)                                                                   \
  TT(Identifier)                                                               \
  TT(Number)                                                                   \
  TT(Lpar)                                                                     \
  TT(Rpar)                                                                     \
  TT(Operator)                                                                 \
  TT(Semicolon)                                                                \
  TT(Comma)                                                                    \
  TT(If)                                                                       \
  TT(Then)                                                                     \
  TT(Else)                                                                     \
  TT(For)                                                                      \
  TT(In)

#define TT(x) #x,
  constexpr static char const *const type_str[] = {TOKENTYPES};
#undef TT

public:
  enum Type {
#define TT(x) Type##x,
    TOKENTYPES
#undef TT
  } type;

  std::string literal;
  double number;

  bool operator==(const Token &) const;
  bool operator!=(const Token &) const;

  friend std::istream &operator>>(std::istream &s, Token &t);

  friend std::ostream &operator<<(std::ostream &s, Token const &t) {
    return s << "{" << t.type << ", \"" << t.literal << "\", " << t.number
             << "}";
  }

  friend std::ostream &operator<<(std::ostream &s, Type &t) {
    return s << type_str[t];
  }
};

#endif
