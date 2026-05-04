//===-- llcdriver.cpp - Implement the LLVM Native Code Generator ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the llc code generator driver. It provides a convenient
// command-line interface for generating an assembly file or a relocatable file,
// given LLVM bitcode.
//
//===----------------------------------------------------------------------===//

#include "NewPMDriver.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/RuntimeLibcallInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/CodeGen/LinkAllAsmWriterComponents.h"
#include "llvm/CodeGen/LinkAllCodegenComponents.h"
#include "llvm/CodeGen/MIRParser/MIRParser.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/AutoUpgrade.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LLVMRemarkStreamer.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCTargetOptionsCommandFlags.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassesOptionsOptInfos.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/PGOOptions.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Target/CGPassBuilderOption.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include <cassert>
#include <memory>
#include <optional>
using namespace llvm;
using namespace llvm::clv2;

static codegen::RegisterCodeGenFlags CGF;
static codegen::RegisterMTuneFlag MTF;
static codegen::RegisterSaveStatsFlag SSF;

// General options for llc.  Other pass-specific options are specified
// within the corresponding llc passes, and target-specific options
// and back-end code generation options are specified with the target machine.
//
inline constexpr OptionInfo<std::string> InputFilename{"", "<input bitcode>",
                                                       Positional{}, Init{"-"}};

inline constexpr ListOptionInfo<std::string> InstPrinterOptions{
    "M", "InstPrinter options", ZeroOrMore};

inline constexpr OptionInfo<std::string> InputLanguage{
    "x", "Input language ('ir' or 'mir')"};

inline constexpr OptionInfo<std::string> OutputFilename{"o", "Output filename",
                                                        value_desc("filename")};

inline constexpr OptionInfo<std::string> SplitDwarfOutputFile{
    "split-dwarf-output", ".dwo output filename", value_desc("filename")};

inline constexpr OptionInfo<unsigned> TimeCompilations{
    "time-compilations", "Repeat compilation N times for timing", Hidden,
    Init{1u}, value_desc("N")};

inline constexpr OptionInfo<bool> TimeTrace{"time-trace", "Record time trace"};

inline constexpr OptionInfo<unsigned> TimeTraceGranularity{
    "time-trace-granularity",
    "Minimum time granularity (in microseconds) traced by time profiler",
    Init{500u}, Hidden};

inline constexpr OptionInfo<std::string> TimeTraceFile{
    "time-trace-file", "Specify time trace file destination",
    value_desc("filename")};

inline constexpr OptionInfo<std::string> BinutilsVersion{
    "binutils-version",
    "Produced object files can use all ELF features "
    "supported by this binutils version and newer."
    "If -no-integrated-as is specified, the generated "
    "assembly will consider GNU as support."
    "'none' means that all ELF features can be used, "
    "regardless of binutils support",
    Hidden};

inline constexpr OptionInfo<bool> PreserveComments{
    "preserve-as-comments", "Preserve Comments in outputted assembly", Hidden,
    Init{true}};

// Determine optimization level.
// NOTE: modeled as std::string (not char) because clv2 has no scalar-char
// OptionInfo specialization; see llvm/tools/lli/lli.cpp's LLI_OptLevel and
// llvm/tools/lto/lto.cpp's OI_LTOOptLevel for the same precedent.
inline constexpr OptionInfo<std::string> OptLevel{
    "O", "Optimization level. [-O0, -O1, -O2, or -O3] (default = '-O2')",
    PrefixFormat, Init{"2"}};

inline constexpr OptionInfo<std::string> TargetTriple{
    "mtriple", "Override target triple for module"};

inline constexpr OptionInfo<std::string> SplitDwarfFile{
    "split-dwarf-file",
    "Specify the name of the .dwo file to encode in the DWARF output"};

inline constexpr OptionInfo<bool> NoVerify{
    "disable-verify", "Do not verify input module", Hidden};

inline constexpr OptionInfo<bool> VerifyEach{"verify-each",
                                             "Verify after each transform"};

inline constexpr OptionInfo<bool> DisableSimplifyLibCalls{
    "disable-simplify-libcalls", "Disable simplify-libcalls"};

inline constexpr OptionInfo<bool> ShowMCEncoding{
    "show-mc-encoding", "Show encoding in .s output", Hidden};

inline constexpr OptionInfo<unsigned> OutputAsmVariant{
    "output-asm-variant", "Syntax variant to use for output printing"};

inline constexpr OptionInfo<bool> DwarfDirectory{
    "dwarf-directory", "Use .file directives with an explicit directory",
    Hidden, Init{true}};

inline constexpr OptionInfo<bool> AsmVerbose{
    "asm-verbose", "Add comments to directives.", Init{true}};

inline constexpr OptionInfo<bool> CompileTwice{
    "compile-twice",
    "Run everything twice, re-using the same pass "
    "manager and verify the result is the same.",
    Hidden, Init{false}};

inline constexpr OptionInfo<bool> DiscardValueNames{
    "discard-value-names", "Discard names from Value (other than GlobalValue).",
    Init{false}, Hidden};

inline constexpr OptionInfo<bool> PrintMIR2VecVocab{
    "print-mir2vec-vocab", "Print MIR2Vec vocabulary contents", Hidden,
    Init{false}};

inline constexpr OptionInfo<bool> PrintMIR2Vec{
    "print-mir2vec", "Print MIR2Vec embeddings for functions", Hidden,
    Init{false}};

inline constexpr ListOptionInfo<std::string> IncludeDirs{
    "I", "include search path", ZeroOrMore};

inline constexpr OptionInfo<bool> RemarksWithHotness{
    "pass-remarks-with-hotness",
    "With PGO, include profile count in optimization remarks", Hidden};

// RemarksHotnessThreshold uses a custom parser for std::optional<uint64_t>
// (values "auto" or an integer). Declare as string; parse manually in
// llcMain() (mirrors llvm/tools/opt/optdriver.cpp's RemarksHotnessThreshold).
inline constexpr OptionInfo<std::string> RemarksHotnessThreshold{
    "pass-remarks-hotness-threshold",
    "Minimum profile count required for "
    "an optimization remark to be output. "
    "Use 'auto' to apply the threshold from profile summary.",
    value_desc("N or 'auto'"), Hidden};

inline constexpr OptionInfo<std::string> RemarksFilename{
    "pass-remarks-output", "Output filename for pass remarks",
    value_desc("filename")};

inline constexpr OptionInfo<std::string> RemarksPasses{
    "pass-remarks-filter",
    "Only record optimization remarks from passes whose "
    "names match the given regular expression",
    value_desc("regex")};

inline constexpr OptionInfo<std::string> RemarksFormat{
    "pass-remarks-format",
    "The format used for serializing remarks (default: YAML)",
    value_desc("format"), Init{"yaml"}};

inline constexpr ListOptionInfo<std::string> PassPlugins{
    "load-pass-plugin", "Load plugin library", ZeroOrMore};

inline constexpr OptionInfo<bool> EnableNewPassManager{
    "enable-new-pm", "Enable the new pass manager", Init{false}};

// This flag specifies a textual description of the optimization pass pipeline
// to run over the module. This flag switches opt to use the new pass manager
// infrastructure, completely disabling all of the flags specific to the old
// pass management.
inline constexpr OptionInfo<std::string> PassPipeline{
    "passes",
    "A textual description of the pass pipeline. To have analysis passes "
    "available before a certain pass, add 'require<foo-analysis>'."};
inline constexpr AliasInfo PassPipeline2{"p", "passes", "Alias for -passes"};

// May be specified multiple times (each occurrence's value is itself allowed
// to be a comma separated list); mirrors the old cl::location(RunPassOpt)
// trick which appended to a shared vector on every occurrence of the flag.
inline constexpr ListOptionInfo<std::string> RunPass{
    "run-pass", "Run compiler only for specified passes (comma separated list)",
    value_desc("pass-name"), ZeroOrMore};

// PGO command line options
enum PGOKind {
  NoPGO,
  SampleUse,
};

inline constexpr EnumVal<PGOKind> PGOKindVals[] = {
    EnumVal<PGOKind>{"nopgo", NoPGO, "Do not use PGO."},
    EnumVal<PGOKind>{"pgo-sample-use-pipeline", SampleUse,
                     "Use sampled profile to guide PGO."},
};

inline constexpr OptionInfo<PGOKind> PGOKindFlag{
    "pgo-kind", "The kind of profile guided optimization",
    ValuesRef<PGOKind>(PGOKindVals), Hidden, Init{NoPGO}};

// New-PM-only options consumed by NewPMDriver.cpp (a separate translation
// unit within this same tool-local library); their parsed values are threaded
// into compileModuleWithNewPM() as explicit parameters, the same way
// PrintPipelinePasses is threaded through below.
inline constexpr OptionInfo<bool> DebugPassManager{
    "debug-pass-manager", "Print pass management debugging information",
    Hidden};

inline constexpr EnumVal<RegAllocType> RegAllocNPMVals[] = {
    EnumVal<RegAllocType>{"default", RegAllocType::Default,
                          "Default register allocator"},
    EnumVal<RegAllocType>{"pbqp", RegAllocType::PBQP,
                          "PBQP register allocator"},
    EnumVal<RegAllocType>{"fast", RegAllocType::Fast,
                          "Fast register allocator"},
    EnumVal<RegAllocType>{"basic", RegAllocType::Basic,
                          "Basic register allocator"},
    EnumVal<RegAllocType>{"greedy", RegAllocType::Greedy,
                          "Greedy register allocator"},
};

inline constexpr OptionInfo<RegAllocType> RegAllocNPM{
    "regalloc-npm", "Register allocator to use for new pass manager",
    ValuesRef<RegAllocType>(RegAllocNPMVals), Hidden,
    Init{RegAllocType::Unset}};

// All tool-local options for llc, collected into a single registry. Library
// options (CodeGen, MC, Passes, target backends, ...) are registered
// separately via RegisterAllLLVMOptions(); llc links every target and the
// AllOptions library (see llvm/tools/llc/CMakeLists.txt), so that single call
// covers everything opt registers piecemeal in configureOptRegistries().
inline constexpr OptionsRegistry<
    &InputFilename, &InstPrinterOptions, &InputLanguage, &OutputFilename,
    &SplitDwarfOutputFile, &TimeCompilations, &TimeTrace, &TimeTraceGranularity,
    &TimeTraceFile, &BinutilsVersion, &PreserveComments, &OptLevel,
    &TargetTriple, &SplitDwarfFile, &NoVerify, &VerifyEach,
    &DisableSimplifyLibCalls, &ShowMCEncoding, &OutputAsmVariant,
    &DwarfDirectory, &AsmVerbose, &CompileTwice, &DiscardValueNames,
    &PrintMIR2VecVocab, &PrintMIR2Vec, &IncludeDirs, &RemarksWithHotness,
    &RemarksHotnessThreshold, &RemarksFilename, &RemarksPasses, &RemarksFormat,
    &PassPlugins, &EnableNewPassManager, &PassPipeline, &PassPipeline2,
    &RunPass, &PGOKindFlag, &DebugPassManager, &RegAllocNPM>
    LLCToolReg;

using LLCOptsView = decltype(LLCToolReg)::ParsedOptionsT;

// Compose the tool registry and every LLVM library registry into a single
// parse.
static void configureLLCRegistries(clv2::OptionParser &P) {
  P.add<&LLCToolReg>();
  RegisterAllLLVMOptions(P);
}

// Function to set PGO options on TargetMachine based on command line flags.
static void setPGOOptions(TargetMachine &TM, const LLCOptsView &Opts) {
  std::optional<PGOOptions> PGOOpt;

  switch (Opts.get<&PGOKindFlag>()) {
  case SampleUse:
    // Use default values for other PGOOptions parameters. This parameter
    // is used to test that PGO data is preserved at -O0.
    PGOOpt = PGOOptions("", "", "", "", PGOOptions::SampleUse,
                        PGOOptions::NoCSAction);
    break;
  case NoPGO:
    PGOOpt = std::nullopt;
    break;
  }

  if (PGOOpt)
    TM.setPGOOption(PGOOpt);
}

static int compileModule(char **argv, SmallVectorImpl<PassPlugin> &,
                         LLVMContext &Context, std::string &OutputFile,
                         const clv2::OptionsContext &OptsCtx,
                         const LLCOptsView &Opts,
                         const std::vector<std::string> &RunPassNames);

[[noreturn]] static void reportError(Twine Msg, StringRef Filename = "") {
  SmallString<256> Prefix;
  if (!Filename.empty()) {
    if (Filename == "-")
      Filename = "<stdin>";
    ("'" + Twine(Filename) + "': ").toStringRef(Prefix);
  }
  WithColor::error(errs(), "llc") << Prefix << Msg << "\n";
  exit(1);
}

[[noreturn]] static void reportError(Error Err, StringRef Filename) {
  assert(Err);
  handleAllErrors(createFileError(Filename, std::move(Err)),
                  [&](const ErrorInfoBase &EI) { reportError(EI.message()); });
  llvm_unreachable("reportError() should not return");
}

static std::unique_ptr<ToolOutputFile>
GetOutputStream(Triple::OSType OS, const clv2::OptionsContext &OptsCtx,
                const LLCOptsView &Opts, std::string &OutputFile) {
  // If we don't yet have an output filename, make one.
  if (OutputFile.empty()) {
    const std::string &InputFile = Opts.get<&InputFilename>();
    if (InputFile == "-")
      OutputFile = "-";
    else {
      // If InputFile ends in .bc or .ll, remove it.
      StringRef IFN = InputFile;
      if (IFN.ends_with(".bc") || IFN.ends_with(".ll"))
        OutputFile = std::string(IFN.drop_back(3));
      else if (IFN.ends_with(".mir"))
        OutputFile = std::string(IFN.drop_back(4));
      else
        OutputFile = std::string(IFN);

      switch (codegen::getFileType(OptsCtx)) {
      case CodeGenFileType::AssemblyFile:
        OutputFile += ".s";
        break;
      case CodeGenFileType::ObjectFile:
        if (OS == Triple::Win32)
          OutputFile += ".obj";
        else
          OutputFile += ".o";
        break;
      case CodeGenFileType::Null:
        OutputFile = "-";
        break;
      }
    }
  }

  // Decide if we need "binary" output.
  bool Binary = false;
  switch (codegen::getFileType(OptsCtx)) {
  case CodeGenFileType::AssemblyFile:
    break;
  case CodeGenFileType::ObjectFile:
  case CodeGenFileType::Null:
    Binary = true;
    break;
  }

  // Open the file.
  std::error_code EC;
  sys::fs::OpenFlags OpenFlags = sys::fs::OF_None;
  if (!Binary)
    OpenFlags |= sys::fs::OF_TextWithCRLF;
  auto FDOut = std::make_unique<ToolOutputFile>(OutputFile, EC, OpenFlags);
  if (EC)
    reportError(EC.message());
  return FDOut;
}

// Entry point for the llc compiler.
//
extern "C" int llcMain(int argc, char **argv) {
  // Enable debug stream buffering.
  EnableDebugBuffering = true;

  // Initialize targets first, so that --version shows registered targets.
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmPrinters();
  InitializeAllAsmParsers();

  // Initialize codegen and IR passes used by llc so that the -print-after,
  // -print-before, and -stop-after options work.
  PassRegistry *Registry = PassRegistry::getPassRegistry();
  initializeCore(*Registry);
  initializeCodeGen(*Registry);
  initializeLoopStrengthReducePass(*Registry);
  initializePostInlineEntryExitInstrumenterPass(*Registry);
  initializeUnreachableBlockElimLegacyPassPass(*Registry);
  initializeConstantHoistingLegacyPassPass(*Registry);
  initializeScalarOpts(*Registry);
  initializeIPO(*Registry);
  initializeVectorization(*Registry);
  initializeScalarizeMaskedMemIntrinLegacyPassPass(*Registry);
  initializeTransformUtils(*Registry);

  // Initialize debugging passes.
  initializeScavengerTestPass(*Registry);

  // Pre-load pass plugins so that any options they register are available
  // before the command line is parsed. -load-pass-plugin remains a normal
  // registered option (below, in LLCToolReg) purely so --help documents it
  // and so clv2 doesn't reject it as unknown; the actual loading has to
  // happen here, before parsing (mirrors llvm/tools/opt/optdriver.cpp).
  SmallVector<PassPlugin, 1> PluginList;
  for (int I = 1; I < argc; ++I) {
    StringRef Arg(argv[I]);
    StringRef PluginPath;
    if (Arg.starts_with("--load-pass-plugin="))
      PluginPath = Arg.substr(strlen("--load-pass-plugin="));
    else if (Arg.starts_with("-load-pass-plugin="))
      PluginPath = Arg.substr(strlen("-load-pass-plugin="));
    else if ((Arg == "--load-pass-plugin" || Arg == "-load-pass-plugin") &&
             I + 1 < argc)
      PluginPath = argv[++I];
    if (!PluginPath.empty()) {
      auto Plugin = PassPlugin::Load(PluginPath.str());
      if (!Plugin)
        reportFatalUsageError(Plugin.takeError());
      PluginList.emplace_back(Plugin.get());
    }
  }

  // Register the Target and CPU printer for --version.
  cl::AddExtraVersionPrinter(sys::printDefaultTargetAndDetectedCPU);
  // Register the target printer for --version.
  cl::AddExtraVersionPrinter(TargetRegistry::printRegisteredTargetsForVersion);

  clv2::OptionParser P;
  configureLLCRegistries(P);
  auto OptsCtxOwner =
      P.parse(argc, argv, "llvm system compiler\n", /*Errs=*/nullptr);
  const auto &OptsCtx = *OptsCtxOwner;
  const auto *Opts = OptsCtx.getViewPtr<&LLCToolReg>();

  // Split each (comma-separated) -run-pass occurrence into individual pass
  // names. Replaces the old location-based RunPassOption trick with a plain
  // local, threaded explicitly into compileModule(); -run-pass may be
  // specified more than once, so RunPass is a ListOptionInfo and every
  // occurrence's value must be visited, not just the last one.
  std::vector<std::string> RunPassNames;
  for (const std::string &RunPassVal : Opts->get<&RunPass>()) {
    if (RunPassVal.empty())
      continue;
    SmallVector<StringRef, 8> PassNames;
    StringRef(RunPassVal).split(PassNames, ',', -1, false);
    for (auto PassName : PassNames)
      RunPassNames.push_back(std::string(PassName));
  }

  if (!Opts->get<&PassPipeline>().empty() && !RunPassNames.empty()) {
    errs() << "The `llc -run-pass=...` syntax for the new pass manager is "
              "not supported, please use `llc -passes=<pipeline>` (or the `-p` "
              "alias for a more concise version).\n";
    return 1;
  }

  bool DoTimeTrace = Opts->get<&TimeTrace>();
  if (DoTimeTrace)
    timeTraceProfilerInitialize(Opts->get<&TimeTraceGranularity>(), argv[0]);

  // Seeded from the -o option, then resolved by GetOutputStream() below if
  // empty; captured by reference (not read from a global) so the scope-exit
  // time-trace writer sees the same resolved filename.
  std::string OutputFile = Opts->get<&OutputFilename>();
  llvm::scope_exit TimeTraceScopeExit([&]() {
    if (DoTimeTrace) {
      if (auto E =
              timeTraceProfilerWrite(Opts->get<&TimeTraceFile>(), OutputFile)) {
        handleAllErrors(std::move(E), [&](const StringError &SE) {
          errs() << SE.getMessage() << "\n";
        });
        return;
      }
      timeTraceProfilerCleanup();
    }
  });

  LLVMContext Context(OptsCtx);
  Context.setDiscardValueNames(Opts->get<&DiscardValueNames>());

  // Set a diagnostic handler that doesn't exit on the first error
  Context.setDiagnosticHandler(std::make_unique<LLCDiagnosticHandler>());

  // Parse the optional hotness threshold ("N" or "auto"); mirrors
  // llvm/tools/opt/optdriver.cpp's handling of the same option.
  std::optional<uint64_t> RemarksHotnessThresholdVal;
  {
    const std::string &Str = Opts->get<&RemarksHotnessThreshold>();
    if (Str.empty())
      RemarksHotnessThresholdVal = 0; // default: no threshold
    else if (Str == "auto")
      RemarksHotnessThresholdVal = std::nullopt;
    else
      RemarksHotnessThresholdVal = (uint64_t)std::stoull(Str);
  }

  Expected<LLVMRemarkFileHandle> RemarksFileOrErr =
      setupLLVMOptimizationRemarks(
          Context, Opts->get<&RemarksFilename>(), Opts->get<&RemarksPasses>(),
          Opts->get<&RemarksFormat>(), Opts->get<&RemarksWithHotness>(),
          RemarksHotnessThresholdVal);
  if (Error E = RemarksFileOrErr.takeError())
    reportError(std::move(E), Opts->get<&RemarksFilename>());
  LLVMRemarkFileHandle RemarksFile = std::move(*RemarksFileOrErr);

  codegen::MaybeEnableStatistics(OptsCtx);

  const std::string &InputLang = Opts->get<&InputLanguage>();
  if (InputLang != "" && InputLang != "ir" && InputLang != "mir")
    reportError("input language must be '', 'IR' or 'MIR'");

  // Compile the module TimeCompilations times to give better compile time
  // metrics.
  for (unsigned I = Opts->get<&TimeCompilations>(); I; --I)
    if (int RetVal = compileModule(argv, PluginList, Context, OutputFile,
                                   OptsCtx, *Opts, RunPassNames))
      return RetVal;

  if (RemarksFile)
    RemarksFile->keep();

  return codegen::MaybeSaveStatistics(OutputFile, "llc", OptsCtx);
}

static bool addPass(PassManagerBase &PM, const char *argv0, StringRef PassName,
                    TargetPassConfig &TPC) {
  if (PassName == "none")
    return false;

  const PassRegistry *PR = PassRegistry::getPassRegistry();
  const PassInfo *PI = PR->getPassInfo(PassName);
  if (!PI) {
    WithColor::error(errs(), argv0)
        << "run-pass " << PassName << " is not registered.\n";
    return true;
  }

  Pass *P;
  if (PI->getNormalCtor())
    P = PI->getNormalCtor()();
  else {
    WithColor::error(errs(), argv0)
        << "cannot create pass: " << PI->getPassName() << "\n";
    return true;
  }
  // Passes created by name via the zero-argument factory above have no way
  // to see the OptionsContext at construction time; install it explicitly,
  // mirroring TargetPassConfig::addPass(Pass *), so passes that consult
  // getOptionsContext() at run time (e.g. RegBankSelectLegacy) see the
  // command line options when reached via -run-pass=.
  P->setOptionsContext(TPC.getTM<TargetMachine>().getOptionsContext());
  std::string Banner = std::string("After ") + std::string(P->getPassName());
  TPC.addMachinePrePasses();
  PM.add(P);
  TPC.addMachinePostPasses(Banner);

  return false;
}

static int compileModule(char **argv, SmallVectorImpl<PassPlugin> &PluginList,
                         LLVMContext &Context, std::string &OutputFile,
                         const clv2::OptionsContext &OptsCtx,
                         const LLCOptsView &Opts,
                         const std::vector<std::string> &RunPassNames) {
  // Load the module to be compiled...
  SMDiagnostic Err;
  std::unique_ptr<Module> M;
  std::unique_ptr<MIRParser> MIR;
  Triple TheTriple;
  std::string CPUStr = codegen::getCPUStr(OptsCtx);
  std::string TuneCPUStr = codegen::getTuneCPUStr(OptsCtx);
  std::string FeaturesStr = codegen::getFeaturesStr(OptsCtx);

  // Set attributes on functions as loaded from MIR from command line arguments.
  auto setMIRFunctionAttributes = [&CPUStr, &TuneCPUStr,
                                   &FeaturesStr](Function &F) {
    codegen::setFunctionAttributes(F, CPUStr, FeaturesStr, TuneCPUStr);
  };

  // NOTE: OptLevel is a std::string in clv2 (see its declaration above); take
  // the first character, matching lli.cpp/lto.cpp's identical workaround.
  std::string OLevelStr = Opts.get<&OptLevel>();
  char OLevelChar = OLevelStr.empty() ? '2' : OLevelStr[0];
  CodeGenOptLevel OLvl;
  if (auto Level = CodeGenOpt::parseLevel(OLevelChar)) {
    OLvl = *Level;
  } else {
    WithColor::error(errs(), argv[0]) << "invalid optimization level.\n";
    return 1;
  }

  // Parse 'none' or '$major.$minor'. Disallow -binutils-version=0 because we
  // use that to indicate the MC default.
  const std::string &BinutilsVersionStr = Opts.get<&BinutilsVersion>();
  if (!BinutilsVersionStr.empty() && BinutilsVersionStr != "none") {
    StringRef V = BinutilsVersionStr;
    unsigned Num;
    if (V.consumeInteger(10, Num) || Num == 0 ||
        !(V.empty() ||
          (V.consume_front(".") && !V.consumeInteger(10, Num) && V.empty()))) {
      WithColor::error(errs(), argv[0])
          << "invalid -binutils-version, accepting 'none' or major.minor\n";
      return 1;
    }
  }
  const std::string &InputFile = Opts.get<&InputFilename>();
  const std::string &InputLang = Opts.get<&InputLanguage>();
  TargetOptions Options;
  auto InitializeOptions = [&](const Triple &TheTriple) {
    Options = codegen::InitTargetOptionsFromCodeGenFlags(TheTriple, OptsCtx);

    if (Options.XCOFFReadOnlyPointers) {
      if (!TheTriple.isOSAIX())
        reportError("-mxcoff-roptr option is only supported on AIX", InputFile);

      // Since the storage mapping class is specified per csect,
      // without using data sections, it is less effective to use read-only
      // pointers. Using read-only pointers may cause other RO variables in the
      // same csect to become RW when the linker acts upon `-bforceimprw`;
      // therefore, we require that separate data sections are used in the
      // presence of ReadOnlyPointers. We respect the setting of data-sections
      // since we have not found reasons to do otherwise that overcome the user
      // surprise of not respecting the setting.
      if (!Options.DataSections)
        reportError("-mxcoff-roptr option must be used with -data-sections",
                    InputFile);
    }

    Options.MCOptions.BinutilsVersion =
        MCTargetOptions::parseBinutilsVersion(BinutilsVersionStr);
    Options.MCOptions.ShowMCEncoding = Opts.get<&ShowMCEncoding>();
    Options.MCOptions.AsmVerbose = Opts.get<&AsmVerbose>();
    Options.MCOptions.PreserveAsmComments = Opts.get<&PreserveComments>();
    if (Opts.specified<&OutputAsmVariant>())
      Options.MCOptions.OutputAsmVariant = Opts.get<&OutputAsmVariant>();
    Options.MCOptions.IASSearchPaths = Opts.get<&IncludeDirs>();
    Options.MCOptions.InstPrinterOptions = Opts.get<&InstPrinterOptions>();
    Options.MCOptions.SplitDwarfFile = Opts.get<&SplitDwarfFile>();
    if (Opts.specified<&DwarfDirectory>()) {
      Options.MCOptions.MCUseDwarfDirectory =
          Opts.get<&DwarfDirectory>() ? MCTargetOptions::EnableDwarfDirectory
                                      : MCTargetOptions::DisableDwarfDirectory;
    } else {
      // -dwarf-directory is not set explicitly. Some assemblers
      // (e.g. GNU as or ptxas) do not support `.file directory'
      // syntax prior to DWARFv5. Let the target decide the default
      // value.
      Options.MCOptions.MCUseDwarfDirectory =
          MCTargetOptions::DefaultDwarfDirectory;
    }
  };

  std::optional<Reloc::Model> RM = codegen::getExplicitRelocModel(OptsCtx);
  std::optional<CodeModel::Model> CM = codegen::getExplicitCodeModel(OptsCtx);

  const Target *TheTarget = nullptr;
  std::unique_ptr<TargetMachine> Target;

  // If user just wants to list available options, skip module loading
  auto MAttrs = codegen::getMAttrs(OptsCtx);
  bool SkipModule =
      CPUStr == "help" || TuneCPUStr == "help" || is_contained(MAttrs, "help");
  const std::string &TargetTripleStr = Opts.get<&TargetTriple>();
  if (SkipModule) {
    if (!TargetTripleStr.empty())
      TheTriple = Triple(Triple::normalize(TargetTripleStr));
    else
      TheTriple = Triple(sys::getDefaultTargetTriple());

    // Get the target specific parser.
    std::string Error;
    TheTarget = TargetRegistry::lookupTarget(codegen::getMArch(OptsCtx),
                                             TheTriple, Error);
    if (!TheTarget) {
      WithColor::error(errs(), argv[0]) << Error << "\n";
      return 1;
    }

    InitializeOptions(TheTriple);
    // Pass "help" as CPU for -mtune=help
    std::string SkipModuleCPU = (TuneCPUStr == "help" ? "help" : CPUStr);
    // Create the target machine just to print the help info. Use unique_ptr
    // to avoid a memory leak.
    Target = std::unique_ptr<TargetMachine>(TheTarget->createTargetMachine(
        TheTriple, SkipModuleCPU, FeaturesStr, Options, RM, CM, OLvl));
    if (!Target) {
      WithColor::error(errs(), argv[0])
          << "could not allocate target machine\n";
      return 1;
    }
    Target->setOptionsContext(OptsCtx);

    // If we don't have a module then just exit now. We do this down
    // here since the CPU/Feature help is underneath the target machine
    // creation.
    return 0;
  }

  auto SetDataLayout = [&](StringRef DataLayoutTargetTriple,
                           StringRef OldDLStr) -> std::optional<std::string> {
    // If we are supposed to override the target triple, do so now.
    std::string IRTargetTriple = DataLayoutTargetTriple.str();
    if (!TargetTripleStr.empty())
      IRTargetTriple = Triple::normalize(TargetTripleStr);
    TheTriple = Triple(IRTargetTriple);
    if (TheTriple.getTriple().empty())
      TheTriple.setTriple(sys::getDefaultTargetTriple());

    std::string Error;
    TheTarget = TargetRegistry::lookupTarget(codegen::getMArch(OptsCtx),
                                             TheTriple, Error);
    if (!TheTarget) {
      WithColor::error(errs(), argv[0]) << Error << "\n";
      exit(1);
    }

    InitializeOptions(TheTriple);
    Target = std::unique_ptr<TargetMachine>(TheTarget->createTargetMachine(
        TheTriple, CPUStr, FeaturesStr, Options, RM, CM, OLvl));
    if (!Target) {
      WithColor::error(errs(), argv[0])
          << "could not allocate target machine\n";
      exit(1);
    }
    Target->setOptionsContext(OptsCtx);

    // Set PGO options based on command line flags
    setPGOOptions(*Target, Opts);

    return Target->createDataLayout().getStringRepresentation();
  };
  if (InputLang == "mir" ||
      (InputLang == "" && StringRef(InputFile).ends_with(".mir"))) {
    MIR = createMIRParserFromFile(InputFile, Err, Context,
                                  setMIRFunctionAttributes);
    if (MIR)
      M = MIR->parseIRModule(SetDataLayout);
  } else {
    M = parseIRFile(InputFile, Err, Context, ParserCallbacks(SetDataLayout));
  }
  if (!M) {
    Err.print(argv[0], WithColor::error(errs(), argv[0]));
    return 1;
  }

  M->setTargetTriple(TheTriple);

  std::optional<CodeModel::Model> CM_IR = M->getCodeModel();
  if (!CM && CM_IR)
    Target->setCodeModel(*CM_IR);
  if (std::optional<uint64_t> LDT =
          codegen::getExplicitLargeDataThreshold(OptsCtx))
    Target->setLargeDataThreshold(*LDT);

  // Figure out where we are going to send the output.
  std::unique_ptr<ToolOutputFile> Out =
      GetOutputStream(TheTriple.getOS(), OptsCtx, Opts, OutputFile);
  if (!Out)
    return 1;

  // Ensure the filename is passed down to CodeViewDebug.
  Target->Options.ObjectFilenameForDebug = Out->outputFilename();

  // Return a copy of the output filename via the output param
  OutputFile = Out->outputFilename();

  // Tell target that this tool is not necessarily used with argument ABI
  // compliance (i.e. narrow integer argument extensions).
  Target->Options.VerifyArgABICompliance = 0;

  std::unique_ptr<ToolOutputFile> DwoOut;
  const std::string &SplitDwarfOutputFileStr =
      Opts.get<&SplitDwarfOutputFile>();
  if (!SplitDwarfOutputFileStr.empty()) {
    std::error_code EC;
    DwoOut = std::make_unique<ToolOutputFile>(SplitDwarfOutputFileStr, EC,
                                              sys::fs::OF_None);
    if (EC)
      reportError(EC.message(), SplitDwarfOutputFileStr);
  }

  // Add an appropriate TargetLibraryInfo pass for the module's triple.
  TargetLibraryInfoImpl TLII(M->getTargetTriple(), Target->Options.VecLib);

  // The -disable-simplify-libcalls flag actually disables all builtin optzns.
  if (Opts.get<&DisableSimplifyLibCalls>())
    TLII.disableAllFunctions();

  bool NoVerifyVal = Opts.get<&NoVerify>();

  // Verify module immediately to catch problems before doInitialization() is
  // called on any passes.
  if (!NoVerifyVal && verifyModule(*M, &errs()))
    reportError("input module cannot be verified", InputFile);

  // Override function attributes based on CPUStr, TuneCPUStr, FeaturesStr, and
  // command line flags.
  codegen::setFunctionAttributes(*M, CPUStr, FeaturesStr, TuneCPUStr);

  for (auto &Plugin : PluginList) {
    CodeGenFileType CGFT = codegen::getFileType(OptsCtx);
    if (Plugin.invokePreCodeGenCallback(*M, *Target, CGFT, Out->os())) {
      // TODO: Deduplicate code with below and the NewPMDriver.
      if (Context.getDiagHandlerPtr()->HasErrors)
        exit(1);
      Out->keep();
      return 0;
    }
  }

  if (mc::getExplicitRelaxAll(OptsCtx) &&
      codegen::getFileType(OptsCtx) != CodeGenFileType::ObjectFile)
    WithColor::warning(errs(), argv[0])
        << ": warning: ignoring -mc-relax-all because filetype != obj";

  VerifierKind VK = VerifierKind::InputOutput;
  if (NoVerifyVal)
    VK = VerifierKind::None;
  else if (Opts.get<&VerifyEach>())
    VK = VerifierKind::EachPass;

  // Use the NewPM if the user specifies -passes (NewPM specific), specifically
  // requests the NewPM with -enable-new-pm, or the target defaults to the
  // NewPM, the user has not explicitly disabled the NewPM with
  // -enable-new-pm=false, and the user has not specified -run-pass.
  bool NewPMSpecified = Opts.specified<&EnableNewPassManager>();
  bool NewPMVal = Opts.get<&EnableNewPassManager>();
  const std::string &PassPipelineStr = Opts.get<&PassPipeline>();
  if (!PassPipelineStr.empty() || (NewPMSpecified && NewPMVal) ||
      (Target->shouldDefaultToNewPM() && !(NewPMSpecified && !NewPMVal) &&
       RunPassNames.empty())) {
    // Print string describing the pipeline if requested.
    // PAS_PrintPipelinePasses is ValueOptional: "specified with no value"
    // collapses to a non-empty sentinel ("text") so NewPMDriver.cpp's
    // `!PrintPipelinePasses.empty()` check (its way of saying "was requested")
    // stays true; this mirrors the specified<>()+get<>() pair opt's own
    // NewPMDriver.cpp uses for the same option
    // (llvm/tools/opt/NewPMDriver.cpp).
    StringRef PrintPipelinePasses;
    if (auto *PassesOpts = clv2::getView<&clv2::PassesOptsReg>(OptsCtx)) {
      if (PassesOpts->specified<&clv2::PAS_PrintPipelinePasses>()) {
        const std::string &Val =
            PassesOpts->get<&clv2::PAS_PrintPipelinePasses>();
        PrintPipelinePasses = Val.empty() ? StringRef("text") : StringRef(Val);
      }
    }
    return compileModuleWithNewPM(
        argv[0], std::move(M), std::move(MIR), std::move(Target),
        std::move(Out), std::move(DwoOut), Context, TLII, VK, PassPipelineStr,
        PluginList, codegen::getFileType(OptsCtx), PrintPipelinePasses,
        Opts.get<&DebugPassManager>(), Opts.get<&RegAllocNPM>());
  }

  // Build up all of the passes that we want to do to the module.
  legacy::PassManager PM;
  PM.setOptionsContext(OptsCtx);
  PM.add(new TargetLibraryInfoWrapperPass(TLII));
  PM.add(new RuntimeLibraryInfoWrapper(Target->Options.ExceptionModel,
                                       Options.MCOptions.ABIName,
                                       Target->Options.VecLib));

  {
    raw_pwrite_stream *OS = &Out->os();

    // Manually do the buffering rather than using buffer_ostream,
    // so we can memcmp the contents in CompileTwice mode
    SmallVector<char, 0> Buffer;
    std::unique_ptr<raw_svector_ostream> BOS;
    bool CompileTwiceVal = Opts.get<&CompileTwice>();
    if ((codegen::getFileType(OptsCtx) != CodeGenFileType::AssemblyFile &&
         !Out->os().supportsSeeking()) ||
        CompileTwiceVal) {
      BOS = std::make_unique<raw_svector_ostream>(Buffer);
      OS = BOS.get();
    }

    const char *argv0 = argv[0];
    MachineModuleInfoWrapperPass *MMIWP =
        new MachineModuleInfoWrapperPass(Target.get());

    // Set a temporary diagnostic handler. This is used before
    // MachineModuleInfoWrapperPass::doInitialization for features like -M.
    bool HasMCErrors = false;
    MCContext &MCCtx = MMIWP->getMMI().getContext();
    MCCtx.setDiagnosticHandler([&](const SMDiagnostic &SMD, bool IsInlineAsm,
                                   const SourceMgr &SrcMgr,
                                   std::vector<const MDNode *> &LocInfos) {
      WithColor::error(errs(), argv0) << SMD.getMessage() << '\n';
      HasMCErrors = true;
    });

    // Construct a custom pass pipeline that starts after instruction
    // selection.
    if (!RunPassNames.empty()) {
      if (!MIR) {
        WithColor::error(errs(), argv[0])
            << "run-pass is for .mir file only.\n";
        delete MMIWP;
        return 1;
      }
      TargetPassConfig *PTPC = Target->createPassConfig(PM);
      TargetPassConfig &TPC = *PTPC;
      if (TargetPassConfig::hasLimitedCodeGenPipeline(OptsCtx)) {
        WithColor::error(errs(), argv[0])
            << "run-pass cannot be used with "
            << TargetPassConfig::getLimitedCodeGenPipelineReason(OptsCtx)
            << ".\n";
        delete PTPC;
        delete MMIWP;
        return 1;
      }

      TPC.setDisableVerify(NoVerifyVal);
      PM.add(&TPC);
      PM.add(MMIWP);
      TPC.printAndVerify("");
      for (const std::string &RunPassName : RunPassNames) {
        if (addPass(PM, argv0, RunPassName, TPC))
          return 1;
      }
      TPC.setInitialized();
      PM.add(createPrintMIRPass(*OS));

      // Add MIR2Vec vocabulary printer if requested
      if (Opts.get<&PrintMIR2VecVocab>()) {
        PM.add(createMIR2VecVocabPrinterLegacyPass(errs()));
      }

      // Add MIR2Vec printer if requested
      if (Opts.get<&PrintMIR2Vec>()) {
        PM.add(createMIR2VecPrinterLegacyPass(errs()));
      }

      PM.add(createFreeMachineFunctionPass());
    } else {
      if (Target->addPassesToEmitFile(PM, *OS, DwoOut ? &DwoOut->os() : nullptr,
                                      codegen::getFileType(OptsCtx),
                                      NoVerifyVal, MMIWP)) {
        if (!HasMCErrors)
          reportError("target does not support generation of this file type");
      }

      // Add MIR2Vec vocabulary printer if requested
      if (Opts.get<&PrintMIR2VecVocab>()) {
        PM.add(createMIR2VecVocabPrinterLegacyPass(errs()));
      }

      // Add MIR2Vec printer if requested
      if (Opts.get<&PrintMIR2Vec>()) {
        PM.add(createMIR2VecPrinterLegacyPass(errs()));
      }
    }

    Target->getObjFileLowering()->Initialize(MMIWP->getMMI().getContext(),
                                             *Target);
    if (MIR) {
      assert(MMIWP && "Forgot to create MMIWP?");
      if (MIR->parseMachineFunctions(*M, MMIWP->getMMI()))
        return 1;
    }

    // Before executing passes, print the final values of the LLVM options.
    // clv2 already printed option values from the parser itself (see
    // llvm/Support/CommandLineCompat.h's deprecated PrintOptionValues() no-op
    // shim), so there is nothing to do here.

    // If requested, run the pass manager over the same module again,
    // to catch any bugs due to persistent state in the passes. Note that
    // opt has the same functionality, so it may be worth abstracting this out
    // in the future.
    SmallVector<char, 0> CompileTwiceBuffer;
    if (CompileTwiceVal) {
      std::unique_ptr<Module> M2(llvm::CloneModule(*M));
      PM.run(*M2);
      CompileTwiceBuffer = Buffer;
      Buffer.clear();
    }

    PM.run(*M);

    if (Context.getDiagHandlerPtr()->HasErrors || HasMCErrors)
      return 1;

    // Compare the two outputs and make sure they're the same
    if (CompileTwiceVal) {
      if (Buffer.size() != CompileTwiceBuffer.size() ||
          (memcmp(Buffer.data(), CompileTwiceBuffer.data(), Buffer.size()) !=
           0)) {
        errs()
            << "Running the pass manager twice changed the output.\n"
               "Writing the result of the second run to the specified output\n"
               "To generate the one-run comparison binary, just run without\n"
               "the compile-twice option\n";
        Out->os() << Buffer;
        Out->keep();
        return 1;
      }
    }

    if (BOS) {
      Out->os() << Buffer;
    }
  }

  // Declare success.
  Out->keep();
  if (DwoOut)
    DwoOut->keep();

  return 0;
}
