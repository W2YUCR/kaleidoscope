import Parser;
import Token;

#include <iostream>

int main() {
  Parser p;
  for (;;) {
    std::cerr << ">>> ";

    // read
    auto ast = p.parse(std::cin);
  }
}
