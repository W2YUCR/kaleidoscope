#include "kaleidoscope/compiler/Token.hpp"
#include <functional>
#include <initializer_list>
#include <iostream>
#include <unordered_map>

#include <sstream>

int test_tokenize(std::string code, std::vector<Token> expected) {
  std::istringstream s(code);
  Token t;
  std::vector<Token> actual;
  while (s >> t) {
    actual.push_back(t);
  }

  if (actual != expected) {
    std::cerr << "Mismatch between actual and expected tokens.\n";
    std::cerr << "Actual:  ";
    auto sep = "";
    for (auto t : actual) {
      std::cerr << sep << t;
      sep = ", ";
    }
    std::cerr << "\nExpected:";
    sep = "";
    for (auto t : expected) {
      std::cerr << t << sep;
      sep = ", ";
    }
    std::cerr << "\n";
    return 1;
  }
  return 0;
}

std::unordered_map<std::string, std::function<int()>> const tests = {
    {"Number",
     []() {
       auto expected = std::vector<Token>{
           {Token::TypeNumber, "1.0", 1},
       };
       return test_tokenize("1.0", expected);
     }},
    {"Function",
     []() {
       using enum Token::Type;

       // Example code from
       // https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl01.html
       auto expected = std::vector<Token>{
           {TypeDef, "def", 0},        {TypeIdentifier, "fib", 0},
           {TypeLpar, "(", 0},         {TypeIdentifier, "x", 0},
           {TypeRpar, ")", 0},         {TypeIdentifier, "if", 0},
           {TypeIdentifier, "x", 0},   {TypeOperator, "<", 0},
           {TypeNumber, "3", 3},       {TypeIdentifier, "then", 0},
           {TypeNumber, "1", 1},       {TypeIdentifier, "else", 0},
           {TypeIdentifier, "fib", 0}, {TypeLpar, "(", 0},
           {TypeIdentifier, "x", 0},   {TypeOperator, "-", 0},
           {TypeNumber, "1", 1},       {TypeRpar, ")", 0},
           {TypeOperator, "+", 0},     {TypeIdentifier, "fib", 0},
           {TypeLpar, "(", 0},         {TypeIdentifier, "x", 0},
           {TypeOperator, "-", 0},     {TypeNumber, "2", 2},
           {TypeRpar, ")", 0},
       };
       return test_tokenize("def fib(x)\n"
                            "  if x < 3 then\n"
                            "    1\n"
                            "  else\n"
                            "    fib(x-1)+fib(x-2)\n",
                            expected);
     }},
};

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "No test number specified\n";
    return 1;
  }
  auto test = tests.find(argv[1]);
  if (test == tests.end()) {
    std::cerr << "Missing test " << argv[1] << ".\n";
    return 1;
  }
  return test->second();
}
