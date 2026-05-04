//===- llvm-profgen.cpp - LLVM SPGO profile generation tool -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// llvm-profgen generates SPGO profiles from perf script ouput.
//
//===----------------------------------------------------------------------===//

#include "ErrorHandling.h"
#include "Options.h"
#include "PerfReader.h"
#include "ProfileGenerator.h"
#include "ProfiledBinary.h"
#include "llvm/Analysis/AnalysisOptionsOptInfos.h"
#include "llvm/AsmParser/AsmParserOptionsOptInfos.h"
#include "llvm/Bitcode/BitcodeOptionsOptInfos.h"
#include "llvm/DebugInfo/Symbolize/SymbolizableModule.h"
#include "llvm/IR/IROptionsOptInfos.h"
#include "llvm/LTO/LTOOptionsOptInfos.h"
#include "llvm/MC/MCOptionsOptInfos.h"
#include "llvm/Object/ObjectOptionsOptInfos.h"
#include "llvm/Passes/PassesOptionsOptInfos.h"
#include "llvm/ProfileData/ProfileDataOptionsOptInfos.h"
#include "llvm/Remarks/RemarksOptionsOptInfos.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/SupportOptions.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Target/AArch64/AArch64OptionsOptInfos.h"
#include "llvm/Target/AMDGPU/AMDGPUOptionsOptInfos.h"
#include "llvm/Target/ARM/ARMOptionsOptInfos.h"
#include "llvm/Target/BPF/BPFOptionsOptInfos.h"
#include "llvm/Target/Hexagon/HexagonOptionsOptInfos.h"
#include "llvm/Target/Lanai/LanaiOptionsOptInfos.h"
#include "llvm/Target/LoongArch/LoongArchOptionsOptInfos.h"
#include "llvm/Target/MSP430/MSP430OptionsOptInfos.h"
#include "llvm/Target/Mips/MipsOptionsOptInfos.h"
#include "llvm/Target/NVPTX/NVPTXOptionsOptInfos.h"
#include "llvm/Target/PowerPC/PowerPCOptionsOptInfos.h"
#include "llvm/Target/RISCV/RISCVOptionsOptInfos.h"
#include "llvm/Target/SPIRV/SPIRVOptionsOptInfos.h"
#include "llvm/Target/Sparc/SparcOptionsOptInfos.h"
#include "llvm/Target/SystemZ/SystemZOptionsOptInfos.h"
#include "llvm/Target/WebAssembly/WebAssemblyOptionsOptInfos.h"
#include "llvm/Target/X86/X86OptionsOptInfos.h"
#include "llvm/Target/XCore/XCoreOptionsOptInfos.h"
#include "llvm/Transforms/AggressiveInstCombine/AggressiveInstCombineOptionsOptInfos.h"
#include "llvm/Transforms/Coroutines/CoroutinesOptionsOptInfos.h"
#include "llvm/Transforms/IPO/IPOOptionsOptInfos.h"
#include "llvm/Transforms/InstCombine/InstCombineOptionsOptInfos.h"
#include "llvm/Transforms/Instrumentation/InstrumentationOptionsOptInfos.h"
#include "llvm/Transforms/ObjCARC/ObjCARCOptionsOptInfos.h"
#include "llvm/Transforms/Scalar/ScalarOptionsOptInfos.h"
#include "llvm/Transforms/Utils/UtilsOptionsOptInfos.h"
#include "llvm/Transforms/Vectorize/VectorizeOptionsOptInfos.h"

using namespace llvm;
using namespace sampleprof;

// ProfGenToolReg and ProfGenReg are defined after all OptionInfo descriptors

struct ProfGenLocalArgs {
  std::string PerfScriptFilename;
  unsigned PerfScriptFilenameOccurrences = 0;
  std::string PerfDataFilename;
  unsigned PerfDataFilenameOccurrences = 0;
  std::string UnsymbolizedProfFilename;
  unsigned UnsymbolizedProfFilenameOccurrences = 0;
  std::string SampleProfFilename;
  unsigned SampleProfFilenameOccurrences = 0;
  std::string BinaryPath;
  uint32_t ProcessId = 0;
  unsigned ProcessIdOccurrences = 0;
  std::string DebugBinPath;
  std::string DataAccessProfileFilename;
  std::string ETMPath;
  unsigned ETMPathOccurrences = 0;
  unsigned ETMTraceID = 0x10;
  std::string TargetTriple;
};

static void validateCommandLine(const ProfGenLocalArgs &Args,
                                const ProfGenConfig &Config) {
  if (!Config.ShowDisassemblyOnly) {
    bool HasPerfData = Args.PerfDataFilenameOccurrences > 0;
    bool HasPerfScript = Args.PerfScriptFilenameOccurrences > 0;
    bool HasUnsymbolizedProfile = Args.UnsymbolizedProfFilenameOccurrences > 0;
    bool HasSampleProfile = Args.SampleProfFilenameOccurrences > 0;
    bool HasEtm = Args.ETMPathOccurrences > 0;
    uint16_t S = HasPerfData + HasPerfScript + HasUnsymbolizedProfile +
                 HasSampleProfile + HasEtm;
    if (S != 1) {
      std::string Msg =
          S > 1 ? "Only one of `--perfscript`, `--perfdata`, "
                  "`--unsymbolized-profile`, "
                  "`--sample-profile` or `--etm` can be used."
                : "Perf input file is missing. Please provide one of "
                  "`--perfscript`, "
                  "`--perfdata`, `--unsymbolized-profile`, `--sample-profile`, "
                  "`--etm`.";
      exitWithError(Msg);
    }

    auto CheckFileExists = [](bool H, StringRef File) {
      if (H && !llvm::sys::fs::exists(File)) {
        std::string Msg = "Input perf file(" + File.str() + ") doesn't exist.";
        exitWithError(Msg);
      }
    };

    CheckFileExists(HasPerfData, Args.PerfDataFilename);
    CheckFileExists(HasPerfScript, Args.PerfScriptFilename);
    CheckFileExists(HasUnsymbolizedProfile, Args.UnsymbolizedProfFilename);
    CheckFileExists(HasSampleProfile, Args.SampleProfFilename);
    CheckFileExists(HasEtm, Args.ETMPath);
  }

  if (!llvm::sys::fs::exists(Args.BinaryPath)) {
    std::string Msg = "Input binary(" + Args.BinaryPath + ") doesn't exist.";
    exitWithError(Msg);
  }

  if (CSProfileGenerator::MaxCompressionSize < -1) {
    exitWithError("Value of --compress-recursion should >= -1");
  }
  if (Config.ShowSourceLocations && !Config.ShowDisassemblyOnly) {
    exitWithError("--show-source-locations should work together with "
                  "--show-disassembly-only!");
  }
}

static InputFile getInputFile(const ProfGenLocalArgs &Args) {
  InputFile File;
  if (Args.PerfDataFilenameOccurrences) {
    File.InputFilePath = Args.PerfDataFilename;
    File.Format = InputFormat::PerfData;
  } else if (Args.PerfScriptFilenameOccurrences) {
    File.InputFilePath = Args.PerfScriptFilename;
    File.Format = InputFormat::PerfScript;
  } else if (Args.UnsymbolizedProfFilenameOccurrences) {
    File.InputFilePath = Args.UnsymbolizedProfFilename;
    File.Format = InputFormat::UnsymbolizedProfile;
  } else if (Args.ETMPathOccurrences) {
    File.InputFilePath = Args.ETMPath;
    File.Format = InputFormat::ETMFormat;
  }
  return File;
}

static constexpr llvm::clv2::OptionCategory ProfGenCat{"ProfGen Options"};

namespace {
using namespace llvm;
using namespace llvm::clv2;

// Main file options
inline constexpr OptionInfo<std::string> PerfScriptFilenameOpt{
    "perfscript",
    "Path of perf-script trace created by Linux perf tool with "
    "`script` command(the raw perf.data should be profiled with -b). "
    "Cannot be used with --perfdata, --unsymbolized-profile, or "
    "--llvm-sample-profile.",
    cat(ProfGenCat), value_desc("perfscript")};
inline constexpr AliasInfo PSAOpt{"ps", "perfscript"};

inline constexpr OptionInfo<std::string> PerfDataFilenameOpt{
    "perfdata",
    "Path of raw perf data created by Linux perf tool (it should be "
    "profiled with -b). Cannot be used with --perfscript, "
    "--unsymbolized-profile, or --llvm-sample-profile.",
    cat(ProfGenCat), value_desc("perfdata")};
inline constexpr AliasInfo PDAOpt{"pd", "perfdata"};

inline constexpr OptionInfo<std::string> UnsymbolizedProfFilenameOpt{
    "unsymbolized-profile",
    "Path of the unsymbolized profile created by "
    "`llvm-profgen` with `--skip-symbolization`. "
    "Cannot be used with --perfscript, --perfdata, or "
    "--llvm-sample-profile.",
    cat(ProfGenCat), value_desc("unsymbolized profile")};
inline constexpr AliasInfo UPAOpt{"up", "unsymbolized-profile"};

inline constexpr OptionInfo<std::string> SampleProfFilenameOpt{
    "llvm-sample-profile",
    "Path of the LLVM sample profile. Cannot be used with"
    "--perfscript, --perfdata, or --unsymbolized-profile",
    cat(ProfGenCat), value_desc("llvm sample profile")};

inline constexpr OptionInfo<std::string> BinaryPathOpt{
    "binary", "Path of profiled executable binary.", clv2::Required,
    cat(ProfGenCat), value_desc("binary")};

inline constexpr OptionInfo<uint32_t> ProcessIdOpt{
    "pid", "Process Id for the profiled executable binary.", Init{0u},
    cat(ProfGenCat), value_desc("process Id")};

inline constexpr OptionInfo<std::string> DebugBinPathOpt{
    "debug-binary",
    "Path of debug info binary, llvm-profgen will load the DWARF info "
    "from it instead of the executable binary.",
    cat(ProfGenCat), value_desc("debug-binary")};

inline constexpr OptionInfo<std::string> DataAccessProfileFilenameOpt{
    "data-access-perftrace",
    "File path of a Linux perf raw trace (generated by `perf report "
    "-D`) consisting of memory access events.",
    cat(ProfGenCat), value_desc("data-access-perftrace")};

inline constexpr OptionInfo<std::string> ETMPathOpt{
    "etm", "Path of raw ETM trace file", cat(ProfGenCat), value_desc("etm")};

inline constexpr OptionInfo<unsigned> ETMTraceIDOpt{
    "etm-trace-id", "CoreSight Trace ID (CSID) used to route ETM trace data.",
    Init{0x10u}, cat(ProfGenCat)};

inline constexpr OptionInfo<std::string> TargetTripleOpt{
    "target-triple", "Override the target triple for the binary",
    cat(ProfGenCat), value_desc("triple")};

// ProfileGenerator.cpp options
inline constexpr OptionInfo<std::string> OutputFilenameOpt{
    "output", "Output profile file", clv2::Required, cat(ProfGenCat),
    value_desc("output")};
inline constexpr AliasInfo OutputAOpt{"o", "output"};

inline constexpr EnumVal<SampleProfileFormat> OutputFormatVals[] = {
    {"binary", SPF_Binary, "Binary encoding (default)"},
    {"extbinary", SPF_Ext_Binary, "Extensible binary encoding"},
    {"text", SPF_Text, "Text encoding"},
    {"gcc", SPF_GCC, "GCC encoding (only meaningful for -sample)"},
};
inline constexpr auto OutputFormatOpt = makeEnumOption<SampleProfileFormat>(
    "format", "Format of output profile", OutputFormatVals,
    Init{SPF_Ext_Binary}, cat(ProfGenCat));

inline constexpr OptionInfo<bool> UseMD5Opt{
    "use-md5",
    "Use md5 to represent function names in the output profile (only "
    "meaningful for -extbinary)",
    Init{false}, clv2::Hidden};

inline constexpr OptionInfo<bool> PopulateProfileSymbolListOpt{
    "populate-profile-symbol-list",
    "Populate profile symbol list (only meaningful for -extbinary)",
    Init{false}, clv2::Hidden};

inline constexpr OptionInfo<bool> FillZeroForAllFuncsOpt{
    "fill-zero-for-all-funcs",
    "Attribute all functions' range with zero count "
    "even it's not hit by any samples.",
    Init{false}, clv2::Hidden};

inline constexpr OptionInfo<int> RecursionCompressionOpt{
    "compress-recursion",
    "Compressing recursion by deduplicating adjacent frame "
    "sequences up to the specified size. -1 means no size limit.",
    Init{-1}, clv2::Hidden};

inline constexpr OptionInfo<bool> TrimColdProfileOpt{
    "trim-cold-profile",
    "If the total count of the profile is smaller "
    "than threshold, it will be trimmed.",
    Init{false}, cat(ProfGenCat)};

inline constexpr OptionInfo<bool> MarkAllContextPreinlinedOpt{
    "mark-all-context-preinlined",
    "Mark all function samples as preinlined(set "
    "ContextShouldBeInlined attribute).",
    Init{false}};

inline constexpr OptionInfo<bool> CSProfMergeColdContextOpt{
    "csprof-merge-cold-context",
    "If the total count of context profile is smaller than "
    "the threshold, it will be merged into context-less base profile.",
    Init{true}, cat(ProfGenCat)};

inline constexpr OptionInfo<uint32_t> CSProfMaxColdContextDepthOpt{
    "csprof-max-cold-context-depth",
    "Keep the last K contexts while merging cold profile. 1 means the "
    "context-less base profile",
    Init{1u}, cat(ProfGenCat)};

inline constexpr OptionInfo<int> CSProfMaxContextDepthOpt{
    "csprof-max-context-depth",
    "Keep the last K contexts while merging profile. -1 means no "
    "depth limit.",
    Init{-1}, cat(ProfGenCat)};

inline constexpr OptionInfo<double> ProfileDensityThresholdOpt{
    "profile-density-threshold",
    "If the profile density is below the given threshold, it "
    "will be suggested to increase the sampling rate.",
    Init{50.0}, cat(ProfGenCat)};

inline constexpr OptionInfo<bool> ShowDensityOpt{"show-density",
                                                 "show profile density details",
                                                 Init{false}, cat(ProfGenCat)};

inline constexpr OptionInfo<int> ProfileDensityCutOffHotOpt{
    "profile-density-cutoff-hot",
    "Total samples cutoff for functions used to calculate profile density.",
    Init{990000}, cat(ProfGenCat)};

inline constexpr OptionInfo<bool> UpdateTotalSamplesOpt{
    "update-total-samples",
    "Update total samples by accumulating all its body samples.", Init{false},
    cat(ProfGenCat)};

inline constexpr OptionInfo<bool> GenCSNestedProfileOpt{
    "gen-cs-nested-profile", "Generate nested function profiles for CSSPGO",
    Init{true}, clv2::Hidden};

inline constexpr OptionInfo<bool> InferMissingFramesOpt{
    "infer-missing-frames",
    "Infer missing call frames due to compiler tail call elimination.",
    Init{true}, cat(ProfGenCat)};

// PerfReader.cpp options
inline constexpr OptionInfo<bool> SkipSymbolizationOpt{
    "skip-symbolization",
    "Dump the unsymbolized profile to the output file. It will show unwinder "
    "output for CS profile generation.",
    Init{false}, cat(ProfGenCat)};

inline constexpr OptionInfo<bool> ShowMmapEventsOpt{
    "show-mmap-events", "Print binary load events.", Init{false},
    cat(ProfGenCat)};

inline constexpr OptionInfo<bool> UseOffsetOpt{
    "use-offset",
    "Work with `--skip-symbolization` or `--unsymbolized-profile` to "
    "write/read the offset instead of virtual address.",
    Init{true}, cat(ProfGenCat)};

inline constexpr OptionInfo<bool> UseLoadableSegmentAsBaseOpt{
    "use-first-loadable-segment-as-base",
    "Use first loadable segment address as base address for offsets in "
    "unsymbolized profile. By default first executable segment address is used",
    Init{false}, cat(ProfGenCat)};

inline constexpr OptionInfo<bool> IgnoreStackSamplesOpt{
    "ignore-stack-samples",
    "Ignore call stack samples for hybrid samples "
    "and produce context-insensitive profile.",
    Init{false}, cat(ProfGenCat)};

inline constexpr OptionInfo<bool> ShowDetailedWarningOpt{
    "show-detailed-warning", "Show detailed warning message.", Init{false},
    cat(ProfGenCat)};

inline constexpr OptionInfo<int> CSProfMaxUnsymbolizedCtxDepthOpt{
    "csprof-max-unsymbolized-context-depth",
    "Keep the last K contexts while merging unsymbolized profile. -1 "
    "means no depth limit.",
    Init{-1}, cat(ProfGenCat)};

inline constexpr OptionInfo<bool> TimeProfGenOpt{
    "time-profgen", "Time llvm-profgen phases", Init{false}, cat(ProfGenCat)};

// ProfiledBinary.cpp options
inline constexpr OptionInfo<bool> ShowDisassemblyOnlyOpt{
    "show-disassembly-only", "Print disassembled code.", Init{false},
    cat(ProfGenCat)};

inline constexpr OptionInfo<bool> ShowSourceLocationsOpt{
    "show-source-locations", "Print source locations.", Init{false},
    cat(ProfGenCat)};

inline constexpr OptionInfo<bool> LoadFunctionFromSymbolOpt{
    "load-function-from-symbol",
    "Gather additional binary function info from symbols (e.g. "
    "symtab) in case dwarf info is incomplete.",
    Init{true}, cat(ProfGenCat)};

inline constexpr OptionInfo<bool> ShowCanonicalFnNameOpt{
    "show-canonical-fname", "Print canonical function name.", Init{false},
    cat(ProfGenCat)};

inline constexpr OptionInfo<bool> ShowPseudoProbeOpt{
    "show-pseudo-probe", "Print pseudo probe section and disassembled info.",
    Init{false}, cat(ProfGenCat)};

inline constexpr OptionInfo<bool> UseDwarfCorrelationOpt{
    "use-dwarf-correlation",
    "Use dwarf for profile correlation even when binary contains pseudo probe.",
    Init{false}, cat(ProfGenCat)};

inline constexpr OptionInfo<std::string> DWPPathOpt{
    "dwp",
    "Path of .dwp file. When not specified, it will be <binary>.dwp in the "
    "same directory as the main binary.",
    Init{""}, cat(ProfGenCat)};

inline constexpr ListOptionInfo<std::string> DisassembleFunctionsOpt{
    "disassemble-functions",
    "List of functions to print disassembly for. Accept demangled "
    "names only. Only work with show-disassembly-only",
    clv2::CommaSeparated, cat(ProfGenCat)};

inline constexpr OptionInfo<bool> KernelBinaryOpt{
    "kernel", "Generate the profile for Linux kernel binary.", Init{false},
    cat(ProfGenCat)};

// CSPreInliner.cpp options
inline constexpr OptionInfo<bool> EnableCSPreInlinerOpt{
    "csspgo-preinliner",
    "Run a global pre-inliner to merge context profile based on "
    "estimated global top-down inline decisions",
    Init{true}, clv2::Hidden};

inline constexpr OptionInfo<bool> UseContextCostForPreInlinerOpt{
    "use-context-cost-for-preinliner",
    "Use context-sensitive byte size cost for preinliner decisions", Init{true},
    clv2::Hidden};

inline constexpr OptionInfo<bool> SamplePreInlineReplayOpt{
    "csspgo-replay-preinline",
    "Replay previous inlining and adjust context profile accordingly",
    Init{false}, clv2::Hidden};

inline constexpr OptionInfo<int> CSPreinlMultiplierForPrevInlOpt{
    "csspgo-preinliner-multiplier-for-previous-inlining",
    "Multiplier to bump up callsite threshold for previous inlining.",
    Init{100}, clv2::Hidden};

// MissingFrameInferrer.cpp option
inline constexpr OptionInfo<uint32_t> MaximumSearchDepthOpt{
    "max-search-depth",
    "The maximum levels the DFS-based missing frame search should go with",
    Init{UINT32_MAX - 1}, cat(ProfGenCat)};

} // anonymous namespace

static constexpr llvm::clv2::OptionsRegistry<
    &PerfScriptFilenameOpt, &PSAOpt, &PerfDataFilenameOpt, &PDAOpt,
    &UnsymbolizedProfFilenameOpt, &UPAOpt, &SampleProfFilenameOpt,
    &BinaryPathOpt, &ProcessIdOpt, &DebugBinPathOpt,
    &DataAccessProfileFilenameOpt, &ETMPathOpt, &ETMTraceIDOpt,
    &TargetTripleOpt, &OutputFilenameOpt, &OutputAOpt, &OutputFormatOpt,
    &UseMD5Opt, &PopulateProfileSymbolListOpt, &FillZeroForAllFuncsOpt,
    &RecursionCompressionOpt, &TrimColdProfileOpt, &MarkAllContextPreinlinedOpt,
    &CSProfMergeColdContextOpt, &CSProfMaxColdContextDepthOpt,
    &CSProfMaxContextDepthOpt, &ProfileDensityThresholdOpt, &ShowDensityOpt,
    &ProfileDensityCutOffHotOpt, &UpdateTotalSamplesOpt, &GenCSNestedProfileOpt,
    &InferMissingFramesOpt, &SkipSymbolizationOpt, &ShowMmapEventsOpt,
    &UseOffsetOpt, &UseLoadableSegmentAsBaseOpt, &IgnoreStackSamplesOpt,
    &ShowDetailedWarningOpt, &CSProfMaxUnsymbolizedCtxDepthOpt, &TimeProfGenOpt,
    &ShowDisassemblyOnlyOpt, &ShowSourceLocationsOpt,
    &LoadFunctionFromSymbolOpt, &ShowCanonicalFnNameOpt, &ShowPseudoProbeOpt,
    &UseDwarfCorrelationOpt, &DWPPathOpt, &DisassembleFunctionsOpt,
    &KernelBinaryOpt, &EnableCSPreInlinerOpt, &UseContextCostForPreInlinerOpt,
    &SamplePreInlineReplayOpt, &CSPreinlMultiplierForPrevInlOpt,
    &MaximumSearchDepthOpt>
    ProfGenToolReg;
// Registries parsed by this tool, with their bridge functions.
static void configureProfGenRegistries(llvm::clv2::OptionParser &P) {
  P.add<&ProfGenToolReg>();
  P.add<&llvm::clv2::SupportOptsReg, llvm::support::applySupportOptions>();
  P.add<&llvm::clv2::RemarksOptsReg>();
  P.add<&llvm::clv2::ObjectOptsReg>();
  P.add<&llvm::clv2::AsmParserOptsReg>();
  P.add<&llvm::clv2::TransformUtilsOptsReg>();
  P.add<&llvm::clv2::IPOOptsReg>();
  P.add<&llvm::clv2::ScalarOptsReg>();
  P.add<&llvm::clv2::AnalysisOptsReg>();
  P.add<&llvm::clv2::VectorizeOptsReg>();
  P.add<&llvm::clv2::InstrumentationOptsReg>();
  P.add<&llvm::clv2::BitcodeOptsReg>();
  P.add<&llvm::clv2::LTOOptsReg>();
  P.add<&llvm::clv2::InstCombineOptsReg>();
  P.add<&llvm::clv2::AggressiveInstCombineOptsReg>();
  P.add<&llvm::clv2::CoroutinesOptsReg>();
  P.add<&llvm::clv2::ObjCARCOptsReg>();
  P.add<&llvm::clv2::IROptsReg>();
  P.add<&llvm::clv2::MCOptsReg>();
  P.add<&llvm::clv2::PassesOptsReg>();
  P.add<&llvm::clv2::ProfileDataOptsReg>();
  P.add<&llvm::clv2::X86OptsReg>();
  P.add<&llvm::clv2::AArch64OptsReg>();
  P.add<&llvm::clv2::AMDGPUOptsReg>();
  P.add<&llvm::clv2::ARMOptsReg>();
  P.add<&llvm::clv2::HexagonOptsReg>();
  P.add<&llvm::clv2::RISCVOptsReg>();
  P.add<&llvm::clv2::PowerPCOptsReg>();
  P.add<&llvm::clv2::MipsOptsReg>();
  P.add<&llvm::clv2::SystemZOptsReg>();
  P.add<&llvm::clv2::SparcOptsReg>();
  P.add<&llvm::clv2::WebAssemblyOptsReg>();
  P.add<&llvm::clv2::LoongArchOptsReg>();
  P.add<&llvm::clv2::NVPTXOptsReg>();
  P.add<&llvm::clv2::LanaiOptsReg>();
  P.add<&llvm::clv2::BPFOptsReg>();
  P.add<&llvm::clv2::SPIRVOptsReg>();
  P.add<&llvm::clv2::MSP430OptsReg>();
  P.add<&llvm::clv2::XCoreOptsReg>();
}

int main(int argc, const char *argv[]) {
  using namespace llvm;

  InitLLVM X(argc, argv);

  // Initialize targets and assembly printers/parsers.
  InitializeAllTargetInfos();
  InitializeAllTargetMCs();
  InitializeAllDisassemblers();
  llvm::clv2::OptionParser P;
  configureProfGenRegistries(P);
  P.enableGlobalDynamicEntries();
  P.hideUnrelatedOptions({&ProfGenCat, &clv2::ColorOptionsCategory});
  auto OptsCtxOwner =
      P.parse(argc, argv, "llvm SPGO profile generator\n", /*Errs=*/nullptr);
  const auto &OptsCtx = *OptsCtxOwner;
  const auto *Opts = OptsCtx.getViewPtr<&ProfGenToolReg>();

  ProfGenLocalArgs LocalArgs;
  LocalArgs.PerfScriptFilename = Opts->get<&PerfScriptFilenameOpt>();
  LocalArgs.PerfScriptFilenameOccurrences =
      Opts->occurrences<&PerfScriptFilenameOpt>();
  LocalArgs.PerfDataFilename = Opts->get<&PerfDataFilenameOpt>();
  LocalArgs.PerfDataFilenameOccurrences =
      Opts->occurrences<&PerfDataFilenameOpt>();
  LocalArgs.UnsymbolizedProfFilename =
      Opts->get<&UnsymbolizedProfFilenameOpt>();
  LocalArgs.UnsymbolizedProfFilenameOccurrences =
      Opts->occurrences<&UnsymbolizedProfFilenameOpt>();
  LocalArgs.SampleProfFilename = Opts->get<&SampleProfFilenameOpt>();
  LocalArgs.SampleProfFilenameOccurrences =
      Opts->occurrences<&SampleProfFilenameOpt>();
  LocalArgs.BinaryPath = Opts->get<&BinaryPathOpt>();
  LocalArgs.ProcessId = Opts->get<&ProcessIdOpt>();
  LocalArgs.ProcessIdOccurrences = Opts->occurrences<&ProcessIdOpt>();
  LocalArgs.DebugBinPath = Opts->get<&DebugBinPathOpt>();
  LocalArgs.DataAccessProfileFilename =
      Opts->get<&DataAccessProfileFilenameOpt>();
  LocalArgs.ETMPath = Opts->get<&ETMPathOpt>();
  LocalArgs.ETMPathOccurrences = Opts->occurrences<&ETMPathOpt>();
  LocalArgs.ETMTraceID = Opts->get<&ETMTraceIDOpt>();
  LocalArgs.TargetTriple = Opts->get<&TargetTripleOpt>();

  ProfGenConfig Config;
  Config.OptsCtx = &OptsCtx;
  Config.OutputFilename = Opts->get<&OutputFilenameOpt>();
  Config.OutputFormat = Opts->get<&OutputFormatOpt>();
  Config.UseMD5 = Opts->get<&UseMD5Opt>();
  Config.PopulateProfileSymbolList = Opts->get<&PopulateProfileSymbolListOpt>();
  Config.FillZeroForAllFuncs = Opts->get<&FillZeroForAllFuncsOpt>();
  Config.TrimColdProfile = Opts->get<&TrimColdProfileOpt>();
  Config.MarkAllContextPreinlined = Opts->get<&MarkAllContextPreinlinedOpt>();
  Config.CSProfMergeColdContext = Opts->get<&CSProfMergeColdContextOpt>();
  Config.CSProfMergeColdContextOccurrences =
      Opts->occurrences<&CSProfMergeColdContextOpt>();
  Config.CSProfMaxColdContextDepth = Opts->get<&CSProfMaxColdContextDepthOpt>();
  Config.ProfileDensityThreshold = Opts->get<&ProfileDensityThresholdOpt>();
  Config.ShowDensity = Opts->get<&ShowDensityOpt>();
  Config.ProfileDensityCutOffHot = Opts->get<&ProfileDensityCutOffHotOpt>();
  Config.UpdateTotalSamples = Opts->get<&UpdateTotalSamplesOpt>();
  Config.GenCSNestedProfile = Opts->get<&GenCSNestedProfileOpt>();
  Config.InferMissingFrames = Opts->get<&InferMissingFramesOpt>();
  Config.SkipSymbolization = Opts->get<&SkipSymbolizationOpt>();
  Config.ShowMmapEvents = Opts->get<&ShowMmapEventsOpt>();
  Config.UseOffset = Opts->get<&UseOffsetOpt>();
  Config.UseLoadableSegmentAsBase = Opts->get<&UseLoadableSegmentAsBaseOpt>();
  Config.IgnoreStackSamples = Opts->get<&IgnoreStackSamplesOpt>();
  Config.CSProfMaxUnsymbolizedCtxDepth =
      Opts->get<&CSProfMaxUnsymbolizedCtxDepthOpt>();
  Config.ShowDetailedWarning = Opts->get<&ShowDetailedWarningOpt>();
  Config.TimeProfGen = Opts->get<&TimeProfGenOpt>();
  Config.ShowDisassemblyOnly = Opts->get<&ShowDisassemblyOnlyOpt>();
  Config.ShowSourceLocations = Opts->get<&ShowSourceLocationsOpt>();
  Config.LoadFunctionFromSymbol = Opts->get<&LoadFunctionFromSymbolOpt>();
  Config.ShowCanonicalFnName = Opts->get<&ShowCanonicalFnNameOpt>();
  Config.ShowPseudoProbe = Opts->get<&ShowPseudoProbeOpt>();
  Config.UseDwarfCorrelation = Opts->get<&UseDwarfCorrelationOpt>();
  Config.DWPPath = Opts->get<&DWPPathOpt>();
  Config.DisassembleFunctions = Opts->get<&DisassembleFunctionsOpt>();
  Config.KernelBinary = Opts->get<&KernelBinaryOpt>();
  Config.EnableCSPreInliner = Opts->get<&EnableCSPreInlinerOpt>();
  Config.UseContextCostForPreInliner =
      Opts->get<&UseContextCostForPreInlinerOpt>();
  Config.SamplePreInlineReplay = Opts->get<&SamplePreInlineReplayOpt>();
  Config.CSPreinlMultiplierForPrevInl =
      Opts->get<&CSPreinlMultiplierForPrevInlOpt>();
  Config.MaximumSearchDepth = Opts->get<&MaximumSearchDepthOpt>();

  sampleprof::CSProfileGenerator::MaxCompressionSize =
      Opts->get<&RecursionCompressionOpt>();
  sampleprof::CSProfileGenerator::MaxContextDepth =
      Opts->get<&CSProfMaxContextDepthOpt>();

  validateCommandLine(LocalArgs, Config);

  // Load symbols and disassemble the code of a given binary.
  std::unique_ptr<ProfiledBinary> Binary = std::make_unique<ProfiledBinary>(
      LocalArgs.BinaryPath, LocalArgs.DebugBinPath, Config);
  Binary->load(LocalArgs.TargetTriple);

  if (Config.ShowDisassemblyOnly)
    return EXIT_SUCCESS;

  if (LocalArgs.SampleProfFilenameOccurrences) {
    LLVMContext Context(OptsCtx);
    auto FS = vfs::getRealFileSystem();
    auto ReaderOrErr =
        SampleProfileReader::create(LocalArgs.SampleProfFilename, Context, *FS);
    if (std::error_code EC = ReaderOrErr.getError())
      exitWithError(EC, LocalArgs.SampleProfFilename);
    std::unique_ptr<sampleprof::SampleProfileReader> Reader =
        std::move(ReaderOrErr.get());
    Reader->read();
    std::unique_ptr<ProfileGeneratorBase> Generator =
        ProfileGeneratorBase::create(Binary.get(), Reader->getProfiles(),
                                     Reader->profileIsCS(), Config);
    Generator->generateProfile();
    Generator->write();
  } else {
    std::optional<uint32_t> PIDFilter;
    if (LocalArgs.ProcessIdOccurrences)
      PIDFilter = LocalArgs.ProcessId;
    InputFile File = getInputFile(LocalArgs);
    const ContextSampleCounterMap *Counters = nullptr;
    bool ProfileIsCS = false;
    std::unique_ptr<ETMReader> EtmReader;
    std::unique_ptr<PerfReaderBase> PerfReader;

    if (File.Format == InputFormat::ETMFormat) {
      EtmReader = std::make_unique<ETMReader>(
          Binary.get(), File.InputFilePath,
          static_cast<uint8_t>(LocalArgs.ETMTraceID));
      EtmReader->parseETMTraces();
      Counters = &EtmReader->getSampleCounters();
    } else {
      PerfReader =
          PerfReaderBase::create(Binary.get(), File, PIDFilter, Config);
      // Parse perf events and samples
      PerfReader->parsePerfTraces();

      if (!LocalArgs.DataAccessProfileFilename.empty()) {
        if (PerfReader->profileIsCS() || Binary->usePseudoProbes()) {
          exitWithError("Symbolizing vtables from data access profiles is not "
                        "yet supported for context-sensitive perf traces or "
                        "when pseudo-probe based mapping is enabled. ");
        }
        if (Error E = PerfReader->parseDataAccessPerfTraces(
                LocalArgs.DataAccessProfileFilename, PIDFilter)) {
          handleAllErrors(std::move(E), [&](const StringError &SE) {
            exitWithError(SE.getMessage());
          });
        }
      }
      Counters = &PerfReader->getSampleCounters();
      ProfileIsCS = PerfReader->profileIsCS();
    }

    if (Config.SkipSymbolization)
      return EXIT_SUCCESS;

    std::unique_ptr<ProfileGeneratorBase> Generator =
        ProfileGeneratorBase::create(Binary.get(), Counters, ProfileIsCS,
                                     Config);
    Generator->generateProfile();
    Generator->write();
  }

  return EXIT_SUCCESS;
}
