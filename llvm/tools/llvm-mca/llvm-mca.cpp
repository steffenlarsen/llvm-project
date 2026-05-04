//===-- llvm-mca.cpp - Machine Code Analyzer -------------------*- C++ -* -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This utility is a simple driver that allows static performance analysis on
// machine code similarly to how IACA (Intel Architecture Code Analyzer) works.
//
//   llvm-mca [options] <file-name>
//      -march <type>
//      -mcpu <cpu>
//      -o <file>
//
// The target defaults to the host target.
// The cpu is derived from the triple if not specified; pass -mcpu=native to
// select the host cpu.
// The output defaults to standard output.
//
//===----------------------------------------------------------------------===//

#include "CodeRegion.h"
#include "CodeRegionGenerator.h"
#include "PipelinePrinter.h"
#include "Views/BottleneckAnalysis.h"
#include "Views/DispatchStatistics.h"
#include "Views/InstructionInfoView.h"
#include "Views/RegisterFileStatistics.h"
#include "Views/ResourcePressureView.h"
#include "Views/RetireControlUnitStatistics.h"
#include "Views/SchedulerStatistics.h"
#include "Views/SummaryView.h"
#include "Views/TimelineView.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCOptionsOptInfos.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptionsCommandFlags.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/MCA/CodeEmitter.h"
#include "llvm/MCA/Context.h"
#include "llvm/MCA/CustomBehaviour.h"
#include "llvm/MCA/InstrBuilder.h"
#include "llvm/MCA/Pipeline.h"
#include "llvm/MCA/Stages/EntryStage.h"
#include "llvm/MCA/Stages/InstructionTables.h"
#include "llvm/MCA/Support.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
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
#include "llvm/TargetParser/Host.h"

using namespace llvm;
using namespace llvm::clv2;

namespace {
static constexpr OptionCategory ToolCategory{"Tool Options"};
static constexpr OptionCategory ViewCategory{"View Options"};

static constexpr OptionInfo<std::string> InputFilenameOpt{
    "input", "<input file>", Positional{}, Init{"-"}, cat(ToolCategory)};

static constexpr OptionInfo<std::string> OutputFilenameOpt{
    "o", "Output filename", value_desc("filename"), Init{"-"},
    cat(ToolCategory)};

static constexpr OptionInfo<std::string> ArchNameOpt{
    "march", "Target architecture. See -version for available targets",
    cat(ToolCategory)};

static constexpr OptionInfo<std::string> TripleNameOpt{
    "mtriple", "Target triple. See -version for available targets",
    cat(ToolCategory)};

static constexpr OptionInfo<std::string> MCPUOpt{
    "mcpu", "Target a specific cpu type (-mcpu=help for details)",
    value_desc("cpu-name"), Init{""}, cat(ToolCategory)};

static constexpr ListOptionInfo<std::string> MAttrsOpt{
    "mattr", "Target specific attributes (-mattr=help for details)",
    value_desc("a1,+a2,-a3,..."), CommaSeparated, cat(ToolCategory)};

static constexpr OptionInfo<bool> PrintJsonOpt{
    "json", "Print the output in json format", cat(ToolCategory)};

static constexpr OptionInfo<int> OutputAsmVariantOpt{
    "output-asm-variant", "Syntax variant to use for output printing", Init{-1},
    cat(ToolCategory)};

static constexpr OptionInfo<bool> PrintImmHexOpt{
    "print-imm-hex", "Prefer hex format when printing immediate values",
    cat(ToolCategory)};

static constexpr OptionInfo<unsigned> IterationsOpt{
    "iterations", "Number of iterations to run", Init{0u}, cat(ToolCategory)};

static constexpr OptionInfo<unsigned> DispatchWidthOpt{
    "dispatch", "Override the processor dispatch width", Init{0u},
    cat(ToolCategory)};

static constexpr OptionInfo<unsigned> RegisterFileSizeOpt{
    "register-file-size",
    "Maximum number of physical registers which can be used for register "
    "mappings",
    Init{0u}, cat(ToolCategory)};

static constexpr OptionInfo<unsigned> MicroOpQueueOpt{
    "micro-op-queue-size", "Number of entries in the micro-op queue", Init{0u},
    Hidden, cat(ToolCategory)};

static constexpr OptionInfo<unsigned> DecoderThroughputOpt{
    "decoder-throughput",
    "Maximum throughput from the decoders (instructions per cycle)", Init{0u},
    Hidden, cat(ToolCategory)};

static constexpr OptionInfo<unsigned> CallLatencyOpt{
    "call-latency", "Number of cycles to assume for a call instruction",
    Init{100u}, Hidden, cat(ToolCategory)};

enum class SkipType { NONE, LACK_SCHED, PARSE_FAILURE, ANY_FAILURE };
static constexpr EnumVal<SkipType> SkipTypeVals[] = {
    {"none", SkipType::NONE,
     "Exit with an error when an instruction is unsupported for any reason "
     "(default)"},
    {"lack-sched", SkipType::LACK_SCHED,
     "Skip instructions on input which lack scheduling information"},
    {"parse-failure", SkipType::PARSE_FAILURE,
     "Skip lines on the input which fail to parse for any reason"},
    {"any", SkipType::ANY_FAILURE,
     "Skip instructions or lines on input which are unsupported for any "
     "reason"},
};
static constexpr OptionInfo<SkipType> SkipUnsupportedInstructionsOpt{
    "skip-unsupported-instructions",
    "Force analysis to continue in the presence of unsupported instructions",
    Init{SkipType::NONE}, ValuesRef(SkipTypeVals), cat(ViewCategory)};

static constexpr OptionInfo<bool> PrintRegisterFileStatsOpt{
    "register-file-stats", "Print register file statistics", cat(ViewCategory)};

static constexpr OptionInfo<bool> PrintDispatchStatsOpt{
    "dispatch-stats", "Print dispatch statistics", cat(ViewCategory)};

static constexpr OptionInfo<bool> PrintSummaryViewOpt{
    "summary-view", "Print summary view (enabled by default)", Init{true},
    Hidden, cat(ViewCategory)};

static constexpr OptionInfo<bool> PrintSchedulerStatsOpt{
    "scheduler-stats", "Print scheduler statistics", cat(ViewCategory)};

static constexpr OptionInfo<bool> PrintRetireStatsOpt{
    "retire-stats", "Print retire control unit statistics", cat(ViewCategory)};

static constexpr OptionInfo<bool> PrintResourcePressureViewOpt{
    "resource-pressure",
    "Print the resource pressure view (enabled by default)", Init{true},
    cat(ViewCategory)};

static constexpr OptionInfo<bool> PrintTimelineViewOpt{
    "timeline", "Print the timeline view", cat(ViewCategory)};

static constexpr OptionInfo<unsigned> TimelineMaxIterationsOpt{
    "timeline-max-iterations",
    "Maximum number of iterations to print in timeline view", Init{0u},
    cat(ViewCategory)};

static constexpr OptionInfo<unsigned> TimelineMaxCyclesOpt{
    "timeline-max-cycles",
    "Maximum number of cycles in the timeline view, or 0 for unlimited. "
    "Defaults to 80 cycles",
    Init{80u}, cat(ViewCategory)};

static constexpr OptionInfo<bool> AssumeNoAliasOpt{
    "noalias", "If set, assume that loads and stores do not alias", Init{true},
    cat(ToolCategory)};

static constexpr OptionInfo<unsigned> LoadQueueSizeOpt{
    "lqueue", "Size of the load queue", Init{0u}, cat(ToolCategory)};

static constexpr OptionInfo<unsigned> StoreQueueSizeOpt{
    "squeue", "Size of the store queue", Init{0u}, cat(ToolCategory)};

enum class InstructionTablesType { NONE, NORMAL, FULL };
static constexpr EnumVal<InstructionTablesType> InstructionTablesVals[] = {
    {"none", InstructionTablesType::NONE, "Do not print instruction tables"},
    {"normal", InstructionTablesType::NORMAL, "Print instruction tables"},
    {"", InstructionTablesType::NORMAL, ""},
    {"full", InstructionTablesType::FULL,
     "Print instruction tables with additional information: bypass latency, "
     "LLVM opcode, used resources"},
};
static constexpr OptionInfo<InstructionTablesType> InstructionTablesOpt{
    "instruction-tables",
    "Print instruction tables",
    ValueOptional,
    Init{InstructionTablesType::NONE},
    ValuesRef(InstructionTablesVals),
    cat(ToolCategory)};

static constexpr OptionInfo<bool> PrintInstructionInfoViewOpt{
    "instruction-info", "Print the instruction info view (enabled by default)",
    Init{true}, cat(ViewCategory)};

static constexpr OptionInfo<bool> EnableAllStatsOpt{
    "all-stats", "Print all hardware statistics", cat(ViewCategory)};

static constexpr OptionInfo<bool> EnableAllViewsOpt{
    "all-views", "Print all views including hardware statistics",
    cat(ViewCategory)};

static constexpr OptionInfo<bool> EnableBottleneckAnalysisOpt{
    "bottleneck-analysis", "Enable bottleneck analysis (disabled by default)",
    cat(ViewCategory)};

static constexpr OptionInfo<bool> ShowEncodingOpt{
    "show-encoding", "Print encoding information in the instruction info view",
    cat(ViewCategory)};

static constexpr OptionInfo<bool> ShowBarriersOpt{
    "show-barriers",
    "Print memory barrier information in the instruction info view",
    cat(ViewCategory)};

static constexpr OptionInfo<bool> DisableCustomBehaviourOpt{
    "disable-cb",
    "Disable custom behaviour (use the default class which does nothing).",
    cat(ViewCategory)};

static constexpr OptionInfo<bool> DisableInstrumentManagerOpt{
    "disable-im",
    "Disable instrumentation manager (use the default class which ignores "
    "instruments.).",
    cat(ViewCategory)};

#ifndef NDEBUG
static constexpr OptionInfo<bool> DebugOpt{"debug", "Enable debug output",
                                           Hidden};
#endif

#define MCA_COMMON_OPTS                                                        \
  &InputFilenameOpt, &OutputFilenameOpt, &ArchNameOpt, &TripleNameOpt,         \
      &MCPUOpt, &MAttrsOpt, &PrintJsonOpt, &OutputAsmVariantOpt,               \
      &PrintImmHexOpt, &IterationsOpt, &DispatchWidthOpt,                      \
      &RegisterFileSizeOpt, &MicroOpQueueOpt, &DecoderThroughputOpt,           \
      &CallLatencyOpt, &SkipUnsupportedInstructionsOpt,                        \
      &PrintRegisterFileStatsOpt, &PrintDispatchStatsOpt,                      \
      &PrintSummaryViewOpt, &PrintSchedulerStatsOpt, &PrintRetireStatsOpt,     \
      &PrintResourcePressureViewOpt, &PrintTimelineViewOpt,                    \
      &TimelineMaxIterationsOpt, &TimelineMaxCyclesOpt, &AssumeNoAliasOpt,     \
      &LoadQueueSizeOpt, &StoreQueueSizeOpt, &InstructionTablesOpt,            \
      &PrintInstructionInfoViewOpt, &EnableAllStatsOpt, &EnableAllViewsOpt,    \
      &EnableBottleneckAnalysisOpt, &ShowEncodingOpt, &ShowBarriersOpt,        \
      &DisableCustomBehaviourOpt, &DisableInstrumentManagerOpt

#ifndef NDEBUG
static constexpr OptionsRegistry<MCA_COMMON_OPTS, &DebugOpt> MCAToolReg;
#else
static constexpr OptionsRegistry<MCA_COMMON_OPTS> MCAToolReg;
#endif
#undef MCA_COMMON_OPTS
} // namespace

struct MCAArgs {
  std::string InputFilename;
  std::string OutputFilename;
  std::string ArchName;
  std::string TripleName;
  std::string MCPU;
  std::vector<std::string> MAttrs;
  bool PrintJson;
  int OutputAsmVariant;
  bool PrintImmHex;
  unsigned Iterations;
  unsigned DispatchWidth;
  unsigned RegisterFileSize;
  unsigned MicroOpQueue;
  unsigned DecoderThroughput;
  unsigned CallLatency;
  SkipType SkipUnsupportedInstructions;
  bool PrintRegisterFileStats;
  bool PrintDispatchStats;
  bool PrintSummaryView;
  bool PrintSchedulerStats;
  bool PrintRetireStats;
  bool PrintResourcePressureView;
  bool PrintTimelineView;
  unsigned TimelineMaxIterations;
  unsigned TimelineMaxCycles;
  bool AssumeNoAlias;
  unsigned LoadQueueSize;
  unsigned StoreQueueSize;
  InstructionTablesType InstructionTables;
  bool PrintInstructionInfoView;
  bool EnableAllStats;
  bool EnableAllViews;
  bool EnableBottleneckAnalysis;
  bool ShowEncoding;
  bool ShowBarriers;
  bool DisableCustomBehaviour;
  bool DisableInstrumentManager;
};

static bool shouldSkip(SkipType SkipVal, SkipType CurType) {
  if (SkipVal == SkipType::NONE)
    return false;
  if (SkipVal == SkipType::ANY_FAILURE)
    return true;
  return CurType == SkipVal;
}

static bool shouldPrintInstructionTables(InstructionTablesType IT) {
  return IT != InstructionTablesType::NONE;
}

static bool shouldPrintInstructionTablesOfType(InstructionTablesType IT,
                                               InstructionTablesType Want) {
  return IT == Want;
}

struct ViewPositions {
  uint16_t PrintRegisterFileStats;
  uint16_t PrintDispatchStats;
  uint16_t PrintSummaryView;
  uint16_t PrintSchedulerStats;
  uint16_t PrintRetireStats;
  uint16_t PrintResourcePressureView;
  uint16_t PrintTimelineView;
  uint16_t PrintInstructionInfoView;
  uint16_t EnableAllStats;
  uint16_t EnableAllViews;
  uint16_t EnableBottleneckAnalysis;
};

// Apply --all-views / --all-stats overrides to individual view flags.
// The all-flag overrides an individual flag only if it appeared *after* that
// flag on the command line (position 0 means never specified).
static void processViewOptions(MCAArgs &Args, const ViewPositions &Pos,
                               bool IsOutOfOrder) {
  if (Pos.EnableAllViews == 0 && Pos.EnableAllStats == 0)
    return;

  // Override Flag with AllDefault if the all-flag appeared after the individual
  // flag (or the individual flag was never specified).
  auto apply = [](bool &Flag, uint16_t FlagPos, uint16_t AllPos,
                  bool AllDefault) {
    if (FlagPos == 0 || FlagPos < AllPos)
      Flag = AllDefault;
  };

  if (Pos.EnableAllViews > 0) {
    apply(Args.PrintSummaryView, Pos.PrintSummaryView, Pos.EnableAllViews,
          Args.EnableAllViews);
    if (IsOutOfOrder)
      apply(Args.EnableBottleneckAnalysis, Pos.EnableBottleneckAnalysis,
            Pos.EnableAllViews, Args.EnableAllViews);
    apply(Args.PrintResourcePressureView, Pos.PrintResourcePressureView,
          Pos.EnableAllViews, Args.EnableAllViews);
    apply(Args.PrintTimelineView, Pos.PrintTimelineView, Pos.EnableAllViews,
          Args.EnableAllViews);
    apply(Args.PrintInstructionInfoView, Pos.PrintInstructionInfoView,
          Pos.EnableAllViews, Args.EnableAllViews);
  }

  // --all-stats and --all-views both affect the stat views; pick the later one.
  // If both are specified, the later one wins; use its value as the default.
  uint16_t StatPos;
  bool StatDefault;
  if (Pos.EnableAllStats >= Pos.EnableAllViews) {
    StatPos = Pos.EnableAllStats;
    StatDefault = Args.EnableAllStats;
  } else {
    StatPos = Pos.EnableAllViews;
    StatDefault = Args.EnableAllViews;
  }
  if (StatPos > 0) {
    apply(Args.PrintRegisterFileStats, Pos.PrintRegisterFileStats, StatPos,
          StatDefault);
    apply(Args.PrintDispatchStats, Pos.PrintDispatchStats, StatPos,
          StatDefault);
    apply(Args.PrintSchedulerStats, Pos.PrintSchedulerStats, StatPos,
          StatDefault);
    if (IsOutOfOrder)
      apply(Args.PrintRetireStats, Pos.PrintRetireStats, StatPos, StatDefault);
  }
}

// Returns true on success.
static bool runPipeline(mca::Pipeline &P) {
  Expected<unsigned> Cycles = P.run();
  if (!Cycles) {
    WithColor::error() << toString(Cycles.takeError());
    return false;
  }
  return true;
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  // Initialize targets and assembly parsers.
  InitializeAllTargetInfos();
  InitializeAllTargetMCs();
  InitializeAllAsmParsers();
  InitializeAllTargetMCAs();

  auto PrintVersions = [](raw_ostream &OS) {
    sys::printDefaultTargetAndDetectedCPU(OS);
    TargetRegistry::printRegisteredTargetsForVersion(OS);
  };

  clv2::OptionParser P;
  P.add<&MCAToolReg>();
  RegisterCoreLLVMOptions(P);
  // MC scheduling options (e.g. the reservation-station scale factor) are
  // read via MCSchedModel; without this the flags parse as dynamic entries
  // but their view never reaches the OptionsContext, so reads see defaults.
  P.add<&MCOptsReg>();
  P.add<&X86OptsReg>();
  P.add<&AArch64OptsReg>();
  P.add<&AMDGPUOptsReg>();
  P.add<&ARMOptsReg>();
  P.add<&HexagonOptsReg>();
  P.add<&RISCVOptsReg>();
  P.add<&PowerPCOptsReg>();
  P.add<&MipsOptsReg>();
  P.add<&SystemZOptsReg>();
  P.add<&SparcOptsReg>();
  P.add<&WebAssemblyOptsReg>();
  P.add<&LoongArchOptsReg>();
  P.add<&NVPTXOptsReg>();
  P.add<&LanaiOptsReg>();
  P.add<&BPFOptsReg>();
  P.add<&SPIRVOptsReg>();
  P.add<&MSP430OptsReg>();
  P.add<&XCoreOptsReg>();
  P.enableGlobalDynamicEntries();
  P.hideUnrelatedOptions(
      {&ToolCategory, &ViewCategory, &clv2::MCScheduleOptionsCategory});
  auto OptsCtx =
      P.parse(argc, argv, "llvm machine code performance analyzer.\n", nullptr,
              "", nullptr, PrintVersions);
  auto *ParsedOpts = OptsCtx->getViewPtr<&MCAToolReg>();

  MCAArgs Args;
  Args.InputFilename = ParsedOpts->get<&InputFilenameOpt>();
  Args.OutputFilename = ParsedOpts->get<&OutputFilenameOpt>();
  Args.ArchName = ParsedOpts->get<&ArchNameOpt>();
  Args.TripleName = ParsedOpts->get<&TripleNameOpt>();
  Args.MCPU = ParsedOpts->get<&MCPUOpt>();
  Args.MAttrs = ParsedOpts->get<&MAttrsOpt>();
  Args.PrintJson = ParsedOpts->get<&PrintJsonOpt>();
  Args.OutputAsmVariant = ParsedOpts->get<&OutputAsmVariantOpt>();
  Args.PrintImmHex = ParsedOpts->get<&PrintImmHexOpt>();
  Args.Iterations = ParsedOpts->get<&IterationsOpt>();
  Args.DispatchWidth = ParsedOpts->get<&DispatchWidthOpt>();
  Args.RegisterFileSize = ParsedOpts->get<&RegisterFileSizeOpt>();
  Args.MicroOpQueue = ParsedOpts->get<&MicroOpQueueOpt>();
  Args.DecoderThroughput = ParsedOpts->get<&DecoderThroughputOpt>();
  Args.CallLatency = ParsedOpts->get<&CallLatencyOpt>();
  Args.SkipUnsupportedInstructions =
      ParsedOpts->get<&SkipUnsupportedInstructionsOpt>();
  Args.PrintRegisterFileStats = ParsedOpts->get<&PrintRegisterFileStatsOpt>();
  Args.PrintDispatchStats = ParsedOpts->get<&PrintDispatchStatsOpt>();
  Args.PrintSummaryView = ParsedOpts->get<&PrintSummaryViewOpt>();
  Args.PrintSchedulerStats = ParsedOpts->get<&PrintSchedulerStatsOpt>();
  Args.PrintRetireStats = ParsedOpts->get<&PrintRetireStatsOpt>();
  Args.PrintResourcePressureView =
      ParsedOpts->get<&PrintResourcePressureViewOpt>();
  Args.PrintTimelineView = ParsedOpts->get<&PrintTimelineViewOpt>();
  Args.TimelineMaxIterations = ParsedOpts->get<&TimelineMaxIterationsOpt>();
  Args.TimelineMaxCycles = ParsedOpts->get<&TimelineMaxCyclesOpt>();
  Args.AssumeNoAlias = ParsedOpts->get<&AssumeNoAliasOpt>();
  Args.LoadQueueSize = ParsedOpts->get<&LoadQueueSizeOpt>();
  Args.StoreQueueSize = ParsedOpts->get<&StoreQueueSizeOpt>();
  Args.InstructionTables = ParsedOpts->get<&InstructionTablesOpt>();
  Args.PrintInstructionInfoView =
      ParsedOpts->get<&PrintInstructionInfoViewOpt>();
  Args.EnableAllStats = ParsedOpts->get<&EnableAllStatsOpt>();
  Args.EnableAllViews = ParsedOpts->get<&EnableAllViewsOpt>();
  Args.EnableBottleneckAnalysis =
      ParsedOpts->get<&EnableBottleneckAnalysisOpt>();
  Args.ShowEncoding = ParsedOpts->get<&ShowEncodingOpt>();
  Args.ShowBarriers = ParsedOpts->get<&ShowBarriersOpt>();
  Args.DisableCustomBehaviour = ParsedOpts->get<&DisableCustomBehaviourOpt>();
  Args.DisableInstrumentManager =
      ParsedOpts->get<&DisableInstrumentManagerOpt>();

  ViewPositions VPos;
  VPos.PrintRegisterFileStats =
      ParsedOpts->position<&PrintRegisterFileStatsOpt>();
  VPos.PrintDispatchStats = ParsedOpts->position<&PrintDispatchStatsOpt>();
  VPos.PrintSummaryView = ParsedOpts->position<&PrintSummaryViewOpt>();
  VPos.PrintSchedulerStats = ParsedOpts->position<&PrintSchedulerStatsOpt>();
  VPos.PrintRetireStats = ParsedOpts->position<&PrintRetireStatsOpt>();
  VPos.PrintResourcePressureView =
      ParsedOpts->position<&PrintResourcePressureViewOpt>();
  VPos.PrintTimelineView = ParsedOpts->position<&PrintTimelineViewOpt>();
  VPos.PrintInstructionInfoView =
      ParsedOpts->position<&PrintInstructionInfoViewOpt>();
  VPos.EnableAllStats = ParsedOpts->position<&EnableAllStatsOpt>();
  VPos.EnableAllViews = ParsedOpts->position<&EnableAllViewsOpt>();
  VPos.EnableBottleneckAnalysis =
      ParsedOpts->position<&EnableBottleneckAnalysisOpt>();

#ifndef NDEBUG
  if (ParsedOpts->get<&DebugOpt>())
    llvm::DebugFlag = true;
#endif

  Triple TheTriple(Args.TripleName.empty()
                       ? Triple::normalize(sys::getDefaultTargetTriple())
                       : Args.TripleName);

  // Get the target from the triple.
  const char *ProgName = argv[0];
  std::string Error;
  const Target *TheTarget =
      TargetRegistry::lookupTarget(Args.ArchName, TheTriple, Error);
  if (!TheTarget) {
    errs() << ProgName << ": " << Error;
    return 1;
  }

  const bool WantsCPUHelp = Args.MCPU == "help";

  std::unique_ptr<MemoryBuffer> InputBuffer;
  if (!WantsCPUHelp) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrErr =
        MemoryBuffer::getFileOrSTDIN(Args.InputFilename);
    if (!BufferOrErr) {
      std::error_code EC = BufferOrErr.getError();
      WithColor::error() << Args.InputFilename << ": " << EC.message() << '\n';
      return 1;
    }
    InputBuffer = std::move(*BufferOrErr);
  }

  if (Args.MCPU == "native")
    Args.MCPU = std::string(llvm::sys::getHostCPUName());

  // Package up features to be passed to target/subtarget
  std::string FeaturesStr;
  if (!Args.MAttrs.empty()) {
    SubtargetFeatures Features;
    for (const std::string &MAttr : Args.MAttrs)
      Features.AddFeature(MAttr);
    FeaturesStr = Features.getString();
  }

  std::unique_ptr<MCSubtargetInfo> STI(TheTarget->createMCSubtargetInfo(
      TheTriple, Args.MCPU, FeaturesStr, *OptsCtx));
  if (!STI) {
    WithColor::error() << "unable to create subtarget info\n";
    return 1;
  }

  if (TheTriple.isAArch64() && STI->checkFeatures("+mca-streaming-sched"))
    WithColor::warning()
        << "AArch64 streaming SVE scheduling is enabled via "
           "'-mattr=+mca-streaming-sched'; llvm-mca results are approximate.\n";

  if (WantsCPUHelp)
    return 0;

  if (!STI->getSchedModel().hasInstrSchedModel()) {
    WithColor::error()
        << "unable to find instruction-level scheduling information for"
        << " target triple '" << TheTriple.normalize() << "' and cpu '"
        << Args.MCPU << "'.\n";

    if (STI->getSchedModel().InstrItineraries)
      WithColor::note()
          << "cpu '" << Args.MCPU << "' provides itineraries. However, "
          << "instruction itineraries are currently unsupported.\n";
    return 1;
  }

  // Apply overrides to llvm-mca specific options.
  bool IsOutOfOrder = STI->getSchedModel().isOutOfOrder();
  processViewOptions(Args, VPos, IsOutOfOrder);

  std::unique_ptr<MCRegisterInfo> MRI(TheTarget->createMCRegInfo(TheTriple));
  assert(MRI && "Unable to create target register info!");

  MCTargetOptions MCOptions = mc::InitMCTargetOptionsFromFlags(*OptsCtx);
  std::unique_ptr<MCAsmInfo> MAI(
      TheTarget->createMCAsmInfo(*MRI, TheTriple, MCOptions));
  assert(MAI && "Unable to create target asm info!");

  SourceMgr SrcMgr;

  // Tell SrcMgr about this buffer, which is what the parser will pick up.
  SrcMgr.AddNewSourceBuffer(std::move(InputBuffer), SMLoc());

  std::unique_ptr<MCInstrInfo> MCII(TheTarget->createMCInstrInfo());
  assert(MCII && "Unable to create instruction info!");

  std::unique_ptr<MCInstrAnalysis> MCIA(
      TheTarget->createMCInstrAnalysis(MCII.get()));

  // Need to initialize an MCInstPrinter as it is
  // required for initializing the MCTargetStreamer
  // which needs to happen within the CRG.parseAnalysisRegions() call below.
  // Without an MCTargetStreamer, certain assembly directives can trigger a
  // segfault. (For example, the .cv_fpo_proc directive on x86 will segfault if
  // we don't initialize the MCTargetStreamer.)
  unsigned IPtempOutputAsmVariant =
      Args.OutputAsmVariant == -1 ? 0 : Args.OutputAsmVariant;
  std::unique_ptr<MCInstPrinter> IPtemp(TheTarget->createMCInstPrinter(
      TheTriple, IPtempOutputAsmVariant, *MAI, *MCII, *MRI));
  if (!IPtemp) {
    WithColor::error()
        << "unable to create instruction printer for target triple '"
        << TheTriple.normalize() << "' with assembly variant "
        << IPtempOutputAsmVariant << ".\n";
    return 1;
  }

  // Parse the input and create CodeRegions that llvm-mca can analyze.
  MCContext ACtx(TheTriple, *MAI, *MRI, *STI, &SrcMgr);
  ACtx.setOptionsContext(*OptsCtx);
  std::unique_ptr<MCObjectFileInfo> AMOFI(
      TheTarget->createMCObjectFileInfo(ACtx, /*PIC=*/false));
  ACtx.setObjectFileInfo(AMOFI.get());
  mca::AsmAnalysisRegionGenerator CRG(*TheTarget, SrcMgr, ACtx, *MAI, *STI,
                                      *MCII);
  Expected<const mca::AnalysisRegions &> RegionsOrErr =
      CRG.parseAnalysisRegions(std::move(IPtemp),
                               shouldSkip(Args.SkipUnsupportedInstructions,
                                          SkipType::PARSE_FAILURE));
  if (!RegionsOrErr) {
    if (auto Err =
            handleErrors(RegionsOrErr.takeError(), [](const StringError &E) {
              WithColor::error() << E.getMessage() << '\n';
            })) {
      // Default case.
      WithColor::error() << toString(std::move(Err)) << '\n';
    }
    return 1;
  }
  const mca::AnalysisRegions &Regions = *RegionsOrErr;

  // Early exit if errors were found by the code region parsing logic.
  if (!Regions.isValid())
    return 1;

  if (Regions.empty()) {
    WithColor::error() << "no assembly instructions found.\n";
    return 1;
  }

  std::unique_ptr<mca::InstrumentManager> IM;
  if (!Args.DisableInstrumentManager) {
    IM = std::unique_ptr<mca::InstrumentManager>(
        TheTarget->createInstrumentManager(*STI, *MCII));
    if (!IM) {
      // If the target doesn't have its own IM implemented we use base class
      // with instruments enabled.
      IM = std::make_unique<mca::InstrumentManager>(*STI, *MCII);
    }
  } else {
    // If the -disable-im flag is set then we use the default base class
    // implementation and disable the instruments.
    IM = std::make_unique<mca::InstrumentManager>(*STI, *MCII,
                                                  /*EnableInstruments=*/false);
  }

  // Parse the input and create InstrumentRegion that llvm-mca
  // can use to improve analysis.
  MCContext ICtx(TheTriple, *MAI, *MRI, *STI, &SrcMgr);
  ICtx.setOptionsContext(*OptsCtx);
  std::unique_ptr<MCObjectFileInfo> IMOFI(
      TheTarget->createMCObjectFileInfo(ICtx, /*PIC=*/false));
  ICtx.setObjectFileInfo(IMOFI.get());
  mca::AsmInstrumentRegionGenerator IRG(*TheTarget, SrcMgr, ICtx, *MAI, *STI,
                                        *MCII, *IM);
  Expected<const mca::InstrumentRegions &> InstrumentRegionsOrErr =
      IRG.parseInstrumentRegions(std::move(IPtemp),
                                 shouldSkip(Args.SkipUnsupportedInstructions,
                                            SkipType::PARSE_FAILURE));
  if (!InstrumentRegionsOrErr) {
    if (auto Err = handleErrors(InstrumentRegionsOrErr.takeError(),
                                [](const StringError &E) {
                                  WithColor::error() << E.getMessage() << '\n';
                                })) {
      // Default case.
      WithColor::error() << toString(std::move(Err)) << '\n';
    }
    return 1;
  }
  const mca::InstrumentRegions &InstrumentRegions = *InstrumentRegionsOrErr;

  // Early exit if errors were found by the instrumentation parsing logic.
  if (!InstrumentRegions.isValid())
    return 1;

  // Now initialize the output file.
  if (Args.OutputFilename.empty())
    Args.OutputFilename = "-";
  std::error_code EC;
  auto TOFPtr = std::make_unique<ToolOutputFile>(Args.OutputFilename, EC,
                                                 sys::fs::OF_TextWithCRLF);
  if (EC) {
    WithColor::error() << EC.message() << '\n';
    return 1;
  }

  unsigned AssemblerDialect = CRG.getAssemblerDialect();
  if (Args.OutputAsmVariant >= 0)
    AssemblerDialect = static_cast<unsigned>(Args.OutputAsmVariant);
  std::unique_ptr<MCInstPrinter> IP(TheTarget->createMCInstPrinter(
      TheTriple, AssemblerDialect, *MAI, *MCII, *MRI));
  if (!IP) {
    WithColor::error()
        << "unable to create instruction printer for target triple '"
        << TheTriple.normalize() << "' with assembly variant "
        << AssemblerDialect << ".\n";
    return 1;
  }

  // Set the display preference for hex vs. decimal immediates.
  IP->setPrintImmHex(Args.PrintImmHex);

  ToolOutputFile &TOF = *TOFPtr;

  const MCSchedModel &SM = STI->getSchedModel();

  std::unique_ptr<mca::InstrPostProcess> IPP;
  if (!Args.DisableCustomBehaviour) {
    IPP = std::unique_ptr<mca::InstrPostProcess>(
        TheTarget->createInstrPostProcess(*STI, *MCII));
  }
  if (!IPP) {
    IPP = std::make_unique<mca::InstrPostProcess>(*STI, *MCII);
  }

  // Create an instruction builder.
  mca::InstrBuilder IB(*STI, *MCII, *MRI, MCIA.get(), *IM, Args.CallLatency);

  // Create a context to control ownership of the pipeline hardware.
  mca::Context MCA(*MRI, *STI);

  mca::PipelineOptions PO(Args.MicroOpQueue, Args.DecoderThroughput,
                          Args.DispatchWidth, Args.RegisterFileSize,
                          Args.LoadQueueSize, Args.StoreQueueSize,
                          Args.AssumeNoAlias, Args.EnableBottleneckAnalysis);

  // Number each region in the sequence.
  unsigned RegionIdx = 0;

  std::unique_ptr<MCCodeEmitter> MCE(
      TheTarget->createMCCodeEmitter(*MCII, ACtx));
  assert(MCE && "Unable to create code emitter!");

  MCTargetOptions MCOptions2 = mc::InitMCTargetOptionsFromFlags(*OptsCtx);
  std::unique_ptr<MCAsmBackend> MAB(
      TheTarget->createMCAsmBackend(*STI, *MRI, MCOptions2));
  assert(MAB && "Unable to create asm backend!");

  json::Object JSONOutput;
  int NonEmptyRegions = 0;
  for (const std::unique_ptr<mca::AnalysisRegion> &Region : Regions) {
    // Skip empty code regions.
    if (Region->empty())
      continue;

    IB.clear();

    // Lower the MCInst sequence into an mca::Instruction sequence.
    ArrayRef<MCInst> Insts = Region->getInstructions();
    mca::CodeEmitter CE(*STI, *MAB, *MCE, Insts);

    IPP->resetState();

    DenseMap<const MCInst *, SmallVector<mca::Instrument *>> InstToInstruments;
    SmallVector<std::unique_ptr<mca::Instruction>> LoweredSequence;
    SmallPtrSet<const MCInst *, 16> DroppedInsts;
    for (const MCInst &MCI : Insts) {
      SMLoc Loc = MCI.getLoc();
      const SmallVector<mca::Instrument *> Instruments =
          InstrumentRegions.getActiveInstruments(Loc);

      Expected<std::unique_ptr<mca::Instruction>> Inst =
          IB.createInstruction(MCI, Instruments);
      if (!Inst) {
        if (auto NewE = handleErrors(
                Inst.takeError(),
                [&IP, &STI, &Args](const mca::InstructionError<MCInst> &IE) {
                  std::string InstructionStr;
                  raw_string_ostream SS(InstructionStr);
                  if (shouldSkip(Args.SkipUnsupportedInstructions,
                                 SkipType::LACK_SCHED))
                    WithColor::warning()
                        << IE.Message
                        << ", skipping with -skip-unsupported-instructions, "
                           "note accuracy will be impacted:\n";
                  else
                    WithColor::error()
                        << IE.Message
                        << ", use -skip-unsupported-instructions=lack-sched to "
                           "ignore these on the input.\n";
                  IP->printInst(&IE.Inst, 0, "", *STI, SS);
                  WithColor::note()
                      << "instruction: " << InstructionStr << '\n';
                })) {
          // Default case.
          WithColor::error() << toString(std::move(NewE));
        }
        if (shouldSkip(Args.SkipUnsupportedInstructions,
                       SkipType::LACK_SCHED)) {
          DroppedInsts.insert(&MCI);
          continue;
        }
        return 1;
      }

      IPP->postProcessInstruction(*Inst.get(), MCI);
      InstToInstruments.insert({&MCI, Instruments});
      LoweredSequence.emplace_back(std::move(Inst.get()));
    }

    Insts = Region->dropInstructions(DroppedInsts);

    // Skip empty regions.
    if (Insts.empty())
      continue;
    NonEmptyRegions++;

    mca::CircularSourceMgr S(
        LoweredSequence, shouldPrintInstructionTables(Args.InstructionTables)
                             ? 1
                             : Args.Iterations);

    if (shouldPrintInstructionTables(Args.InstructionTables)) {
      //  Create a pipeline, stages, and a printer.
      auto P = std::make_unique<mca::Pipeline>();
      P->appendStage(std::make_unique<mca::EntryStage>(S));
      P->appendStage(std::make_unique<mca::InstructionTables>(SM));

      mca::PipelinePrinter Printer(*P, *Region, RegionIdx, *STI, PO);
      if (Args.PrintJson) {
        Printer.addView(
            std::make_unique<mca::InstructionView>(*STI, *IP, Insts));
      }

      // Create the views for this pipeline, execute, and emit a report.
      if (Args.PrintInstructionInfoView) {
        Printer.addView(std::make_unique<mca::InstructionInfoView>(
            *STI, *MCII, CE, Args.ShowEncoding, Insts, *IP, LoweredSequence,
            Args.ShowBarriers,
            shouldPrintInstructionTablesOfType(Args.InstructionTables,
                                               InstructionTablesType::FULL),
            *IM, InstToInstruments));
      }

      if (Args.PrintResourcePressureView)
        Printer.addView(
            std::make_unique<mca::ResourcePressureView>(*STI, *IP, Insts));

      if (!runPipeline(*P))
        return 1;

      if (Args.PrintJson) {
        Printer.printReport(JSONOutput);
      } else {
        Printer.printReport(TOF.os());
      }

      ++RegionIdx;
      continue;
    }

    std::unique_ptr<mca::CustomBehaviour> CB;
    if (!Args.DisableCustomBehaviour)
      CB = std::unique_ptr<mca::CustomBehaviour>(
          TheTarget->createCustomBehaviour(*STI, S, *MCII));
    if (!CB)
      CB = std::make_unique<mca::CustomBehaviour>(*STI, S, *MCII);

    // Create a basic pipeline simulating an out-of-order backend.
    auto P = MCA.createDefaultPipeline(PO, S, *CB);

    mca::PipelinePrinter Printer(*P, *Region, RegionIdx, *STI, PO);

    if (!Args.DisableCustomBehaviour) {
      std::vector<std::unique_ptr<mca::View>> CBViews =
          CB->getStartViews(*IP, Insts);
      for (auto &CBView : CBViews)
        Printer.addView(std::move(CBView));
    }

    if (Args.PrintJson) {
      auto IV = std::make_unique<mca::InstructionView>(*STI, *IP, Insts);
      Printer.addView(std::move(IV));
    }

    if (Args.PrintSummaryView)
      Printer.addView(
          std::make_unique<mca::SummaryView>(SM, Insts, Args.DispatchWidth));

    if (Args.EnableBottleneckAnalysis) {
      if (!IsOutOfOrder) {
        WithColor::warning()
            << "bottleneck analysis is not supported for in-order CPU '"
            << Args.MCPU << "'.\n";
      }
      Printer.addView(std::make_unique<mca::BottleneckAnalysis>(
          *STI, *IP, Insts, S.getNumIterations()));
    }

    if (Args.PrintInstructionInfoView)
      Printer.addView(std::make_unique<mca::InstructionInfoView>(
          *STI, *MCII, CE, Args.ShowEncoding, Insts, *IP, LoweredSequence,
          Args.ShowBarriers, /*ShouldPrintFullInfo=*/false, *IM,
          InstToInstruments));

    if (!Args.DisableCustomBehaviour) {
      std::vector<std::unique_ptr<mca::View>> CBViews =
          CB->getPostInstrInfoViews(*IP, Insts);
      for (auto &CBView : CBViews)
        Printer.addView(std::move(CBView));
    }

    if (Args.PrintDispatchStats)
      Printer.addView(std::make_unique<mca::DispatchStatistics>());

    if (Args.PrintSchedulerStats)
      Printer.addView(std::make_unique<mca::SchedulerStatistics>(*STI));

    if (Args.PrintRetireStats)
      Printer.addView(std::make_unique<mca::RetireControlUnitStatistics>(SM));

    if (Args.PrintRegisterFileStats)
      Printer.addView(std::make_unique<mca::RegisterFileStatistics>(*STI));

    if (Args.PrintResourcePressureView)
      Printer.addView(
          std::make_unique<mca::ResourcePressureView>(*STI, *IP, Insts));

    if (Args.PrintTimelineView) {
      unsigned TimelineIterations =
          Args.TimelineMaxIterations ? Args.TimelineMaxIterations : 10;
      Printer.addView(std::make_unique<mca::TimelineView>(
          *STI, *IP, Insts, std::min(TimelineIterations, S.getNumIterations()),
          Args.TimelineMaxCycles));
    }

    if (!Args.DisableCustomBehaviour) {
      std::vector<std::unique_ptr<mca::View>> CBViews =
          CB->getEndViews(*IP, Insts);
      for (auto &CBView : CBViews)
        Printer.addView(std::move(CBView));
    }

    if (!runPipeline(*P))
      return 1;

    if (Args.PrintJson) {
      Printer.printReport(JSONOutput);
    } else {
      Printer.printReport(TOF.os());
    }

    ++RegionIdx;
  }

  if (NonEmptyRegions == 0) {
    WithColor::error() << "no assembly instructions found.\n";
    return 1;
  }

  if (Args.PrintJson)
    TOF.os() << formatv("{0:2}", json::Value(std::move(JSONOutput))) << "\n";

  TOF.keep();
  return 0;
}
