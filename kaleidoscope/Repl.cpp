#include "compiler/Parser.hpp"
#include "compiler/Token.hpp"
#include "llvm/Config/llvm-config.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include <fstream>
#include <iostream>
#include <istream>
#include <llvm/ADT/StringMapEntry.h>
#include <string>

using namespace llvm;
using namespace llvm::orc;
class ResourceTrackerManager {
  ResourceTrackerSP rt;

public:
  ResourceTrackerManager(ResourceTrackerSP rt) : rt(rt) {}
  ~ResourceTrackerManager() { ExitOnError()(rt->remove()); }
};

class TeeString : public std::streambuf {
public:
  std::istream *source;
  std::string teed;
  TeeString(std::istream *source) : source(source) {}

  // supply some string to start at the end of.
  TeeString(std::istream *source, std::string const &precursor)
      : source(source), teed(precursor) {
    setg(teed.data(), teed.data() + teed.size(), teed.data() + teed.size());
  }
  virtual int_type underflow() override {
    auto c = source->get();
    if (c == EOF)
      return EOF;
    teed.push_back(c);
    setg(teed.data(), teed.data() + teed.size() - 1, teed.data() + teed.size());
    return c;
  }

  // since ungetc isn't passed to the underlying stream, past the used size is
  // where the ungotten characters are.
  size_t used() const { return gptr() - eback(); }
};

int run(std::istream &input, bool interactive = false) {
  // todo: "taint" functions' removability if another function depends on it (or
  // somehow regenerate dependent functions)
  using namespace llvm;
  using namespace llvm::orc;
  InitializeNativeTarget();
  InitializeNativeTargetAsmParser();
  InitializeNativeTargetAsmPrinter();
  ExitOnError ExitOnErr;

  auto jit = ExitOnErr(llvm::orc::LLJITBuilder().create());
  std::map<std::string, std::shared_ptr<ast::Prototype>> prototypes;
  std::map<std::string, std::unique_ptr<ResourceTrackerManager>> whatprovides;

  Parser p;
  for (;;) {
    // prompt
    if (interactive)
      std::cerr << ">>> ";

    // read
    CodegenContext ctx(jit.get(), &prototypes);
    std::unique_ptr<ast::Expr> ast;

    TeeString tee =
        p.peeking() ? TeeString{&input, p.peek().literal} : TeeString{&input};
    std::istream tee_stream(&tee);

    try {
      ast = p.parse(tee_stream);
    } catch (Parser::ParseError e) {
      std::cerr << e.what() << '\n';
      std::string already_read = tee.teed;
      std::string rest_of_line;
      auto used = tee.used();
      std::getline(tee_stream, rest_of_line);
      std::cerr << already_read << rest_of_line << '\n';

      for (auto i = int(used) - int(p.peek().literal.size()); i-- > 0;) {
        if (tee.teed[i] == '\n')
          break;
        std::cerr << ' ';
      }
      std::cerr << "^ Unexpected " << p.next() << "\n";
      if (!interactive)
        return 1;
      continue;
    }

    // compile
    Value *code;
    try {
      code = ast->codegen(ctx);
      code->print(llvm::errs());
      llvm::errs() << "\n";
    } catch (CodegenException e) {
      std::cerr << e.what() << '\n';
      if (!interactive)
        return 1;
      continue;
    }

    auto resource_tracker = jit->getMainJITDylib().createResourceTracker();

    auto top = dynamic_cast<ast::Function *>(ast.get());
    if (top) {
      whatprovides[top->get_name()] =
          std::make_unique<ResourceTrackerManager>(resource_tracker);
    }

    auto ts_module =
        ThreadSafeModule(std::move(ctx.module), std::move(ctx.ctx));
    ExitOnErr(jit->addIRModule(resource_tracker, std::move(ts_module)));

    // evaluate if top level)
    if (top && top->get_name() == "__anon_expr") {
      auto expr_symbol = ExitOnErr(jit->lookup("__anon_expr"));
      // assert(ExprSymbol && "Function not found");

      // get function
      double (*FP)() = expr_symbol.toPtr<double (*)()>();
      fprintf(stderr, "Evaluated to %f\n", FP());

      // remove __anon_expr's module
      whatprovides.erase(whatprovides.find("__anon_expr"));
    }

    // not top level so not managed by whatprovides
    if (!top)
      ExitOnErr(resource_tracker->remove());
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2)
    return run(std::cin, true);

  if (argc == 2) {
    std::ifstream input(argv[1]);
    if (input.fail()) {
      std::cerr << "Failed to open " << argv[1] << ".";
      return 1;
    }
    return run(input, false);
  }

  std::cerr << "Usage: " << argv[0] << " [SCRIPT]";
}
