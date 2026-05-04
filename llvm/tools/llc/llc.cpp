//===-- llc.cpp - Implement the LLVM Native Code Generator ----------------===//
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
#include "llvm/Analysis/AnalysisOptionsRegistration.h"
#include "llvm/Analysis/RuntimeLibcallInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/AsmParser/AsmParserOptionsRegistration.h"
#include "llvm/Bitcode/BitcodeOptionsRegistration.h"
#include "llvm/CGData/CGDataOptionsRegistration.h"
#include "llvm/CodeGen/CodeGenOptionsRegistration.h"
#include "llvm/CodeGen/CodeGenPassOptionsOptInfos.h"
#include "llvm/Config/Targets.h"
#if LLVM_HAS_ARC_TARGET
#include "llvm/Target/ARC/ARCOptionsOptInfos.h"
#endif
#if LLVM_HAS_CSKY_TARGET
#include "llvm/Target/CSKY/CSKYOptionsOptInfos.h"
#endif
#if LLVM_HAS_M68K_TARGET
#include "llvm/Target/M68k/M68kOptionsOptInfos.h"
#endif
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/CodeGen/CommandFlagsOptInfos.h"
#include "llvm/CodeGen/GlobalISel/RegBankSelect.h"
#include "llvm/CodeGen/LinkAllAsmWriterComponents.h"
#include "llvm/CodeGen/LinkAllCodegenComponents.h"
#include "llvm/CodeGen/MIRParser/MIRParser.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Frontend/OpenMP/OpenMPOptionsRegistration.h"
#include "llvm/IR/AutoUpgrade.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/IROptionsRegistration.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LLVMRemarkStreamer.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/InitializePasses.h"
#include "llvm/LTO/LTOOptionsRegistration.h"
#include "llvm/MC/MCOptionsRegistration.h"
#include "llvm/MC/MCTargetOptionsCommandFlags.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/ObjectOptionsRegistration.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassesOptionsOptInfos.h"
#include "llvm/Passes/PassesOptionsRegistration.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/ProfileData/ProfileDataOptionsRegistration.h"
#include "llvm/Remarks/RemarksOptionsRegistration.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/PGOOptions.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PluginLoader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/SupportOptions.h"
#include "llvm/Support/SupportOptionsOptInfos.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptionsRegistration.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/AggressiveInstCombine/AggressiveInstCombineOptionsRegistration.h"
#include "llvm/Transforms/Coroutines/CoroutinesOptionsRegistration.h"
#include "llvm/Transforms/IPO/IPOOptionsRegistration.h"
#include "llvm/Transforms/InstCombine/InstCombineOptionsRegistration.h"
#include "llvm/Transforms/Instrumentation/InstrumentationOptionsRegistration.h"
#include "llvm/Transforms/ObjCARC/ObjCARCOptionsRegistration.h"
#include "llvm/Transforms/Scalar/ScalarOptionsRegistration.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/UtilsOptionsRegistration.h"
#include "llvm/Transforms/Vectorize/VectorizeOptions.h"
#include "llvm/Transforms/Vectorize/VectorizeOptionsRegistration.h"
#include <cassert>
#include <memory>
#include <optional>
using namespace llvm;
using namespace llvm::clv2;

//===----------------------------------------------------------------------===//
// llc-local option declarations
//===----------------------------------------------------------------------===//

inline constexpr OptionInfo<std::string> InputFilenameOpt{
    "", "<input bitcode>", Positional{}, Init{"-"}};

inline constexpr ListOptionInfo<std::string> InstPrinterOptionsOpt{
    "M", "InstPrinter options", Hidden};

inline constexpr OptionInfo<std::string> InputLanguageOpt{
    "x", "Input language ('ir' or 'mir')"};

inline constexpr OptionInfo<std::string> OutputFilenameOpt{
    "o", "Output filename", value_desc("filename")};

inline constexpr OptionInfo<std::string> SplitDwarfOutputFileOpt{
    "split-dwarf-output", ".dwo output filename", value_desc("filename")};

inline constexpr OptionInfo<unsigned> TimeCompilationsOpt{
    "time-compilations", "Repeat compilation N times for timing",
    value_desc("N"), Init{1u}, Hidden};

inline constexpr OptionInfo<bool> TimeTraceOpt{"time-trace",
                                               "Record time trace"};

inline constexpr OptionInfo<unsigned> TimeTraceGranularityOpt{
    "time-trace-granularity",
    "Minimum time granularity (in microseconds) traced by time profiler",
    Init{500u}, Hidden};

inline constexpr OptionInfo<std::string> TimeTraceFileOpt{
    "time-trace-file", "Specify time trace file destination",
    value_desc("filename")};

inline constexpr OptionInfo<std::string> BinutilsVersionOpt{
    "binutils-version",
    "Produced object files can use all ELF features supported by this "
    "binutils version and newer. If -no-integrated-as is specified, the "
    "generated assembly will consider GNU as support. 'none' means that all "
    "ELF features can be used, regardless of binutils support",
    Hidden};

inline constexpr OptionInfo<bool> PreserveCommentsOpt{
    "preserve-as-comments", "Preserve Comments in outputted assembly",
    Init{true}, Hidden};

// -O<level>: optimization level, parsed as a string with PrefixFormat.
inline constexpr OptionInfo<std::string> OptLevelOpt{
    "O", "Optimization level. [-O0, -O1, -O2, or -O3] (default = '-O2')",
    PrefixFormat, value_desc("char"), Init{"2"}};

inline constexpr OptionInfo<std::string> TargetTripleOpt{
    "mtriple", "Override target triple for module"};

inline constexpr OptionInfo<std::string> SplitDwarfFileOpt{
    "split-dwarf-file",
    "Specify the name of the .dwo file to encode in the DWARF output"};

inline constexpr OptionInfo<bool> NoVerifyOpt{
    "disable-verify", "Do not verify input module", Hidden};

inline constexpr OptionInfo<bool> VerifyEachOpt{"verify-each",
                                                "Verify after each transform"};

inline constexpr OptionInfo<bool> DisableSimplifyLibCallsOpt{
    "disable-simplify-libcalls", "Disable simplify-libcalls"};

inline constexpr OptionInfo<bool> ShowMCEncodingOpt{
    "show-mc-encoding", "Show encoding in .s output", Hidden};

inline constexpr OptionInfo<unsigned> OutputAsmVariantOpt{
    "output-asm-variant", "Syntax variant to use for output printing"};

inline constexpr OptionInfo<bool> DwarfDirectoryOpt{
    "dwarf-directory", "Use .file directives with an explicit directory",
    Init{true}, Hidden};

inline constexpr OptionInfo<bool> AsmVerboseOpt{
    "asm-verbose", "Add comments to directives.", Init{true}};

inline constexpr OptionInfo<bool> CompileTwiceOpt{
    "compile-twice",
    "Run everything twice, re-using the same pass manager and verify the "
    "result is the same.",
    Init{false}, Hidden};

inline constexpr OptionInfo<bool> DiscardValueNamesOpt{
    "discard-value-names", "Discard names from Value (other than GlobalValue).",
    Init{false}, Hidden};

inline constexpr OptionInfo<bool> PrintMIR2VecVocabOpt{
    "print-mir2vec-vocab", "Print MIR2Vec vocabulary contents", Init{false},
    Hidden};

inline constexpr OptionInfo<bool> PrintMIR2VecOpt{
    "print-mir2vec", "Print MIR2Vec embeddings for functions", Init{false},
    Hidden};

inline constexpr ListOptionInfo<std::string> IncludeDirsOpt{
    "I", "include search path"};

inline constexpr OptionInfo<bool> RemarksWithHotnessOpt{
    "pass-remarks-with-hotness",
    "With PGO, include profile count in optimization remarks", Hidden};

// RemarksHotnessThreshold: 'N or auto' — stored as string, parsed manually.
inline constexpr OptionInfo<std::string> RemarksHotnessThresholdOpt{
    "pass-remarks-hotness-threshold",
    "Minimum profile count required for an optimization remark to be output. "
    "Use 'auto' to apply the threshold from profile summary.",
    value_desc("N or 'auto'"), Hidden};

inline constexpr OptionInfo<std::string> RemarksFilenameOpt{
    "pass-remarks-output", "Output filename for pass remarks",
    value_desc("filename")};

inline constexpr OptionInfo<std::string> RemarksPassesOpt{
    "pass-remarks-filter",
    "Only record optimization remarks from passes whose names match the given "
    "regular expression",
    value_desc("regex")};

inline constexpr OptionInfo<std::string> RemarksFormatOpt{
    "pass-remarks-format",
    "The format used for serializing remarks (default: YAML)",
    value_desc("format"), Init{"yaml"}};

inline constexpr ListOptionInfo<std::string> PassPluginsOpt{
    "load-pass-plugin", "Load plugin library"};

inline constexpr OptionInfo<bool> EnableNewPassManagerOpt{
    "enable-new-pm", "Enable the new pass manager", Init{false}};

inline constexpr OptionInfo<std::string> PassPipelineOpt{
    "passes",
    "A textual description of the pass pipeline. To have analysis passes "
    "available before a certain pass, add 'require<foo-analysis>'."};

inline constexpr AliasInfo PassPipelineAliasOpt{"p", "passes"};

// -run-pass: each occurrence adds a pass name.  Multiple -run-pass flags OR
// a comma-separated list on one flag are both accepted.
inline constexpr ListOptionInfo<std::string> RunPassOpt{
    "run-pass", "Run compiler only for specified passes (comma separated list)",
    value_desc("pass-name"), CommaSeparated};

// PGO kind option. Must use an enum type (not int) for makeEnumOption<>.
enum class PGOKindEnum { NoPGO = 0, SampleUsePipeline = 1 };
inline constexpr EnumVal<PGOKindEnum> PGOKindVals[] = {
    {"nopgo", PGOKindEnum::NoPGO, "Do not use PGO."},
    {"pgo-sample-use-pipeline", PGOKindEnum::SampleUsePipeline,
     "Use sampled profile to guide PGO."},
};
inline constexpr auto PGOKindFlagOpt = makeEnumOption<PGOKindEnum>(
    "pgo-kind", "The kind of profile guided optimization", PGOKindVals, Hidden,
    Init{PGOKindEnum::NoPGO});

//===----------------------------------------------------------------------===//
// TPC-level options (passed via setTPCValues())
//===----------------------------------------------------------------------===//

// start-after, start-before, stop-after and stop-before are declared in
// CodeGenPassOptions.td (CGPassSched2Reg); llc reads them from the context
// below and forwards them into the override.
//
// verify-machineinstrs, fast-isel and global-isel are declared there too, but
// TargetPassConfig overlays those onto the override itself, so llc must not
// forward them.  Declaring any of these a second time here would put the same
// CLI name in two registries, and the copy that loses is silently never set.

//===----------------------------------------------------------------------===//
// MC_* option descriptors are provided by MCOptionsOptInfos.h (MCOptsReg).

//===----------------------------------------------------------------------===//
// NewPMDriver options
//===----------------------------------------------------------------------===//

// Register allocator selection for the new pass manager.
inline constexpr EnumVal<RegAllocType> NPM_RegAllocVals[] = {
    {"default", RegAllocType::Default, "Default register allocator"},
    {"pbqp", RegAllocType::PBQP, "PBQP register allocator"},
    {"fast", RegAllocType::Fast, "Fast register allocator"},
    {"basic", RegAllocType::Basic, "Basic register allocator"},
    {"greedy", RegAllocType::Greedy, "Greedy register allocator"},
};
inline constexpr auto NPM_RegAlloc = makeEnumOption<RegAllocType>(
    "regalloc-npm", "Register allocator to use for new pass manager",
    NPM_RegAllocVals, Hidden, Init{RegAllocType::Unset});

inline constexpr OptionInfo<bool> NPM_DebugPM{
    "debug-pass-manager", "Print pass management debugging information",
    Hidden};

// print-pipeline-passes is declared in PassesOptions.td (PassesOptsReg) and
// read from the context below.

//===----------------------------------------------------------------------===//
// Registry
//===----------------------------------------------------------------------===//

// Tool-only options (including MC and NewPM flags that don't have shared
// library registries yet).
inline constexpr OptionsRegistry<
    &InputFilenameOpt, &InstPrinterOptionsOpt, &InputLanguageOpt,
    &OutputFilenameOpt, &SplitDwarfOutputFileOpt, &TimeCompilationsOpt,
    &TimeTraceOpt, &TimeTraceGranularityOpt, &TimeTraceFileOpt,
    &BinutilsVersionOpt, &PreserveCommentsOpt, &OptLevelOpt, &TargetTripleOpt,
    &SplitDwarfFileOpt, &NoVerifyOpt, &VerifyEachOpt,
    &DisableSimplifyLibCallsOpt, &ShowMCEncodingOpt, &OutputAsmVariantOpt,
    &DwarfDirectoryOpt, &AsmVerboseOpt, &CompileTwiceOpt, &DiscardValueNamesOpt,
    &PrintMIR2VecVocabOpt, &PrintMIR2VecOpt, &IncludeDirsOpt,
    &RemarksWithHotnessOpt, &RemarksHotnessThresholdOpt, &RemarksFilenameOpt,
    &RemarksPassesOpt, &RemarksFormatOpt, &PassPluginsOpt,
    &EnableNewPassManagerOpt, &PassPipelineOpt, &PassPipelineAliasOpt,
    &RunPassOpt, &PGOKindFlagOpt,
    // NewPM driver options
    &NPM_RegAlloc, &NPM_DebugPM>
    LLCToolReg;

using LLCToolOpts = decltype(LLCToolReg)::ParsedOptionsT;

// Registries parsed by this tool, with their bridge functions.
static void configureLLCRegistries(clv2::OptionParser &P) {
  P.add<&LLCToolReg>();
  registerCGOptsOptions(P);
  registerMCOptsOptions(P);
  P.add<&SupportOptsReg, support::applySupportOptions>();
  registerRemarksOptsOptions(P);
  registerObjectOptsOptions(P);
  registerAsmParserOptsOptions(P);
  registerPassesOptsOptions(P);
  registerIROptsOptions(P);
  registerScalarOptsOptions(P);
  registerAnalysisOptsOptions(P);
  registerIPOOptsOptions(P);
  registerVectorizeOptsOptions(P);
  registerTransformUtilsOptsOptions(P);
  registerInstrumentationOptsOptions(P);
  registerBitcodeOptsOptions(P);
  registerLTOOptsOptions(P);
  registerProfileDataOptsOptions(P);
  registerInstCombineOptsOptions(P);
  registerAggressiveInstCombineOptsOptions(P);
  registerCoroutinesOptsOptions(P);
  registerObjCARCOptsOptions(P);
  registerCGPassAsmPrintOptions(P);
  registerCGPassCore1Options(P);
  registerCGPassCore2Options(P);
  registerCGPassGISelOptions(P);
  registerCGPassMachine1Options(P);
  registerCGPassMachine2Options(P);
  registerCGPassAllocOptions(P);
  registerCGPassSched1Options(P);
  registerCGPassSched2Options(P);
  registerCGPassSelDAGOptions(P);
  registerCGDataOptsOptions(P);
  registerOMPOptsOptions(P);
#if LLVM_HAS_ARC_TARGET
  P.add<&clv2::ARCOptsReg>();
#endif
#if LLVM_HAS_CSKY_TARGET
  P.add<&clv2::CSKYOptsReg>();
#endif
#if LLVM_HAS_M68K_TARGET
  P.add<&clv2::M68kOptsReg>();
#endif
  registerX86Options(P);
  registerAArch64Options(P);
  registerAMDGPUOptionsWithBridge(P);
  registerARMOptions(P);
  registerHexagonOptions(P);
  registerRISCVOptions(P);
  registerPowerPCOptions(P);
  registerMipsOptions(P);
  registerSystemZOptions(P);
  registerSparcOptions(P);
  registerWebAssemblyOptions(P);
  registerLoongArchOptions(P);
  registerNVPTXOptions(P);
  registerLanaiOptions(P);
  registerBPFOptions(P);
  registerSPIRVOptions(P);
  registerMSP430Options(P);
  registerXCoreOptions(P);
}

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

static std::vector<std::string> &getRunPassNames() {
  static std::vector<std::string> RunPassNames;
  return RunPassNames;
}

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

static int compileModule(char **argv, SmallVectorImpl<PassPlugin> &,
                         LLVMContext &Context, std::string &OutFilename,
                         const LLCToolOpts *Opts);

static std::unique_ptr<ToolOutputFile>
GetOutputStream(Triple::OSType OS, const LLCToolOpts *Opts,
                const clv2::OptionsContext &Ctx) {
  std::string InputFilename = Opts->get<&InputFilenameOpt>();
  std::string OutputFilename = Opts->get<&OutputFilenameOpt>();
  if (OutputFilename.empty()) {
    if (InputFilename == "-")
      OutputFilename = "-";
    else {
      StringRef IFN = InputFilename;
      if (IFN.ends_with(".bc") || IFN.ends_with(".ll"))
        OutputFilename = std::string(IFN.drop_back(3));
      else if (IFN.ends_with(".mir"))
        OutputFilename = std::string(IFN.drop_back(4));
      else
        OutputFilename = std::string(IFN);

      switch (codegen::getFileType(Ctx)) {
      case CodeGenFileType::AssemblyFile:
        OutputFilename += ".s";
        break;
      case CodeGenFileType::ObjectFile:
        if (OS == Triple::Win32)
          OutputFilename += ".obj";
        else
          OutputFilename += ".o";
        break;
      case CodeGenFileType::Null:
        OutputFilename = "-";
        break;
      }
    }
  }

  bool Binary = false;
  switch (codegen::getFileType(Ctx)) {
  case CodeGenFileType::AssemblyFile:
    break;
  case CodeGenFileType::ObjectFile:
  case CodeGenFileType::Null:
    Binary = true;
    break;
  }

  std::error_code EC;
  sys::fs::OpenFlags OpenFlags = sys::fs::OF_None;
  if (!Binary)
    OpenFlags |= sys::fs::OF_TextWithCRLF;
  auto FDOut = std::make_unique<ToolOutputFile>(OutputFilename, EC, OpenFlags);
  if (EC)
    reportError(EC.message());
  return FDOut;
}

// Function to set PGO options on TargetMachine based on parsed flags.
static void setPGOOptions(TargetMachine &TM, const LLCToolOpts *Opts) {
  std::optional<PGOOptions> PGOOpt;
  PGOKindEnum PGOKind = Opts->get<&PGOKindFlagOpt>();
  if (PGOKind == PGOKindEnum::SampleUsePipeline) {
    PGOOpt = PGOOptions("", "", "", "", PGOOptions::SampleUse,
                        PGOOptions::NoCSAction);
  }
  if (PGOOpt)
    TM.setPGOOption(PGOOpt);
}

//===----------------------------------------------------------------------===//
// main
//===----------------------------------------------------------------------===//

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);
  registerPluginLoaderOption();

  // Enable debug stream buffering.
  EnableDebugBuffering = true;

  // Initialize targets first, so that --version shows registered targets.
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmPrinters();
  InitializeAllAsmParsers();

  // Register the Target and CPU printer for --version.
  cl::AddExtraVersionPrinter(sys::printDefaultTargetAndDetectedCPU);
  // Register the target printer for --version.
  cl::AddExtraVersionPrinter(TargetRegistry::printRegisteredTargetsForVersion);

  std::vector<clv2::detail::OptionEntry> ExtraEntries;
  // Pre-load pass plugins so that options they register are available
  // before parsing.
  SmallVector<PassPlugin, 1> PreloadedPlugins;
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
      if (Plugin) {
        PreloadedPlugins.emplace_back(Plugin.get());
      } else {
        errs() << "Could not load library '" << PluginPath
               << "': " << toString(Plugin.takeError()) << '\n';
        return 1;
      }
    }
  }

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

  clv2::OptionParser P;
  configureLLCRegistries(P);
  P.enableGlobalDynamicEntries();
  for (auto &E : ExtraEntries)
    P.addDynamicEntry(std::move(E));
  auto OptsCtxOwner =
      P.parse(argc, argv, "llvm system compiler\n", /*Errs=*/nullptr);
  const auto &OptsCtx = *OptsCtxOwner;
  const auto *Opts = OptsCtx.getViewPtr<&LLCToolReg>();

  {
    CGPassBuilderOption TPCVals;
    TPCVals.StartAfter =
        clv2::getOptValOr<&clv2::CGPassSched2Reg, &clv2::CGPASS_StartAfter>(
            OptsCtx, std::string{});
    TPCVals.StartBefore =
        clv2::getOptValOr<&clv2::CGPassSched2Reg, &clv2::CGPASS_StartBefore>(
            OptsCtx, std::string{});
    TPCVals.StopAfter =
        clv2::getOptValOr<&clv2::CGPassSched2Reg, &clv2::CGPASS_StopAfter>(
            OptsCtx, std::string{});
    TPCVals.StopBefore =
        clv2::getOptValOr<&clv2::CGPassSched2Reg, &clv2::CGPASS_StopBefore>(
            OptsCtx, std::string{});
    TPCVals.DebugPM = Opts->get<&NPM_DebugPM>();
    TPCVals.RegAlloc = Opts->get<&NPM_RegAlloc>();
    setTPCValues(TPCVals);
  }

  // Use pre-loaded plugins (loaded before parse so their options are visible
  // to the parser).
  SmallVector<PassPlugin, 1> PluginList(std::move(PreloadedPlugins));

  // Collect -run-pass names.
  for (const std::string &PassName : Opts->get<&RunPassOpt>())
    getRunPassNames().push_back(PassName);

  // Resolve -passes / -p alias.
  std::string PassPipeline = Opts->get<&PassPipelineOpt>();

  if (!PassPipeline.empty() && !getRunPassNames().empty()) {
    errs() << "The `llc -run-pass=...` syntax for the new pass manager is "
              "not supported, please use `llc -passes=<pipeline>` (or the `-p` "
              "alias for a more concise version).\n";
    return 1;
  }

  bool TimeTrace = Opts->get<&TimeTraceOpt>();
  std::string TimeTraceFile = Opts->get<&TimeTraceFileOpt>();
  unsigned TimeTraceGranularity = Opts->get<&TimeTraceGranularityOpt>();
  std::string OutputFilename = Opts->get<&OutputFilenameOpt>();

  if (TimeTrace)
    timeTraceProfilerInitialize(TimeTraceGranularity, argv[0]);
  llvm::scope_exit TimeTraceScopeExit([&]() {
    if (TimeTrace) {
      if (auto E = timeTraceProfilerWrite(TimeTraceFile, OutputFilename)) {
        handleAllErrors(std::move(E), [&](const StringError &SE) {
          errs() << SE.getMessage() << "\n";
        });
        return;
      }
      timeTraceProfilerCleanup();
    }
  });

  LLVMContext Context(OptsCtx);
  Context.setDiscardValueNames(Opts->get<&DiscardValueNamesOpt>());

  Context.setDiagnosticHandler(std::make_unique<LLCDiagnosticHandler>());

  // Parse the hotness threshold for remarks.
  std::optional<uint64_t> HotnessThreshold = 0;
  std::string HotnessThresholdStr = Opts->get<&RemarksHotnessThresholdOpt>();
  if (!HotnessThresholdStr.empty()) {
    if (HotnessThresholdStr == "auto") {
      HotnessThreshold = std::nullopt;
    } else {
      uint64_t Val;
      if (StringRef(HotnessThresholdStr).getAsInteger(10, Val)) {
        WithColor::error(errs(), "llc")
            << "invalid value for --pass-remarks-hotness-threshold: '"
            << HotnessThresholdStr << "'\n";
        return 1;
      }
      HotnessThreshold = Val;
    }
  }

  Expected<LLVMRemarkFileHandle> RemarksFileOrErr =
      setupLLVMOptimizationRemarks(
          Context, Opts->get<&RemarksFilenameOpt>(),
          Opts->get<&RemarksPassesOpt>(), Opts->get<&RemarksFormatOpt>(),
          Opts->get<&RemarksWithHotnessOpt>(), HotnessThreshold);
  if (Error E = RemarksFileOrErr.takeError())
    reportError(std::move(E), Opts->get<&RemarksFilenameOpt>());
  LLVMRemarkFileHandle RemarksFile = std::move(*RemarksFileOrErr);

  codegen::MaybeEnableStatistics(OptsCtx);

  std::string InputLanguage = Opts->get<&InputLanguageOpt>();
  if (InputLanguage != "" && InputLanguage != "ir" && InputLanguage != "mir")
    reportError("input language must be '', 'IR' or 'MIR'");

  unsigned TimeCompilations = Opts->get<&TimeCompilationsOpt>();
  for (unsigned I = TimeCompilations; I; --I)
    if (int RetVal =
            compileModule(argv, PluginList, Context, OutputFilename, Opts))
      return RetVal;

  if (RemarksFile)
    RemarksFile->keep();

  return codegen::MaybeSaveStatistics(OutputFilename, "llc", OptsCtx);
}

//===----------------------------------------------------------------------===//
// Pass helpers
//===----------------------------------------------------------------------===//

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
  if (P->getPassID() == &RegBankSelectLegacy::ID)
    static_cast<RegBankSelectLegacy *>(P)->setModeFromContext(
        TPC.getTM<TargetMachine>().getOptionsContext());
  std::string Banner = std::string("After ") + std::string(P->getPassName());
  TPC.addMachinePrePasses();
  PM.add(P);
  TPC.addMachinePostPasses(Banner);

  return false;
}

//===----------------------------------------------------------------------===//
// compileModule
//===----------------------------------------------------------------------===//

static int compileModule(char **argv, SmallVectorImpl<PassPlugin> &PluginList,
                         LLVMContext &Context, std::string &OutputFilename,
                         const LLCToolOpts *Opts) {
  SMDiagnostic Err;
  std::unique_ptr<Module> M;
  std::unique_ptr<MIRParser> MIR;
  Triple TheTriple;
  std::string CPUStr = codegen::getCPUStr(Context.getOptionsContext());
  std::string TuneCPUStr = codegen::getTuneCPUStr(Context.getOptionsContext());
  std::string FeaturesStr =
      codegen::getFeaturesStr(Context.getOptionsContext());

  auto setMIRFunctionAttributes = [&CPUStr, &TuneCPUStr,
                                   &FeaturesStr](Function &F) {
    codegen::setFunctionAttributes(F, CPUStr, FeaturesStr, TuneCPUStr);
  };

  // Extract optimization level from -O<level> string.
  std::string OStr = Opts->get<&OptLevelOpt>();
  char OptLevelChar = OStr.empty() ? '2' : OStr[0];
  CodeGenOptLevel OLvl;
  if (auto Level = CodeGenOpt::parseLevel(OptLevelChar)) {
    OLvl = *Level;
  } else {
    WithColor::error(errs(), argv[0]) << "invalid optimization level.\n";
    return 1;
  }

  std::string BinutilsVersion = Opts->get<&BinutilsVersionOpt>();
  if (!BinutilsVersion.empty() && BinutilsVersion != "none") {
    StringRef V = BinutilsVersion;
    unsigned Num;
    if (V.consumeInteger(10, Num) || Num == 0 ||
        !(V.empty() ||
          (V.consume_front(".") && !V.consumeInteger(10, Num) && V.empty()))) {
      WithColor::error(errs(), argv[0])
          << "invalid -binutils-version, accepting 'none' or major.minor\n";
      return 1;
    }
  }

  std::string TargetTripleStr = Opts->get<&TargetTripleOpt>();
  std::string InputFilename = Opts->get<&InputFilenameOpt>();
  std::string InputLanguage = Opts->get<&InputLanguageOpt>();
  bool NoVerify = Opts->get<&NoVerifyOpt>();
  bool VerifyEach = Opts->get<&VerifyEachOpt>();
  bool DisableSimplifyLibCalls = Opts->get<&DisableSimplifyLibCallsOpt>();
  bool ShowMCEncoding = Opts->get<&ShowMCEncodingOpt>();
  bool AsmVerbose = Opts->get<&AsmVerboseOpt>();
  bool PreserveComments = Opts->get<&PreserveCommentsOpt>();
  bool CompileTwice = Opts->get<&CompileTwiceOpt>();
  bool PrintMIR2VecVocab = Opts->get<&PrintMIR2VecVocabOpt>();
  bool PrintMIR2Vec = Opts->get<&PrintMIR2VecOpt>();
  std::string SplitDwarfFile = Opts->get<&SplitDwarfFileOpt>();
  std::string SplitDwarfOutputFile = Opts->get<&SplitDwarfOutputFileOpt>();
  std::vector<std::string> IncludeDirs = Opts->get<&IncludeDirsOpt>();
  std::vector<std::string> InstPrinterOptions =
      Opts->get<&InstPrinterOptionsOpt>();

  TargetOptions Options;
  auto InitializeOptions = [&](const Triple &TheTriple) {
    Options = codegen::InitTargetOptionsFromCodeGenFlags(
        TheTriple, Context.getOptionsContext());

    if (Options.XCOFFReadOnlyPointers) {
      if (!TheTriple.isOSAIX())
        reportError("-mxcoff-roptr option is only supported on AIX",
                    InputFilename);
      if (!Options.DataSections)
        reportError("-mxcoff-roptr option must be used with -data-sections",
                    InputFilename);
    }

    if (TheTriple.isX86() &&
        codegen::getFuseFPOps(Context.getOptionsContext()) !=
            FPOpFusion::FPOpFusionMode::Standard)
      WithColor::warning(errs(), argv[0])
          << "X86 backend ignores --fp-contract setting; use IR fast-math "
             "flags instead.";

    Options.BinutilsVersion =
        TargetMachine::parseBinutilsVersion(BinutilsVersion);
    Options.MCOptions.ShowMCEncoding = ShowMCEncoding;
    Options.MCOptions.AsmVerbose = AsmVerbose;
    Options.MCOptions.PreserveAsmComments = PreserveComments;
    if (Opts->template specified<&OutputAsmVariantOpt>())
      Options.MCOptions.OutputAsmVariant =
          Opts->template get<&OutputAsmVariantOpt>();
    Options.MCOptions.IASSearchPaths = IncludeDirs;
    Options.MCOptions.InstPrinterOptions = InstPrinterOptions;
    Options.MCOptions.SplitDwarfFile = SplitDwarfFile;
    if (Opts->template specified<&DwarfDirectoryOpt>()) {
      Options.MCOptions.MCUseDwarfDirectory =
          Opts->template get<&DwarfDirectoryOpt>()
              ? MCTargetOptions::EnableDwarfDirectory
              : MCTargetOptions::DisableDwarfDirectory;
    } else {
      Options.MCOptions.MCUseDwarfDirectory =
          MCTargetOptions::DefaultDwarfDirectory;
    }
    Options.MCOptions.OptsCtx = &Context.getOptionsContext();
    Options.OptsCtx = &Context.getOptionsContext();
  };

  std::optional<Reloc::Model> RM =
      codegen::getExplicitRelocModel(Context.getOptionsContext());
  std::optional<CodeModel::Model> CM =
      codegen::getExplicitCodeModel(Context.getOptionsContext());

  const Target *TheTarget = nullptr;
  std::unique_ptr<TargetMachine> Target;

  auto MAttrs = codegen::getMAttrs(Context.getOptionsContext());
  bool SkipModule =
      CPUStr == "help" || TuneCPUStr == "help" || is_contained(MAttrs, "help");
  if (SkipModule) {
    if (!TargetTripleStr.empty())
      TheTriple = Triple(Triple::normalize(TargetTripleStr));
    else
      TheTriple = Triple(sys::getDefaultTargetTriple());

    std::string Error;
    TheTarget = TargetRegistry::lookupTarget(
        codegen::getMArch(Context.getOptionsContext()), TheTriple, Error);
    if (!TheTarget) {
      WithColor::error(errs(), argv[0]) << Error << "\n";
      return 1;
    }

    InitializeOptions(TheTriple);
    std::string SkipModuleCPU = (TuneCPUStr == "help" ? "help" : CPUStr);
    Target = std::unique_ptr<TargetMachine>(TheTarget->createTargetMachine(
        TheTriple, SkipModuleCPU, FeaturesStr, Options, RM, CM, OLvl));
    if (!Target) {
      WithColor::error(errs(), argv[0])
          << "could not allocate target machine\n";
      return 1;
    }
    return 0;
  }

  auto SetDataLayout = [&](StringRef DataLayoutTargetTriple,
                           StringRef OldDLStr) -> std::optional<std::string> {
    std::string IRTargetTriple = DataLayoutTargetTriple.str();
    if (!TargetTripleStr.empty())
      IRTargetTriple = Triple::normalize(TargetTripleStr);
    TheTriple = Triple(IRTargetTriple);
    if (TheTriple.getTriple().empty())
      TheTriple.setTriple(sys::getDefaultTargetTriple());

    std::string Error;
    TheTarget = TargetRegistry::lookupTarget(
        codegen::getMArch(Context.getOptionsContext()), TheTriple, Error);
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
    Target->setOptionsContext(Context.getOptionsContext());

    setPGOOptions(*Target, Opts);
    return Target->createDataLayout().getStringRepresentation();
  };

  if (InputLanguage == "mir" ||
      (InputLanguage == "" && StringRef(InputFilename).ends_with(".mir"))) {
    MIR = createMIRParserFromFile(InputFilename, Err, Context,
                                  setMIRFunctionAttributes);
    if (MIR)
      M = MIR->parseIRModule(SetDataLayout);
  } else {
    M = parseIRFile(InputFilename, Err, Context,
                    ParserCallbacks(SetDataLayout));
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
          codegen::getExplicitLargeDataThreshold(Context.getOptionsContext()))
    Target->setLargeDataThreshold(*LDT);

  // Figure out where we are going to send the output.
  std::unique_ptr<ToolOutputFile> Out =
      GetOutputStream(TheTriple.getOS(), Opts, Context.getOptionsContext());
  if (!Out)
    return 1;

  Target->Options.ObjectFilenameForDebug = Out->outputFilename();
  OutputFilename = Out->outputFilename();
  Target->Options.VerifyArgABICompliance = 0;

  std::unique_ptr<ToolOutputFile> DwoOut;
  if (!SplitDwarfOutputFile.empty()) {
    std::error_code EC;
    DwoOut = std::make_unique<ToolOutputFile>(SplitDwarfOutputFile, EC,
                                              sys::fs::OF_None);
    if (EC)
      reportError(EC.message(), SplitDwarfOutputFile);
  }

  TargetLibraryInfoImpl TLII(M->getTargetTriple(), Target->Options.VecLib);
  if (DisableSimplifyLibCalls)
    TLII.disableAllFunctions();

  if (!NoVerify && verifyModule(*M, &errs()))
    reportError("input module cannot be verified", InputFilename);

  codegen::setFunctionAttributes(*M, CPUStr, FeaturesStr, TuneCPUStr);

  for (auto &Plugin : PluginList) {
    CodeGenFileType CGFT = codegen::getFileType(Context.getOptionsContext());
    if (Plugin.invokePreCodeGenCallback(*M, *Target, CGFT, Out->os())) {
      if (Context.getDiagHandlerPtr()->HasErrors)
        exit(1);
      Out->keep();
      return 0;
    }
  }

  if (mc::getExplicitRelaxAll(Context.getOptionsContext()) &&
      codegen::getFileType(Context.getOptionsContext()) !=
          CodeGenFileType::ObjectFile)
    WithColor::warning(errs(), argv[0])
        << ": warning: ignoring -mc-relax-all because filetype != obj";

  VerifierKind VK = VerifierKind::InputOutput;
  if (NoVerify)
    VK = VerifierKind::None;
  else if (VerifyEach)
    VK = VerifierKind::EachPass;

  std::string PassPipeline = Opts->get<&PassPipelineOpt>();

  bool EnableNewPassManager = Opts->get<&EnableNewPassManagerOpt>();
  bool EnableNewPassManagerSpecified =
      Opts->specified<&EnableNewPassManagerOpt>();

  // Use the NewPM if the user specifies -passes (NewPM specific), specifically
  // requests the NewPM with -enable-new-pm, or the target defaults to the
  // NewPM, the user has not explicitly disabled the NewPM with
  // -enable-new-pm=false, and the user has not specified -run-pass.
  if (!PassPipeline.empty() ||
      (EnableNewPassManagerSpecified && EnableNewPassManager) ||
      (Target->shouldDefaultToNewPM() &&
       !(EnableNewPassManagerSpecified && !EnableNewPassManager) &&
       getRunPassNames().empty())) {
    // Resolve the print-pipeline-passes format: empty means not requested,
    // "text" or "tree" selects the output format.
    // Outlives PipelineFmt, which is a StringRef into it.
    std::string PipelineFmtStorage;
    StringRef PipelineFmt;
    const auto &PipeCtx = Context.getOptionsContext();
    if (clv2::wasOptSpecified<&clv2::PassesOptsReg,
                              &clv2::PAS_PrintPipelinePasses>(PipeCtx)) {
      PipelineFmtStorage = clv2::getOptValOr<&clv2::PassesOptsReg,
                                             &clv2::PAS_PrintPipelinePasses>(
          PipeCtx, std::string{});
      PipelineFmt = PipelineFmtStorage.empty() ? StringRef("text")
                                               : StringRef(PipelineFmtStorage);
    }
    return compileModuleWithNewPM(
        argv[0], std::move(M), std::move(MIR), std::move(Target),
        std::move(Out), std::move(DwoOut), Context, TLII, VK, PassPipeline,
        PluginList, codegen::getFileType(Context.getOptionsContext()),
        PipelineFmt);
  }

  legacy::PassManager PM;
  PM.setOptionsContext(Context.getOptionsContext());
  PM.add(new TargetLibraryInfoWrapperPass(TLII));
  PM.add(new RuntimeLibraryInfoWrapper(
      Target->Options.ExceptionModel, Target->Options.EABIVersion,
      Options.MCOptions.ABIName, Target->Options.VecLib));

  {
    raw_pwrite_stream *OS = &Out->os();

    SmallVector<char, 0> Buffer;
    std::unique_ptr<raw_svector_ostream> BOS;
    if ((codegen::getFileType(Context.getOptionsContext()) !=
             CodeGenFileType::AssemblyFile &&
         !Out->os().supportsSeeking()) ||
        CompileTwice) {
      BOS = std::make_unique<raw_svector_ostream>(Buffer);
      OS = BOS.get();
    }

    const char *argv0 = argv[0];
    MachineModuleInfoWrapperPass *MMIWP =
        new MachineModuleInfoWrapperPass(Target.get());

    bool HasMCErrors = false;
    MCContext &MCCtx = MMIWP->getMMI().getContext();
    MCCtx.setDiagnosticHandler([&](const SMDiagnostic &SMD, bool IsInlineAsm,
                                   const SourceMgr &SrcMgr,
                                   std::vector<const MDNode *> &LocInfos) {
      WithColor::error(errs(), argv0) << SMD.getMessage() << '\n';
      HasMCErrors = true;
    });

    if (!getRunPassNames().empty()) {
      if (!MIR) {
        WithColor::error(errs(), argv[0])
            << "run-pass is for .mir file only.\n";
        delete MMIWP;
        return 1;
      }
      TargetPassConfig *PTPC = Target->createPassConfig(PM);
      TargetPassConfig &TPC = *PTPC;
      if (TPC.hasLimitedCodeGenPipeline(Target->getOptionsContext())) {
        WithColor::error(errs(), argv[0])
            << "run-pass cannot be used with "
            << TPC.getLimitedCodeGenPipelineReason(Target->getOptionsContext())
            << ".\n";
        delete PTPC;
        delete MMIWP;
        return 1;
      }

      TPC.setDisableVerify(NoVerify);
      PM.add(&TPC);
      PM.add(MMIWP);
      TPC.printAndVerify("");
      for (const std::string &RunPassName : getRunPassNames()) {
        if (addPass(PM, argv0, RunPassName, TPC))
          return 1;
      }
      TPC.setInitialized();
      PM.add(createPrintMIRPass(*OS));

      if (PrintMIR2VecVocab) {
        PM.add(createMIR2VecVocabPrinterLegacyPass(errs()));
      }
      if (PrintMIR2Vec) {
        PM.add(createMIR2VecPrinterLegacyPass(errs()));
      }

      PM.add(createFreeMachineFunctionPass());
    } else {
      // Use a large buffer for assembly output so the formatted_raw_ostream
      // created inside addPassesToEmitFile doesn't auto-flush mid-function.
      // Without this, GISel fallback warnings (written unbuffered to errs())
      // interleave with partially-flushed assembly on stdout when both share
      // a fd via 2>&1.
      if (codegen::getFileType(Context.getOptionsContext()) ==
              CodeGenFileType::AssemblyFile &&
          OS->GetBufferSize() < (1u << 20))
        OS->SetBufferSize(1u << 20);
      if (Target->addPassesToEmitFile(
              PM, *OS, DwoOut ? &DwoOut->os() : nullptr,
              codegen::getFileType(Context.getOptionsContext()), NoVerify,
              MMIWP)) {
        if (!HasMCErrors)
          reportError("target does not support generation of this file type");
      }

      if (PrintMIR2VecVocab) {
        PM.add(createMIR2VecVocabPrinterLegacyPass(errs()));
      }
      if (PrintMIR2Vec) {
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


    SmallVector<char, 0> CompileTwiceBuffer;
    if (CompileTwice) {
      std::unique_ptr<Module> M2(llvm::CloneModule(*M));
      PM.run(*M2);
      CompileTwiceBuffer = Buffer;
      Buffer.clear();
    }

    PM.run(*M);

    if (Context.getDiagHandlerPtr()->HasErrors || HasMCErrors)
      return 1;

    if (CompileTwice) {
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

  Out->keep();
  if (DwoOut)
    DwoOut->keep();

  return 0;
}
