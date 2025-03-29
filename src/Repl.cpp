#include "KaleidoscopeJIT.hpp"
#include <iostream>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/raw_ostream.h>
import Parser;
import Token;

int main() {
  using namespace llvm;
  using namespace llvm::orc;
  LLVMInitializeNativeTarget();
  LLVMInitializeNativeAsmPrinter();
  LLVMInitializeNativeAsmParser();
  ExitOnError ExitOnErr;

  auto jit = ExitOnErr(orc::KaleidoscopeJIT::Create());
  std::map<std::string, std::shared_ptr<ast::Prototype>> prototypes;
  CodegenContext ctx(jit.get(), &prototypes);

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

    Value *code;
    try {
      code = ast->codegen(ctx);
      code->print(llvm::errs());
      llvm::errs() << "\n";
    } catch (CodegenException e) {
      std::cerr << e.what() << '\n';
      continue;
    }

    auto resource_tracker = jit->getMainJITDylib().createResourceTracker();

    auto ts_module =
        ThreadSafeModule(std::move(ctx.module), std::move(ctx.ctx));
    ExitOnErr(jit->addModule(std::move(ts_module), resource_tracker));
    ctx = {jit.get(), &prototypes};

    // top level if the name is __anon_expr
    if (auto top = dynamic_cast<ast::Function *>(ast.get());
        top && top->get_name() == "__anon_expr") {

      auto expr_symbol = ExitOnErr(jit->lookup("__anon_expr"));
      // assert(ExprSymbol && "Function not found");

      // get function
      double (*FP)() = expr_symbol.getAddress().toPtr<double (*)()>();
      fprintf(stderr, "Evaluated to %f\n", FP());

      // remove __anon_expr's module
      ExitOnErr(resource_tracker->remove());
    }

    // todo: keep resource_tracker for named functions so when a def with the
    // same name is performed, the old function can be removed.
  }
}
