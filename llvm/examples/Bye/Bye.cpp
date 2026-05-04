#include "llvm/IR/Function.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

static constexpr clv2::OptionInfo<bool> OI_Wave{"wave-goodbye",
                                                "wave good bye"};
static constexpr clv2::OptionInfo<bool> OI_LastWords{
    "last-words", "say last words (suppress codegen)"};
static constexpr clv2::OptionsRegistry<&OI_Wave, &OI_LastWords> ByeOptsReg;

// Options are read from the LLVMContext owning the IR rather than mirrored
// into globals, so concurrent in-process jobs can use different settings.
static bool waveGoodbye(const LLVMContext &Ctx) {
  return clv2::getOptValOr<&ByeOptsReg, &OI_Wave>(Ctx.getOptionsContext(),
                                                  false);
}
static bool lastWords(const LLVMContext &Ctx) {
  return clv2::getOptValOr<&ByeOptsReg, &OI_LastWords>(Ctx.getOptionsContext(),
                                                       false);
}

static const int RegisterByeOpts = [] {
  clv2::registerDynamicRegistry<&ByeOptsReg>();
  return 0;
}();

namespace {

bool runBye(Function &F) {
  if (waveGoodbye(F.getContext())) {
    errs() << "Bye: ";
    errs().write_escaped(F.getName()) << '\n';
  }
  return false;
}

struct LegacyBye : public FunctionPass {
  static char ID;
  LegacyBye() : FunctionPass(ID) {}
  bool runOnFunction(Function &F) override { return runBye(F); }
};

struct Bye : OptionalPassInfoMixin<Bye> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
    if (!runBye(F))
      return PreservedAnalyses::all();
    return PreservedAnalyses::none();
  }
};

void registerPassBuilderCallbacks(PassBuilder &PB) {
  PB.registerVectorizerStartEPCallback(
      [](llvm::FunctionPassManager &PM, OptimizationLevel Level) {
        PM.addPass(Bye());
      });
  PB.registerPipelineParsingCallback(
      [](StringRef Name, llvm::FunctionPassManager &PM,
         ArrayRef<llvm::PassBuilder::PipelineElement>) {
        if (Name == "goodbye") {
          PM.addPass(Bye());
          return true;
        }
        return false;
      });
}

bool preCodeGenCallback(Module &M, TargetMachine &, CodeGenFileType CGFT,
                        raw_pwrite_stream &OS) {
  if (lastWords(M.getContext())) {
    if (CGFT != CodeGenFileType::AssemblyFile) {
      // Test error emission.
      M.getContext().emitError("last words unsupported for binary output");
      return false;
    }
    OS << "CodeGen Bye\n";
    return true; // Suppress remaining compilation pipeline.
  }
  // Do nothing.
  return false;
}

} // namespace

char LegacyBye::ID = 0;

static RegisterPass<LegacyBye> X("goodbye", "Good Bye World Pass",
                                 false /* Only looks at CFG */,
                                 false /* Analysis Pass */);

/* New PM Registration */
llvm::PassPluginLibraryInfo getByePluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "Bye", LLVM_VERSION_STRING,
          registerPassBuilderCallbacks, preCodeGenCallback};
}

#ifndef LLVM_BYE_LINK_INTO_TOOLS
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getByePluginInfo();
}
#endif
