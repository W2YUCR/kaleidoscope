#include <llvm/Support/raw_ostream.h>
#include <memory>
import Parser;
import Token;

#include <cstdio>
#include <iostream>

int main() {
  CodegenContext ctx;
  Parser p;
  for (;;) {
    std::cerr << ">>> ";

    // read
    std::unique_ptr<ast::Expr> ast;

    try {
      ast = p.parse(std::cin);
    } catch (Parser::ParseError e) {
      std::cerr << e.what() << '\n';
      std::cerr << "Errored on: " << p.next() << "\n";
      continue;
    }

    try {
      auto code = ast->codegen(ctx);
      code->print(llvm::errs());
      llvm::errs() << "\n";
    } catch (CodegenException e) {
      std::cerr << e.what() << '\n';
    }
  }
}
