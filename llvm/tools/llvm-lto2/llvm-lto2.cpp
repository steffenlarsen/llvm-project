//===-- llvm-lto2: test harness for the resolution-based LTO interface ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This program takes in a list of bitcode files, links them and performs
// link-time optimization according to the provided symbol resolutions using the
// resolution-based LTO interface, and outputs one or more object files.
//
// This program is intended to eventually replace llvm-lto which uses the legacy
// LTO interface.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ScopeExit.h"
#include "llvm/Analysis/AnalysisOptionsOptInfos.h"
#include "llvm/AsmParser/AsmParserOptionsOptInfos.h"
#include "llvm/Bitcode/BitcodeOptionsOptInfos.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/CGData/CGDataOptionsOptInfos.h"
#include "llvm/CodeGen/CodeGenPassOptionsOptInfos.h"
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/CodeGen/CommandFlagsOptInfos.h"
#include "llvm/DTLTO/DTLTO.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/IROptionsOptInfos.h"
#include "llvm/LTO/LTO.h"
#include "llvm/LTO/LTOOptionsOptInfos.h"
#include "llvm/MC/MCOptionsOptInfos.h"
#include "llvm/MC/MCTargetOptionsCommandFlags.h"
#include "llvm/Object/ObjectOptionsOptInfos.h"
#include "llvm/Passes/PassesOptionsOptInfos.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Remarks/RemarksOptionsOptInfos.h"
#include "llvm/Support/Caching.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/PluginLoader.h"
#include "llvm/Support/SupportOptions.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Transforms/AggressiveInstCombine/AggressiveInstCombineOptionsOptInfos.h"
#include "llvm/Transforms/Coroutines/CoroutinesOptionsOptInfos.h"
#include "llvm/Transforms/IPO/IPOOptionsOptInfos.h"
#include "llvm/Transforms/InstCombine/InstCombineOptionsOptInfos.h"
#include "llvm/Transforms/Instrumentation/InstrumentationOptionsOptInfos.h"
#include "llvm/Transforms/ObjCARC/ObjCARCOptionsOptInfos.h"
#include "llvm/Transforms/Scalar/ScalarOptionsOptInfos.h"
#include "llvm/Transforms/Utils/UtilsOptionsOptInfos.h"
#include "llvm/Transforms/Vectorize/VectorizeOptionsOptInfos.h"
#include <atomic>
#include <optional>

using namespace llvm;
using namespace lto;

//===----------------------------------------------------------------------===//
// Option descriptors
//===----------------------------------------------------------------------===//

inline constexpr clv2::OptionInfo<unsigned> OptLevelOpt{
    "O",
    "Optimization level. [-O0, -O1, -O2, or -O3] "
    "(default = '-O2')",
    clv2::PrefixFormat, clv2::Init{2u}};

inline constexpr clv2::OptionInfo<unsigned> CGOptLevelOpt{
    "cg-opt-level", "Codegen optimization level (0, 1, 2 or 3, default = '2')",
    clv2::Init{2u}};

inline constexpr clv2::ListOptionInfo<std::string> InputFilenamesOpt{
    "", "<input bitcode files>", clv2::Positional{}, clv2::OneOrMore};

inline constexpr clv2::OptionInfo<std::string> OutputFilenameOpt{
    "o", "Output filename", clv2::value_desc("filename")};

inline constexpr clv2::OptionInfo<std::string> CacheDirOpt{
    "cache-dir", "Cache Directory", clv2::value_desc("directory")};

inline constexpr clv2::OptionInfo<std::string> OptPipelineOpt{
    "opt-pipeline", "Optimizer Pipeline", clv2::value_desc("pipeline")};

inline constexpr clv2::OptionInfo<std::string> AAPipelineOpt{
    "aa-pipeline", "Alias Analysis Pipeline", clv2::value_desc("aapipeline")};

inline constexpr clv2::OptionInfo<bool> SaveTempsOpt{"save-temps",
                                                     "Save temporary files"};

inline constexpr clv2::ListOptionInfo<std::string> SelectSaveTempsOpt{
    "select-save-temps",
    "Save selected temporary files. Cannot be specified together with "
    "-save-temps",
    clv2::CommaSeparated,
    clv2::value_desc(
        "One, or multiple of: "
        "resolution,preopt,promote,internalize,import,opt,precodegen"
        ",combinedindex")};

constexpr const char *SaveTempsValues[] = {
    "resolution", "preopt", "promote",    "internalize",
    "import",     "opt",    "precodegen", "combinedindex"};

inline constexpr clv2::OptionInfo<bool> ThinLTODistributedIndexesOpt{
    "thinlto-distributed-indexes", "Write out individual index and "
                                   "import files for the "
                                   "distributed backend case"};

inline constexpr clv2::OptionInfo<bool> ThinLTOEmitIndexesOpt{
    "thinlto-emit-indexes", "Write out individual index files via "
                            "InProcessThinLTO"};

inline constexpr clv2::OptionInfo<bool> ThinLTOEmitImportsOpt{
    "thinlto-emit-imports", "Write out individual imports files via "
                            "InProcessThinLTO. Has no effect unless "
                            "specified with -thinlto-emit-indexes or "
                            "-thinlto-distributed-indexes"};

inline constexpr clv2::OptionInfo<std::string> DTLTODistributorOpt{
    "dtlto-distributor",
    "Distributor to use for ThinLTO backend compilations. Specifying "
    "this enables DTLTO."};

inline constexpr clv2::ListOptionInfo<std::string> DTLTODistributorArgsOpt{
    "dtlto-distributor-arg",
    "Arguments to pass to the DTLTO distributor process.", clv2::CommaSeparated,
    clv2::value_desc("arg")};

inline constexpr clv2::OptionInfo<std::string> DTLTOCompilerOpt{
    "dtlto-compiler",
    "Compiler to use for DTLTO ThinLTO backend compilations."};

inline constexpr clv2::ListOptionInfo<std::string> DTLTOCompilerPrependArgsOpt{
    "dtlto-compiler-prepend-arg",
    "Prepend arguments to pass to the remote compiler for backend "
    "compilations.",
    clv2::CommaSeparated, clv2::value_desc("arg")};

inline constexpr clv2::ListOptionInfo<std::string> DTLTOCompilerArgsOpt{
    "dtlto-compiler-arg",
    "Arguments to pass to the remote compiler for backend "
    "compilations.",
    clv2::CommaSeparated, clv2::value_desc("arg")};

inline constexpr clv2::OptionInfo<std::string> ThreadsOpt{"thinlto-threads",
                                                          ""};

inline constexpr clv2::ListOptionInfo<std::string> SymbolResolutionsOpt{
    "r", "Specify a symbol resolution: filename,symbolname,resolution\n"
         "where \"resolution\" is a sequence (which may be empty) of the\n"
         "following characters:\n"
         " p - prevailing: the linker has chosen this definition of the\n"
         "     symbol\n"
         " l - local: the definition of this symbol is unpreemptable at\n"
         "     runtime and is known to be in this linkage unit\n"
         " x - externally visible: the definition of this symbol is\n"
         "     visible outside of the LTO unit\n"
         "A resolution for each symbol must be specified"};

inline constexpr clv2::OptionInfo<std::string> OverrideTripleOpt{
    "override-triple",
    "Replace target triples in input files with this triple"};

inline constexpr clv2::OptionInfo<std::string> DefaultTripleOpt{
    "default-triple",
    "Replace unspecified target triples in input files with this triple"};

inline constexpr clv2::OptionInfo<bool> RemarksWithHotnessOpt{
    "pass-remarks-with-hotness",
    "With PGO, include profile count in optimization remarks", clv2::Hidden};

inline constexpr clv2::OptionInfo<std::string> RemarksHotnessThresholdOpt{
    "pass-remarks-hotness-threshold",
    "Minimum profile count required for an "
    "optimization remark to be output."
    " Use 'auto' to apply the threshold from profile summary.",
    clv2::value_desc("uint or 'auto'"), clv2::Hidden};

inline constexpr clv2::OptionInfo<std::string> RemarksFilenameOpt{
    "pass-remarks-output", "Output filename for pass remarks",
    clv2::value_desc("filename")};

inline constexpr clv2::OptionInfo<std::string> RemarksPassesOpt{
    "pass-remarks-filter",
    "Only record optimization remarks from passes whose "
    "names match the given regular expression",
    clv2::value_desc("regex")};

inline constexpr clv2::OptionInfo<std::string> RemarksFormatOpt{
    "pass-remarks-format",
    "The format used for serializing remarks (default: YAML)",
    clv2::value_desc("format"), clv2::Init{"yaml"}};

inline constexpr clv2::OptionInfo<std::string> SamplePGOFileOpt{
    "lto-sample-profile-file", "Specify a SamplePGO profile file"};

inline constexpr clv2::OptionInfo<std::string> CSPGOFileOpt{
    "lto-cspgo-profile-file", "Specify a context sensitive PGO profile file"};

inline constexpr clv2::OptionInfo<bool> RunCSIRInstrOpt{
    "lto-cspgo-gen", "Run PGO context sensitive IR instrumentation",
    clv2::Hidden};

inline constexpr clv2::OptionInfo<bool> DebugPassManagerOpt{
    "debug-pass-manager", "Print pass management debugging information",
    clv2::Hidden};

inline constexpr clv2::OptionInfo<std::string> StatsFileOpt{
    "stats-file", "Filename to write statistics to"};

inline constexpr clv2::ListOptionInfo<std::string> PassPluginsOpt{
    "load-pass-plugin", "Load passes from plugin library"};

// -load comes from PluginLoader (LLVMSupport), which also performs the load.
// Declaring it here as well put the same CLI name in two registries, where
// only the first added is reachable.

inline constexpr clv2::EnumVal<LTO::LTOKind> UnifiedLTOModeVals[] = {
    {"thin", LTO::LTOK_UnifiedThin, "ThinLTO with Unified LTO enabled"},
    {"full", LTO::LTOK_UnifiedRegular, "Regular LTO with Unified LTO enabled"},
    {"default", LTO::LTOK_Default, "Any LTO mode without Unified LTO"},
};

inline constexpr auto UnifiedLTOModeOpt = clv2::makeEnumOption<LTO::LTOKind>(
    "unified-lto",
    "Set LTO mode with the following options:", UnifiedLTOModeVals,
    clv2::Init{LTO::LTOK_Default}, clv2::value_desc("mode"));

inline constexpr clv2::OptionInfo<bool> EnableFreestandingOpt{
    "lto-freestanding",
    "Enable Freestanding (disable builtins / TLI) during LTO", clv2::Hidden};

inline constexpr clv2::OptionInfo<bool> WholeProgramVisibilityEnabledInLTOOpt{
    "whole-program-visibility-enabled-in-lto",
    "Enable whole program visibility during LTO", clv2::Hidden};

inline constexpr clv2::OptionInfo<bool> ValidateAllVtablesHaveTypeInfosOpt{
    "validate-all-vtables-have-type-infos",
    "Validate that all vtables have type infos in LTO", clv2::Hidden};

inline constexpr clv2::OptionInfo<bool> AllVtablesHaveTypeInfosOpt{
    "all-vtables-have-type-infos", "All vtables have type infos", clv2::Hidden};

// Specifying a symbol here states that it is a library symbol that had a
// definition in bitcode, but was not extracted. Such symbols cannot safely
// be referenced, since they have already lost their opportunity to be defined.
//
// FIXME: Listing all bitcode libfunc symbols here is clunky. A higher-level way
// to indicate which TUs made it into the link might be better, but this would
// require more detailed tracking of the sources of constructs in the IR.
// Alternatively, there may be some other data structure that could hold this
// information.
inline constexpr clv2::ListOptionInfo<std::string> BitcodeLibFuncsOpt{
    "bitcode-libfuncs", "set of unextracted libfuncs implemented in bitcode",
    clv2::Hidden};

inline constexpr clv2::OptionInfo<bool> TimeTraceOpt{"time-trace",
                                                     "Record time trace"};

inline constexpr clv2::OptionInfo<unsigned> TimeTraceGranularityOpt{
    "time-trace-granularity",
    "Minimum time granularity (in microseconds) traced by time profiler",
    clv2::Init{500u}, clv2::Hidden};

inline constexpr clv2::OptionInfo<std::string> TimeTraceFileOpt{
    "time-trace-file", "Specify time trace file destination",
    clv2::value_desc("filename")};

//===----------------------------------------------------------------------===//
// Registries
//===----------------------------------------------------------------------===//

static constexpr clv2::OptionsRegistry<
    &OptLevelOpt, &CGOptLevelOpt, &InputFilenamesOpt, &OutputFilenameOpt,
    &CacheDirOpt, &OptPipelineOpt, &AAPipelineOpt, &SaveTempsOpt,
    &SelectSaveTempsOpt, &ThinLTODistributedIndexesOpt, &ThinLTOEmitIndexesOpt,
    &ThinLTOEmitImportsOpt, &DTLTODistributorOpt, &DTLTODistributorArgsOpt,
    &DTLTOCompilerOpt, &DTLTOCompilerPrependArgsOpt, &DTLTOCompilerArgsOpt,
    &ThreadsOpt, &SymbolResolutionsOpt, &OverrideTripleOpt, &DefaultTripleOpt,
    &RemarksWithHotnessOpt, &RemarksHotnessThresholdOpt, &RemarksFilenameOpt,
    &RemarksPassesOpt, &RemarksFormatOpt, &SamplePGOFileOpt, &CSPGOFileOpt,
    &RunCSIRInstrOpt, &DebugPassManagerOpt, &StatsFileOpt, &PassPluginsOpt,
    &UnifiedLTOModeOpt, &EnableFreestandingOpt,
    &WholeProgramVisibilityEnabledInLTOOpt, &ValidateAllVtablesHaveTypeInfosOpt,
    &AllVtablesHaveTypeInfosOpt, &BitcodeLibFuncsOpt, &TimeTraceOpt,
    &TimeTraceGranularityOpt, &TimeTraceFileOpt>
    LTO2ToolReg;

// Registries parsed by this tool, with their bridge functions.
static void configureLTO2Registries(clv2::OptionParser &P) {
  P.add<&LTO2ToolReg>();
  P.add<&clv2::CGOptsReg>();
  P.add<&clv2::MCOptsReg>();
  P.add<&clv2::SupportOptsReg, support::applySupportOptions>();
  P.add<&clv2::RemarksOptsReg>();
  P.add<&clv2::ObjectOptsReg>();
  P.add<&clv2::AsmParserOptsReg>();
  P.add<&clv2::IROptsReg, ir_opts::applyIROptions>();
  P.add<&clv2::PassesOptsReg>();
  P.add<&clv2::LTOOptsReg>();
  P.add<&clv2::ScalarOptsReg>();
  P.add<&clv2::AnalysisOptsReg>();
  P.add<&clv2::IPOOptsReg>();
  P.add<&clv2::VectorizeOptsReg>();
  P.add<&clv2::TransformUtilsOptsReg>();
  P.add<&clv2::InstrumentationOptsReg>();
  P.add<&clv2::BitcodeOptsReg>();
  P.add<&clv2::InstCombineOptsReg>();
  P.add<&clv2::AggressiveInstCombineOptsReg>();
  P.add<&clv2::CoroutinesOptsReg>();
  P.add<&clv2::ObjCARCOptsReg>();
  P.add<&clv2::CGDataOptsReg>();
  P.add<&clv2::CGPassAsmPrintReg>();
  P.add<&clv2::CGPassCore1Reg>();
  P.add<&clv2::CGPassCore2Reg>();
  P.add<&clv2::CGPassGISelReg>();
  P.add<&clv2::CGPassMachine1Reg>();
  P.add<&clv2::CGPassMachine2Reg>();
  P.add<&clv2::CGPassRegAllocReg>();
  P.add<&clv2::CGPassSched1Reg>();
  P.add<&clv2::CGPassSched2Reg>();
  P.add<&clv2::CGPassSelDAGReg>();
}

static void check(Error E, std::string Msg) {
  if (!E)
    return;
  handleAllErrors(std::move(E), [&](ErrorInfoBase &EIB) {
    errs() << "llvm-lto2: " << Msg << ": " << EIB.message().c_str() << '\n';
  });
  exit(1);
}

template <typename T> static T check(Expected<T> E, std::string Msg) {
  if (E)
    return std::move(*E);
  check(E.takeError(), Msg);
  return T();
}

static void check(std::error_code EC, std::string Msg) {
  check(errorCodeToError(EC), Msg);
}

template <typename T> static T check(ErrorOr<T> E, std::string Msg) {
  if (E)
    return std::move(*E);
  check(E.getError(), Msg);
  return T();
}

static int usage() {
  errs() << "Available subcommands: dump-symtab run print-guid\n";
  return 1;
}

static int run(int argc, char **argv) {
  // Pre-load plugins so that options defined by plugins are visible
  // to the parser.
  for (int I = 1; I < argc; ++I) {
    StringRef Arg(argv[I]);
    StringRef PluginPath;
    bool IsPassPlugin = false;
    if (Arg.starts_with("--load-pass-plugin=")) {
      PluginPath = Arg.substr(strlen("--load-pass-plugin="));
      IsPassPlugin = true;
    } else if (Arg.starts_with("-load-pass-plugin=")) {
      PluginPath = Arg.substr(strlen("-load-pass-plugin="));
      IsPassPlugin = true;
    } else if ((Arg == "--load-pass-plugin" || Arg == "-load-pass-plugin") &&
               I + 1 < argc) {
      PluginPath = argv[++I];
      IsPassPlugin = true;
    } else if (Arg.starts_with("--load="))
      PluginPath = Arg.substr(strlen("--load="));
    else if (Arg.starts_with("-load="))
      PluginPath = Arg.substr(strlen("-load="));
    else if ((Arg == "--load" || Arg == "-load") && I + 1 < argc)
      PluginPath = argv[++I];
    if (!PluginPath.empty()) {
      if (IsPassPlugin) {
        auto Plugin = PassPlugin::Load(PluginPath.str());
        if (!Plugin)
          reportFatalUsageError(Plugin.takeError());
        (void)*Plugin;
      } else {
        std::string Error;
        if (sys::DynamicLibrary::LoadLibraryPermanently(
                PluginPath.str().c_str(), &Error)) {
          errs() << "Error opening '" << PluginPath << "': " << Error
                 << "\n  -load request ignored.\n";
        }
      }
    }
  }

  auto OptsCtxOwner = [&] {
    clv2::OptionParser P;
    configureLTO2Registries(P);
    P.enableGlobalDynamicEntries();
    P.hideUnrelatedOptions({&clv2::ColorOptionsCategory});
    return P.parse(argc, argv, "Resolution-based LTO test harness",
                   /*Errs=*/nullptr);
  }();
  const auto &OptsCtx = *OptsCtxOwner;
  const auto *Opts = OptsCtx.getViewPtr<&LTO2ToolReg>();

  unsigned OptLevel = Opts->get<&OptLevelOpt>();
  unsigned CGOptLevel = Opts->get<&CGOptLevelOpt>();
  auto InputFilenames = Opts->get<&InputFilenamesOpt>();
  auto OutputFilename = Opts->get<&OutputFilenameOpt>();
  auto CacheDir = Opts->get<&CacheDirOpt>();
  auto OptPipeline = Opts->get<&OptPipelineOpt>();
  auto AAPipeline = Opts->get<&AAPipelineOpt>();
  bool SaveTemps = Opts->get<&SaveTempsOpt>();
  auto SelectSaveTemps = Opts->get<&SelectSaveTempsOpt>();
  bool ThinLTODistributedIndexes = Opts->get<&ThinLTODistributedIndexesOpt>();
  bool ThinLTOEmitIndexes = Opts->get<&ThinLTOEmitIndexesOpt>();
  bool ThinLTOEmitImports = Opts->get<&ThinLTOEmitImportsOpt>();
  auto DTLTODistributor = Opts->get<&DTLTODistributorOpt>();
  auto DTLTODistributorArgs = Opts->get<&DTLTODistributorArgsOpt>();
  auto DTLTOCompiler = Opts->get<&DTLTOCompilerOpt>();
  auto DTLTOCompilerPrependArgs = Opts->get<&DTLTOCompilerPrependArgsOpt>();
  auto DTLTOCompilerArgs = Opts->get<&DTLTOCompilerArgsOpt>();
  auto Threads = Opts->get<&ThreadsOpt>();
  auto SymbolResolutions = Opts->get<&SymbolResolutionsOpt>();
  auto OverrideTriple = Opts->get<&OverrideTripleOpt>();
  auto DefaultTriple = Opts->get<&DefaultTripleOpt>();
  bool RemarksWithHotness = Opts->get<&RemarksWithHotnessOpt>();
  auto RemarksFilename = Opts->get<&RemarksFilenameOpt>();
  auto RemarksPasses = Opts->get<&RemarksPassesOpt>();
  auto RemarksFormat = Opts->get<&RemarksFormatOpt>();
  auto SamplePGOFile = Opts->get<&SamplePGOFileOpt>();
  auto CSPGOFile = Opts->get<&CSPGOFileOpt>();
  bool RunCSIRInstr = Opts->get<&RunCSIRInstrOpt>();
  bool DebugPassManager = Opts->get<&DebugPassManagerOpt>();
  auto StatsFile = Opts->get<&StatsFileOpt>();
  auto PassPlugins = Opts->get<&PassPluginsOpt>();
  auto UnifiedLTOMode = Opts->get<&UnifiedLTOModeOpt>();
  bool EnableFreestanding = Opts->get<&EnableFreestandingOpt>();
  bool WholeProgramVisibilityEnabledInLTO =
      Opts->get<&WholeProgramVisibilityEnabledInLTOOpt>();
  unsigned WholeProgramVisibilityOccurrences =
      Opts->occurrences<&WholeProgramVisibilityEnabledInLTOOpt>();
  bool ValidateAllVtablesHaveTypeInfos =
      Opts->get<&ValidateAllVtablesHaveTypeInfosOpt>();
  unsigned ValidateAllVtablesOccurrences =
      Opts->occurrences<&ValidateAllVtablesHaveTypeInfosOpt>();
  bool AllVtablesHaveTypeInfos = Opts->get<&AllVtablesHaveTypeInfosOpt>();
  unsigned AllVtablesOccurrences =
      Opts->occurrences<&AllVtablesHaveTypeInfosOpt>();
  bool TimeTrace = Opts->get<&TimeTraceOpt>();
  unsigned TimeTraceGranularity = Opts->get<&TimeTraceGranularityOpt>();
  auto TimeTraceFile = Opts->get<&TimeTraceFileOpt>();

  std::optional<uint64_t> RemarksHotnessThreshold = 0;
  auto ThresholdStr = Opts->get<&RemarksHotnessThresholdOpt>();
  if (!ThresholdStr.empty()) {
    if (ThresholdStr == "auto") {
      RemarksHotnessThreshold = 0;
    } else {
      uint64_t Val;
      if (StringRef(ThresholdStr).getAsInteger(0, Val)) {
        errs() << "error: invalid value '" << ThresholdStr
               << "' for --pass-remarks-hotness-threshold\n";
        return 1;
      }
      RemarksHotnessThreshold = Val;
    }
  }

  if (TimeTrace)
    timeTraceProfilerInitialize(TimeTraceGranularity, argv[0]);
  llvm::scope_exit TimeTraceScopeExit([&]() {
    if (TimeTrace) {
      check(timeTraceProfilerWrite(TimeTraceFile, OutputFilename),
            "timeTraceProfilerWrite failed");
      timeTraceProfilerCleanup();
    }
  });

  // FIXME: Workaround PR30396 which means that a symbol can appear
  // more than once if it is defined in module-level assembly and
  // has a GV declaration. We allow (file, symbol) pairs to have multiple
  // resolutions and apply them in the order observed.
  std::map<std::pair<std::string, std::string>, std::list<SymbolResolution>>
      CommandLineResolutions;
  for (StringRef R : SymbolResolutions) {
    StringRef Rest, FileName, SymbolName;
    std::tie(FileName, Rest) = R.split(',');
    if (Rest.empty()) {
      llvm::errs() << "invalid resolution: " << R << '\n';
      return 1;
    }
    std::tie(SymbolName, Rest) = Rest.split(',');
    SymbolResolution Res;
    for (char C : Rest) {
      if (C == 'p')
        Res.Prevailing = true;
      else if (C == 'l')
        Res.FinalDefinitionInLinkageUnit = true;
      else if (C == 'x')
        Res.VisibleToRegularObj = true;
      else if (C == 'r')
        Res.LinkerRedefined = true;
      else {
        llvm::errs() << "invalid character " << C << " in resolution: " << R
                     << '\n';
        return 1;
      }
    }
    CommandLineResolutions[{std::string(FileName), std::string(SymbolName)}]
        .push_back(Res);
  }

  std::vector<std::unique_ptr<MemoryBuffer>> MBs;

  Config Conf(OptsCtx);
  if (TimeTrace) {
    Conf.TimeTraceEnabled = TimeTrace;
    Conf.TimeTraceGranularity = TimeTraceGranularity;
  }
  Conf.CPU = codegen::getMCPU(OptsCtx);
  Conf.Options = codegen::InitTargetOptionsFromCodeGenFlags(Triple(), OptsCtx);
  Conf.Options.MCOptions.OptsCtx = &OptsCtx;
  Conf.Options.OptsCtx = &OptsCtx;
  Conf.MAttrs = codegen::getMAttrs(OptsCtx);
  if (auto RM = codegen::getExplicitRelocModel(OptsCtx))
    Conf.RelocModel = *RM;
  Conf.CodeModel = codegen::getExplicitCodeModel(OptsCtx);

  Conf.DebugPassManager = DebugPassManager;

  if (SaveTemps && !SelectSaveTemps.empty()) {
    llvm::errs() << "-save-temps cannot be specified with -select-save-temps\n";
    return 1;
  }
  if (SaveTemps || !SelectSaveTemps.empty()) {
    DenseSet<StringRef> SaveTempsArgs;
    for (auto &S : SelectSaveTemps)
      if (is_contained(SaveTempsValues, S))
        SaveTempsArgs.insert(S);
      else {
        llvm::errs() << ("invalid -select-save-temps argument: " + S) << '\n';
        return 1;
      }
    check(Conf.addSaveTemps(OutputFilename + ".", false, SaveTempsArgs),
          "Config::addSaveTemps failed");
  }

  // Optimization remarks.
  Conf.RemarksFilename = RemarksFilename;
  Conf.RemarksPasses = RemarksPasses;
  Conf.RemarksWithHotness = RemarksWithHotness;
  Conf.RemarksHotnessThreshold = RemarksHotnessThreshold;
  Conf.RemarksFormat = RemarksFormat;

  Conf.SampleProfile = SamplePGOFile;
  Conf.CSIRProfile = CSPGOFile;
  Conf.RunCSIRInstr = RunCSIRInstr;

  // Run a custom pipeline, if asked for.
  Conf.OptPipeline = OptPipeline;
  Conf.AAPipeline = AAPipeline;

  Conf.OptLevel = OptLevel;
  Conf.Freestanding = EnableFreestanding;
  llvm::append_range(Conf.PassPluginFilenames, PassPlugins);
  if (CGOptLevel > 3) {
    llvm::errs() << "invalid cg optimization level: " << CGOptLevel << '\n';
    return 1;
  }
  if (auto Level = CodeGenOpt::parseLevel('0' + CGOptLevel)) {
    Conf.CGOptLevel = *Level;
  } else {
    llvm::errs() << "invalid cg optimization level: " << CGOptLevel << '\n';
    return 1;
  }

  if (auto FT = codegen::getExplicitFileType(OptsCtx))
    Conf.CGFileType = *FT;

  Conf.OverrideTriple = OverrideTriple;
  Conf.DefaultTriple = DefaultTriple;
  Conf.StatsFile = StatsFile;
  Conf.PTO.LoopVectorization = Conf.OptLevel > 1;
  Conf.PTO.SLPVectorization = Conf.OptLevel > 1;

  if (WholeProgramVisibilityOccurrences > 0)
    Conf.HasWholeProgramVisibility = WholeProgramVisibilityEnabledInLTO;
  if (ValidateAllVtablesOccurrences > 0)
    Conf.ValidateAllVtablesHaveTypeInfos = ValidateAllVtablesHaveTypeInfos;
  if (AllVtablesOccurrences > 0)
    Conf.AllVtablesHaveTypeInfos = AllVtablesHaveTypeInfos;

  if (ThinLTODistributedIndexes && !DTLTODistributor.empty())
    llvm::errs() << "-thinlto-distributed-indexes cannot be specfied together "
                    "with -dtlto-distributor\n";
  auto DTLTODistributorArgsSV = llvm::to_vector<0>(llvm::map_range(
      DTLTODistributorArgs, [](const std::string &S) { return StringRef(S); }));
  auto DTLTOCompilerPrependArgsSV = llvm::to_vector<0>(
      llvm::map_range(DTLTOCompilerPrependArgs,
                      [](const std::string &S) { return StringRef(S); }));
  auto DTLTOCompilerArgsSV = llvm::to_vector<0>(llvm::map_range(
      DTLTOCompilerArgs, [](const std::string &S) { return StringRef(S); }));

  auto AddStream =
      [&](size_t Task,
          const Twine &ModuleName) -> std::unique_ptr<CachedFileStream> {
    std::string Path = OutputFilename + "." + utostr(Task);

    std::error_code EC;
    auto S = std::make_unique<raw_fd_ostream>(Path, EC, sys::fs::OF_None);
    check(EC, Path);
    return std::make_unique<CachedFileStream>(std::move(S), Path);
  };

  auto AddBuffer = [&](size_t Task, const Twine &ModuleName,
                       std::unique_ptr<MemoryBuffer> MB) {
    auto Stream = AddStream(Task, ModuleName);
    *Stream->OS << MB->getBuffer();
    check(Stream->commit(), "Failed to commit cache");
  };

  ThinBackend Backend;
  if (ThinLTODistributedIndexes)
    Backend = createWriteIndexesThinBackend(llvm::hardware_concurrency(Threads),
                                            /*OldPrefix=*/"",
                                            /*NewPrefix=*/"",
                                            /*NativeObjectPrefix=*/"",
                                            ThinLTOEmitImports,
                                            /*LinkedObjectsFile=*/nullptr,
                                            /*OnWrite=*/{});
  else
    Backend = createInProcessThinBackend(
        llvm::heavyweight_hardware_concurrency(Threads),
        /* OnWrite */ {}, ThinLTOEmitIndexes, ThinLTOEmitImports);

  // Track whether we hit an error; in particular, in the multi-threaded case,
  // we can't exit() early because the rest of the threads wouldn't have had a
  // change to be join-ed, and that would result in a "terminate called without
  // an active exception". Altogether, this results in nondeterministic
  // behavior. Instead, we don't exit in the multi-threaded case, but we make
  // sure to report the error and then at the end (after joining cleanly)
  // exit(1).
  std::atomic<bool> HasErrors{false};
  Conf.DiagHandler = [&](const DiagnosticInfo &DI) {
    DiagnosticPrinterRawOStream DP(errs());
    DI.print(DP);
    errs() << '\n';
    if (DI.getSeverity() == DS_Error)
      HasErrors = true;
  };

  LTO::LTOKind LTOMode = UnifiedLTOMode;

  std::unique_ptr<LTO> Lto;
  if (!DTLTODistributor.empty()) {
    Lto = std::make_unique<DTLTO>(
        std::move(Conf), 1, LTOMode, nullptr, ThinLTOEmitIndexes,
        ThinLTOEmitImports, OutputFilename, DTLTODistributor,
        DTLTODistributorArgsSV, DTLTOCompiler, DTLTOCompilerPrependArgsSV,
        DTLTOCompilerArgsSV, AddBuffer, SaveTemps);
  } else {
    Lto =
        std::make_unique<LTO>(std::move(Conf), std::move(Backend), 1, LTOMode);
  }

  for (std::string F : InputFilenames) {
    std::unique_ptr<MemoryBuffer> MB = check(MemoryBuffer::getFile(F), F);
    std::unique_ptr<InputFile> Input =
        check(InputFile::create(MB->getMemBufferRef(), OptsCtx), F);

    std::vector<SymbolResolution> Res;
    for (const InputFile::Symbol &Sym : Input->symbols()) {
      auto I = CommandLineResolutions.find({F, std::string(Sym.getName())});
      // If it isn't found, look for ".", which would have been added
      // (followed by a hash) when the symbol was promoted during module
      // splitting if it was defined in one part and used in the other.
      // Try looking up the symbol name before the suffix.
      if (I == CommandLineResolutions.end()) {
        auto SplitName = Sym.getName().rsplit(".");
        I = CommandLineResolutions.find({F, std::string(SplitName.first)});
      }
      if (I == CommandLineResolutions.end()) {
        llvm::errs() << argv[0] << ": missing symbol resolution for " << F
                     << ',' << Sym.getName() << '\n';
        HasErrors = true;
      } else {
        Res.push_back(I->second.front());
        I->second.pop_front();
        if (I->second.empty())
          CommandLineResolutions.erase(I);
      }
    }

    if (HasErrors)
      continue;

    MBs.push_back(std::move(MB));
    check(Lto->add(std::move(Input), Res), F);
  }

  if (!CommandLineResolutions.empty()) {
    HasErrors = true;
    for (auto UnusedRes : CommandLineResolutions)
      llvm::errs() << argv[0] << ": unused symbol resolution for "
                   << UnusedRes.first.first << ',' << UnusedRes.first.second
                   << '\n';
  }
  if (HasErrors)
    return 1;

  const auto &BitcodeLibFuncs = Opts->get<&BitcodeLibFuncsOpt>();
  Lto->setBitcodeLibFuncs(
      SmallVector<StringRef>(BitcodeLibFuncs.begin(), BitcodeLibFuncs.end()));

  FileCache Cache;
  if (!CacheDir.empty())
    Cache = check(localCache("ThinLTO", "Thin", CacheDir, AddBuffer),
                  "failed to create cache");

  check(Lto->run(AddStream, Cache), "LTO::run failed");
  Lto->waitForCleanup();
  return static_cast<int>(HasErrors);
}

// The dump-symtab subcommand runs before any options are parsed.
static int dumpSymtab(int argc, char **argv) {
  const clv2::OptionsContext &OptsCtx = clv2::defaultOptionsContext();
  for (StringRef F : make_range(argv + 1, argv + argc)) {
    std::unique_ptr<MemoryBuffer> MB =
        check(MemoryBuffer::getFile(F), std::string(F));
    BitcodeFileContents BFC =
        check(getBitcodeFileContents(*MB), std::string(F));

    if (BFC.Symtab.size() >= sizeof(irsymtab::storage::Header)) {
      auto *Hdr = reinterpret_cast<const irsymtab::storage::Header *>(
          BFC.Symtab.data());
      outs() << "version: " << Hdr->Version << '\n';
      if (Hdr->Version == irsymtab::storage::Header::kCurrentVersion)
        outs() << "producer: " << Hdr->Producer.get(BFC.StrtabForSymtab)
               << '\n';
    }

    std::unique_ptr<InputFile> Input = check(
        InputFile::create(MB->getMemBufferRef(), OptsCtx), std::string(F));

    outs() << "target triple: " << Input->getTargetTriple() << '\n';
    Triple TT(Input->getTargetTriple());

    outs() << "source filename: " << Input->getSourceFileName() << '\n';

    if (TT.isOSBinFormatCOFF())
      outs() << "linker opts: " << Input->getCOFFLinkerOpts() << '\n';

    if (TT.isOSBinFormatELF()) {
      outs() << "dependent libraries:";
      for (auto L : Input->getDependentLibraries())
        outs() << " \"" << L << "\"";
      outs() << '\n';
    }

    ArrayRef<std::pair<StringRef, Comdat::SelectionKind>> ComdatTable =
        Input->getComdatTable();
    for (const InputFile::Symbol &Sym : Input->symbols()) {
      switch (Sym.getVisibility()) {
      case GlobalValue::HiddenVisibility:
        outs() << 'H';
        break;
      case GlobalValue::ProtectedVisibility:
        outs() << 'P';
        break;
      case GlobalValue::DefaultVisibility:
        outs() << 'D';
        break;
      }

      auto PrintBool = [&](char C, bool B) { outs() << (B ? C : '-'); };
      PrintBool('U', Sym.isUndefined());
      PrintBool('C', Sym.isCommon());
      PrintBool('W', Sym.isWeak());
      PrintBool('I', Sym.isIndirect());
      PrintBool('O', Sym.canBeOmittedFromSymbolTable());
      PrintBool('T', Sym.isTLS());
      PrintBool('X', Sym.isExecutable());
      outs() << ' ' << Sym.getName() << '\n';

      if (Sym.isCommon())
        outs() << "         size " << Sym.getCommonSize() << " align "
               << Sym.getCommonAlignment() << '\n';

      int Comdat = Sym.getComdatIndex();
      if (Comdat != -1) {
        outs() << "         comdat ";
        switch (ComdatTable[Comdat].second) {
        case Comdat::Any:
          outs() << "any";
          break;
        case Comdat::ExactMatch:
          outs() << "exactmatch";
          break;
        case Comdat::Largest:
          outs() << "largest";
          break;
        case Comdat::NoDeduplicate:
          outs() << "nodeduplicate";
          break;
        case Comdat::SameSize:
          outs() << "samesize";
          break;
        }
        outs() << ' ' << ComdatTable[Comdat].first << '\n';
      }

      if (TT.isOSBinFormatCOFF() && Sym.isWeak() && Sym.isIndirect())
        outs() << "         fallback " << Sym.getCOFFWeakExternalFallback()
               << '\n';

      if (!Sym.getSectionName().empty())
        outs() << "         section " << Sym.getSectionName() << "\n";
    }

    outs() << '\n';
  }

  return 0;
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);
  registerPluginLoaderOption();
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmPrinters();
  InitializeAllAsmParsers();

  // FIXME: This should use llvm::cl subcommands, but it isn't currently
  // possible to pass an argument not associated with a subcommand to a
  // subcommand (e.g. -use-new-pm).
  if (argc < 2)
    return usage();

  StringRef Subcommand = argv[1];
  // Ensure that argv[0] is correct after adjusting argv/argc.
  argv[1] = argv[0];
  if (Subcommand == "dump-symtab")
    return dumpSymtab(argc - 1, argv + 1);
  if (Subcommand == "run")
    return run(argc - 1, argv + 1);
  if (Subcommand == "print-guid" && argc > 2) {
    // Note the name of the function we're calling: this won't return the right
    // answer for internal linkage symbols.
    outs() << GlobalValue::getGUIDAssumingExternalLinkage(argv[2]) << '\n';
    return 0;
  }
  if (Subcommand == "--version") {
    cl::PrintVersionMessage();
    return 0;
  }
  return usage();
}
