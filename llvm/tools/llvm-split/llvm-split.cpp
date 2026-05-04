//===-- llvm-split: command line tool for testing module splitting --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This program can be used to test the llvm::SplitModule and
// TargetMachine::splitModule functions.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/PassInstrumentation.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/AMDGPUSplitModuleOptions.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/IPO/GlobalDCE.h"
#include "llvm/Transforms/Utils/SplitModule.h"
#include "llvm/Transforms/Utils/SplitModuleByCategory.h"

using namespace llvm;
using namespace llvm::clv2;

static constexpr OptionCategory SplitCategory{"Split Options"};

static constexpr OptionInfo<std::string> InputFilename{
    "input", "<input bitcode file>", Positional{}, Init{"-"},
    cat(SplitCategory)};

static constexpr OptionInfo<std::string> OutputFilename{
    "o", "Override output filename", value_desc("filename"),
    cat(SplitCategory)};

static constexpr OptionInfo<unsigned> NumOutputs{
    "j", "Number of output files", PrefixFormat, Init{2u}, cat(SplitCategory)};

static constexpr OptionInfo<bool> PreserveLocals{
    "preserve-locals", "Split without externalizing locals",
    cat(SplitCategory)};

static constexpr OptionInfo<bool> RoundRobin{
    "round-robin",
    "Use round-robin distribution of functions to modules instead of the "
    "default name-hash-based one",
    cat(SplitCategory)};

static constexpr OptionInfo<std::string> MTriple{
    "mtriple",
    "Target triple. When present, a TargetMachine is created and "
    "TargetMachine::splitModule is used instead of the common SplitModule "
    "logic.",
    value_desc("triple"), cat(SplitCategory)};

static constexpr OptionInfo<std::string> MCPU{
    "mcpu", "Target CPU, ignored if --mtriple is not used", value_desc("cpu"),
    cat(SplitCategory)};

enum class SplitByCategoryType {
  SBCT_ByAttribute,
  SBCT_ByKernel,
  SBCT_None,
};

static constexpr EnumVal<SplitByCategoryType> SplitByCategoryVals[] = {
    {"attribute", SplitByCategoryType::SBCT_ByAttribute,
     "one output module per unique value of the function attribute named by "
     "--category-attribute"},
    {"kernel", SplitByCategoryType::SBCT_ByKernel,
     "one output module per kernel"},
};

static constexpr auto SplitByCategory = makeEnumOption<SplitByCategoryType>(
    "split-by-category",
    "Split by category. If present, splitting by category is used with the "
    "specified categorization type.",
    SplitByCategoryVals, Init{SplitByCategoryType::SBCT_None},
    cat(SplitCategory));

static constexpr OptionInfo<std::string> CategoryAttribute{
    "category-attribute",
    "Function attribute name to use when splitting with "
    "-split-by-category=attribute",
    value_desc("name"), cat(SplitCategory)};

static constexpr OptionInfo<bool> OutputAssembly{
    "S", "Write output as LLVM assembly", cat(SplitCategory)};

static constexpr OptionInfo<unsigned> AMDGPUMaxDepth{
    "amdgpu-module-splitting-max-depth",
    "maximum search depth. 0 forces a greedy approach. "
    "warning: the algorithm is up to O(2^N), where N is the max depth.",
    Hidden, Init{8u}, cat(SplitCategory)};

static constexpr OptionInfo<float> AMDGPULargeFnFactor{
    "amdgpu-module-splitting-large-threshold",
    "when max depth is reached and we can no longer branch out, this value "
    "determines if a function is worth merging into an already existing "
    "partition to reduce code duplication.",
    Hidden, Init{2.0f}, cat(SplitCategory)};

static constexpr OptionInfo<float> AMDGPULargeFnOverlapForMerge{
    "amdgpu-module-splitting-merge-threshold",
    "when a function is considered for merging into a partition that already "
    "contains some of its callees, do the merge if at least n% of the code "
    "it can reach is already present inside the partition.",
    Hidden, Init{0.7f}, cat(SplitCategory)};

static constexpr OptionInfo<bool> AMDGPUNoExternalizeGlobals{
    "amdgpu-module-splitting-no-externalize-globals",
    "disables externalization of global variable with local linkage.", Hidden,
    cat(SplitCategory)};

static constexpr OptionInfo<bool> AMDGPUNoExternalizeOnAddrTaken{
    "amdgpu-module-splitting-no-externalize-address-taken",
    "disables externalization of functions whose addresses are taken.", Hidden,
    cat(SplitCategory)};

static constexpr OptionInfo<std::string> AMDGPUModuleDotCfgOutput{
    "amdgpu-module-splitting-print-module-dotcfg",
    "output file to write out the dotgraph representation of the input module.",
    Hidden, cat(SplitCategory)};

static constexpr OptionInfo<std::string> AMDGPUPartitionSummariesOutput{
    "amdgpu-module-splitting-print-partition-summaries",
    "output file to write out a summary of the partitions created for each "
    "module.",
    Hidden, cat(SplitCategory)};

static constexpr OptionInfo<std::string> DebugOnly{
    "debug-only",
    "Enable a specific type of debug output (comma separated list of types)",
    Hidden, cat(SplitCategory)};

static constexpr OptionsRegistry<
    &InputFilename, &OutputFilename, &NumOutputs, &PreserveLocals, &RoundRobin,
    &MTriple, &MCPU, &SplitByCategory, &CategoryAttribute, &OutputAssembly,
    &AMDGPUMaxDepth, &AMDGPULargeFnFactor, &AMDGPULargeFnOverlapForMerge,
    &AMDGPUNoExternalizeGlobals, &AMDGPUNoExternalizeOnAddrTaken,
    &AMDGPUModuleDotCfgOutput, &AMDGPUPartitionSummariesOutput, &DebugOnly>
    SplitToolReg;

void writeStringToFile(StringRef Content, StringRef Path) {
  std::error_code EC;
  raw_fd_ostream OS(Path, EC);
  if (EC) {
    errs() << formatv("error opening file: {0}, error: {1}\n", Path,
                      EC.message());
    exit(1);
  }

  OS << Content << "\n";
}

void writeModuleToFile(const Module &M, StringRef Path, bool DoOutputAssembly) {
  int FD = -1;
  if (std::error_code EC = sys::fs::openFileForWrite(Path, FD)) {
    errs() << formatv("error opening file: {0}, error: {1}", Path, EC.message())
           << '\n';
    exit(1);
  }

  raw_fd_ostream OS(FD, /*ShouldClose*/ true);
  if (DoOutputAssembly)
    M.print(OS, /*AssemblyAnnotationWriter*/ nullptr);
  else
    WriteBitcodeToFile(M, OS);
}

/// EntryPointCategorizer is used for splitting by category either by a named
/// function attribute or by kernels. It doesn't provide categories for
/// functions other than kernels. Categorizer computes a string key for the
/// given Function and records the association between the string key and an
/// integer category. If a string key already belongs to some category then the
/// corresponding integer category is returned.
class EntryPointCategorizer {
public:
  EntryPointCategorizer(SplitByCategoryType Type, StringRef AttributeName)
      : Type(Type), AttributeName(AttributeName) {}

  EntryPointCategorizer() = delete;
  EntryPointCategorizer(EntryPointCategorizer &) = delete;
  EntryPointCategorizer &operator=(const EntryPointCategorizer &) = delete;
  EntryPointCategorizer(EntryPointCategorizer &&) = default;
  EntryPointCategorizer &operator=(EntryPointCategorizer &&) = default;

  /// Returns integer specifying the category for the given \p F.
  /// If the given function isn't a kernel then returns std::nullopt.
  std::optional<int> operator()(const Function &F) {
    if (!isEntryPoint(F))
      return std::nullopt; // skip the function.

    auto StringKey = computeFunctionCategory(Type, F);
    if (auto it = StrKeyToID.find(StringRef(StringKey)); it != StrKeyToID.end())
      return it->second;

    int ID = static_cast<int>(StrKeyToID.size());
    return StrKeyToID.try_emplace(std::move(StringKey), ID).first->second;
  }

private:
  static bool isEntryPoint(const Function &F) {
    if (F.isDeclaration())
      return false;

    return F.hasKernelCallingConv();
  }

  SmallString<0> computeFunctionCategory(SplitByCategoryType Type,
                                         const Function &F) {
    SmallString<0> Key;
    switch (Type) {
    case SplitByCategoryType::SBCT_ByKernel:
      Key = F.getName().str();
      break;
    case SplitByCategoryType::SBCT_ByAttribute:
      Key = F.getFnAttribute(AttributeName).getValueAsString().str();
      break;
    default:
      llvm_unreachable("unexpected mode.");
    }

    return Key;
  }

private:
  struct KeyInfo {
    static bool isEqual(const SmallString<0> &LHS, const SmallString<0> &RHS) {
      return LHS == RHS;
    }

    static unsigned getHashValue(const SmallString<0> &S) {
      return llvm::hash_value(StringRef(S));
    }
  };

  SplitByCategoryType Type;
  std::string AttributeName;
  DenseMap<SmallString<0>, int, KeyInfo> StrKeyToID;
};

void cleanupModule(Module &M) {
  ModuleAnalysisManager MAM;
  MAM.registerPass([&] { return PassInstrumentationAnalysis(); });
  ModulePassManager MPM;
  MPM.addPass(GlobalDCEPass()); // Delete unreachable globals.
  MPM.run(M, MAM);
}

Error runSplitModuleByCategory(std::unique_ptr<Module> M,
                               SplitByCategoryType CategoryType,
                               StringRef AttributeName, StringRef OutFilename,
                               bool DoOutputAssembly) {
  if (CategoryType == SplitByCategoryType::SBCT_ByAttribute &&
      AttributeName.empty())
    return createStringError(
        "-split-by-category=attribute requires --category-attribute=<name>");

  size_t OutputID = 0;
  auto PostSplitCallback = [&](std::unique_ptr<Module> MPart) -> Error {
    if (verifyModule(*MPart)) {
      errs() << "Broken Module!\n";
      exit(1);
    }

    cleanupModule(*MPart);
    size_t ID = OutputID;
    ++OutputID;
    StringRef ModuleSuffix = DoOutputAssembly ? ".ll" : ".bc";
    std::string ModulePath =
        (Twine(OutFilename) + "_" + Twine(ID) + ModuleSuffix).str();
    writeModuleToFile(*MPart, ModulePath, DoOutputAssembly);
    return Error::success();
  };

  auto Categorizer = EntryPointCategorizer(CategoryType, AttributeName);
  return splitModuleTransitiveFromEntryPoints(std::move(M), Categorizer,
                                              PostSplitCallback);
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  clv2::OptionParser P;
  P.add<&SplitToolReg>();
  RegisterAllLLVMOptions(P);
  P.hideUnrelatedOptions({&SplitCategory, &getColorCategory()});
  auto OptsCtx = P.parse(argc, argv, "LLVM module splitter\n");
  auto *Opts = OptsCtx->getViewPtr<&SplitToolReg>();

  if (!Opts->get<&DebugOnly>().empty()) {
    llvm::DebugFlag = true;
    setCurrentDebugType(Opts->get<&DebugOnly>().c_str());
  }

  LLVMContext Context(*OptsCtx);
  SMDiagnostic Err;

  Triple TT(Opts->get<&MTriple>());

  std::unique_ptr<TargetMachine> TM;
  if (!Opts->get<&MTriple>().empty()) {
    InitializeAllTargets();
    InitializeAllTargetMCs();

    std::string Error;
    const Target *T = TargetRegistry::lookupTarget(TT, Error);
    if (!T) {
      errs() << "unknown target '" << Opts->get<&MTriple>() << "': " << Error
             << "\n";
      return 1;
    }

    TargetOptions Options;
    TM = std::unique_ptr<TargetMachine>(
        T->createTargetMachine(TT, Opts->get<&MCPU>(), /*FS*/ "", Options,
                               std::nullopt, std::nullopt));
  }

  std::unique_ptr<Module> M =
      parseIRFile(Opts->get<&InputFilename>(), Err, Context);

  if (!M) {
    Err.print(argv[0], errs());
    return 1;
  }

  const std::string &OutFilename = Opts->get<&OutputFilename>();
  bool DoOutputAssembly = Opts->get<&OutputAssembly>();
  unsigned NOutputs = Opts->get<&NumOutputs>();

  unsigned I = 0;
  const auto HandleModulePart = [&](std::unique_ptr<Module> MPart) {
    std::error_code EC;
    std::unique_ptr<ToolOutputFile> Out(
        new ToolOutputFile(OutFilename + utostr(I++), EC, sys::fs::OF_None));
    if (EC) {
      errs() << EC.message() << '\n';
      exit(1);
    }

    if (verifyModule(*MPart, &errs())) {
      errs() << "Broken module!\n";
      exit(1);
    }

    WriteBitcodeToFile(*MPart, Out->os());

    Out->keep();
  };

  if (Opts->get<&SplitByCategory>() != SplitByCategoryType::SBCT_None) {
    auto E = runSplitModuleByCategory(
        std::move(M), Opts->get<&SplitByCategory>(),
        Opts->get<&CategoryAttribute>(), OutFilename, DoOutputAssembly);
    if (E) {
      errs() << "error: " << toString(std::move(E)) << "\n";
      return 1;
    }

    return 0;
  }

  if (TM) {
    if (Opts->get<&PreserveLocals>()) {
      errs() << "warning: --preserve-locals has no effect when using "
                "TargetMachine::splitModule\n";
    }
    if (Opts->get<&RoundRobin>())
      errs() << "warning: --round-robin has no effect when using "
                "TargetMachine::splitModule\n";

    AMDGPUSplitModuleOptions AmdOpts;
    AmdOpts.MaxDepth = Opts->get<&AMDGPUMaxDepth>();
    AmdOpts.LargeFnFactor = Opts->get<&AMDGPULargeFnFactor>();
    AmdOpts.LargeFnOverlapForMerge = Opts->get<&AMDGPULargeFnOverlapForMerge>();
    AmdOpts.NoExternalizeGlobals = Opts->get<&AMDGPUNoExternalizeGlobals>();
    AmdOpts.NoExternalizeOnAddrTaken =
        Opts->get<&AMDGPUNoExternalizeOnAddrTaken>();
    AmdOpts.ModuleDotCfgOutput = Opts->get<&AMDGPUModuleDotCfgOutput>();
    AmdOpts.PartitionSummariesOutput =
        Opts->get<&AMDGPUPartitionSummariesOutput>();

    if (TM->splitModule(*M, NOutputs, HandleModulePart, &AmdOpts))
      return 0;

    errs() << "warning: "
              "TargetMachine::splitModule failed, falling back to default "
              "splitModule implementation\n";
  }

  SplitModule(*M, NOutputs, HandleModulePart, Opts->get<&PreserveLocals>(),
              Opts->get<&RoundRobin>());
  return 0;
}
