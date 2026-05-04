//===-- llvm-mc.cpp - Machine Code Hacking Driver ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This utility is a simple driver that allows command line hacking on machine
// code.
//
//===----------------------------------------------------------------------===//

#include "Disassembler.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Analysis/AnalysisOptionsOptInfos.h"
#include "llvm/AsmParser/AsmParserOptionsOptInfos.h"
#include "llvm/Bitcode/BitcodeOptionsOptInfos.h"
#include "llvm/Config/Targets.h"
#include "llvm/DWARFCFIChecker/DWARFCFIFunctionFrameAnalyzer.h"
#include "llvm/DWARFCFIChecker/DWARFCFIFunctionFrameStreamer.h"
#include "llvm/IR/IROptionsOptInfos.h"
#include "llvm/LTO/LTOOptionsOptInfos.h"
#if LLVM_HAS_ARC_TARGET
#include "llvm/Target/ARC/ARCOptionsOptInfos.h"
#endif
#if LLVM_HAS_CSKY_TARGET
#include "llvm/Target/CSKY/CSKYOptionsOptInfos.h"
#endif
#if LLVM_HAS_M68K_TARGET
#include "llvm/Target/M68k/M68kOptionsOptInfos.h"
#endif
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCLFI.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCOptionsOptInfos.h"
#include "llvm/MC/MCParser/AsmLexer.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptionsCommandFlags.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/ObjectOptionsOptInfos.h"
#include "llvm/Passes/PassesOptionsOptInfos.h"
#include "llvm/Remarks/RemarksOptionsOptInfos.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/SupportOptions.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/VirtualFileSystem.h"
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
#include "llvm/Transforms/AggressiveInstCombine/AggressiveInstCombineOptionsOptInfos.h"
#include "llvm/Transforms/Coroutines/CoroutinesOptionsOptInfos.h"
#include "llvm/Transforms/IPO/IPOOptionsOptInfos.h"
#include "llvm/Transforms/InstCombine/InstCombineOptionsOptInfos.h"
#include "llvm/Transforms/Instrumentation/InstrumentationOptionsOptInfos.h"
#include "llvm/Transforms/ObjCARC/ObjCARCOptionsOptInfos.h"
#include "llvm/Transforms/Scalar/ScalarOptionsOptInfos.h"
#include "llvm/Transforms/Utils/UtilsOptionsOptInfos.h"
#include "llvm/Transforms/Vectorize/VectorizeOptionsOptInfos.h"
#include <memory>

using namespace llvm;
using namespace llvm::clv2;

namespace llvm {
LLVM_ABI void setX86AsmSyntax(unsigned Dialect);
}

namespace {
static constexpr OptionCategory MCCategory{"MC Options"};

static constexpr OptionInfo<std::string> InputFilenameOpt{
    "input", "<input file>", Positional{}, Init{"-"}, cat(MCCategory)};

static constexpr ListOptionInfo<std::string> InstPrinterOptionsOpt{
    "M", "InstPrinter options", cat(MCCategory)};

static constexpr OptionInfo<std::string> OutputFilenameOpt{
    "o", "Output filename", value_desc("filename"), Init{"-"}, cat(MCCategory)};

static constexpr OptionInfo<std::string> SplitDwarfFileOpt{
    "split-dwarf-file", "DWO output filename", value_desc("filename"),
    cat(MCCategory)};

static constexpr OptionInfo<bool> ShowEncodingOpt{
    "show-encoding", "Show instruction encodings", cat(MCCategory)};

enum DebugCompressionTypeEnum {
  DCT_None = 0,
  DCT_Zlib,
  DCT_Zstd,
};
static constexpr EnumVal<DebugCompressionType> CompressDebugSectionsVals[] = {
    {"none", DebugCompressionType::None, "No compression"},
    {"zlib", DebugCompressionType::Zlib, "Use zlib"},
    {"zstd", DebugCompressionType::Zstd, "Use zstd"},
};
static constexpr OptionInfo<DebugCompressionType> CompressDebugSectionsOpt{
    "compress-debug-sections",
    "Choose DWARF debug sections compression:",
    ValueOptional,
    Init{DebugCompressionType::None},
    ValuesRef(CompressDebugSectionsVals),
    cat(MCCategory)};

static constexpr OptionInfo<bool> ShowInstOpt{
    "show-inst", "Show internal instruction representation", cat(MCCategory)};

static constexpr OptionInfo<bool> ShowInstOperandsOpt{
    "show-inst-operands", "Show instructions operands as parsed",
    cat(MCCategory)};

static constexpr OptionInfo<unsigned> OutputAsmVariantOpt{
    "output-asm-variant", "Syntax variant to use for output printing",
    cat(MCCategory)};

static constexpr OptionInfo<bool> PrintImmHexOpt{
    "print-imm-hex", "Prefer hex format for immediate values", cat(MCCategory)};

static constexpr OptionInfo<bool> HexBytesOpt{
    "hex",
    "Take raw hexadecimal bytes as input for disassembly. Whitespace is "
    "ignored",
    cat(MCCategory)};

static constexpr ListOptionInfo<std::string> DefineSymbolOpt{
    "defsym", "Defines a symbol to be an integer constant", cat(MCCategory)};

static constexpr OptionInfo<bool> PreserveCommentsOpt{
    "preserve-comments", "Preserve Comments in outputted assembly",
    cat(MCCategory)};

static constexpr OptionInfo<unsigned> CommentColumnOpt{
    "comment-column", "Asm comments indentation", Init{40u}, Hidden,
    cat(MCCategory)};

enum OutputFileType { OFT_Null, OFT_AssemblyFile, OFT_ObjectFile };
static constexpr EnumVal<OutputFileType> FileTypeVals[] = {
    {"asm", OFT_AssemblyFile, "Emit an assembly ('.s') file"},
    {"null", OFT_Null, "Don't emit anything (for timing purposes)"},
    {"obj", OFT_ObjectFile, "Emit a native object ('.o') file"},
};
static constexpr OptionInfo<OutputFileType> FileTypeOpt{
    "filetype", "Choose an output file type:", Init{OFT_AssemblyFile},
    ValuesRef(FileTypeVals), cat(MCCategory)};

static constexpr ListOptionInfo<std::string> IncludeDirsOpt{
    "I", "Directory of include files", value_desc("directory"), PrefixFormat,
    cat(MCCategory)};

static constexpr OptionInfo<std::string> ArchNameOpt{
    "arch", "Target arch to assemble for, see -version for available targets",
    cat(MCCategory)};

static constexpr OptionInfo<std::string> TripleNameOpt{
    "triple",
    "Target triple to assemble for, see -version for available targets",
    cat(MCCategory)};

static constexpr OptionInfo<std::string> MCPUOpt{
    "mcpu", "Target a specific cpu type (-mcpu=help for details)",
    value_desc("cpu-name"), cat(MCCategory)};

static constexpr ListOptionInfo<std::string> MAttrsOpt{
    "mattr", "Target specific attributes (-mattr=help for details)",
    value_desc("a1,+a2,-a3,..."), CommaSeparated, cat(MCCategory)};

static constexpr OptionInfo<bool> PICOpt{
    "position-independent", "Position independent", cat(MCCategory)};

static constexpr OptionInfo<bool> LargeCodeModelOpt{
    "large-code-model",
    "Create cfi directives that assume the code might be more than 2gb away",
    cat(MCCategory)};

static constexpr OptionInfo<bool> NoInitialTextSectionOpt{
    "n", "Don't assume assembly file starts in the text section",
    cat(MCCategory)};

static constexpr OptionInfo<bool> GenDwarfForAssemblyOpt{
    "g", "Generate dwarf debugging info for assembly source files",
    cat(MCCategory)};

static constexpr OptionInfo<std::string> DebugCompilationDirOpt{
    "fdebug-compilation-dir", "Specifies the debug info's compilation dir",
    cat(MCCategory)};

static constexpr ListOptionInfo<std::string> DebugPrefixMapOpt{
    "fdebug-prefix-map", "Map file source paths in debug info",
    value_desc("= separated key-value pairs"), cat(MCCategory)};

static constexpr OptionInfo<std::string> MainFileNameOpt{
    "main-file-name", "Specifies the name we should consider the input file",
    cat(MCCategory)};

static constexpr OptionInfo<bool> LexMasmIntegersOpt{
    "masm-integers", "Enable binary and hex masm integers (0b110 and 0ABCh)",
    cat(MCCategory)};

static constexpr OptionInfo<bool> LexMasmHexFloatsOpt{
    "masm-hexfloats", "Enable MASM-style hex float initializers (3F800000r)",
    cat(MCCategory)};

static constexpr OptionInfo<bool> LexMotorolaIntegersOpt{
    "motorola-integers",
    "Enable binary and hex Motorola integers (%110 and $ABC)", cat(MCCategory)};

static constexpr OptionInfo<bool> NoExecStackOpt{
    "no-exec-stack", "File doesn't need an exec stack", cat(MCCategory)};

static constexpr OptionInfo<bool> ValidateCFIOpt{
    "validate-cfi", "Validate the CFI directives", cat(MCCategory)};

enum ActionType {
  AC_AsLex,
  AC_Assemble,
  AC_Disassemble,
  AC_MDisassemble,
  AC_CDisassemble,
};
static constexpr EnumVal<ActionType> ActionVals[] = {
    {"as-lex", AC_AsLex, "Lex tokens from a .s file"},
    {"assemble", AC_Assemble, "Assemble a .s file (default)"},
    {"disassemble", AC_Disassemble, "Disassemble strings of hex bytes"},
    {"mdis", AC_MDisassemble, "Marked up disassembly of strings of hex bytes"},
    {"cdis", AC_CDisassemble, "Colored disassembly of strings of hex bytes"},
};
static constexpr OptionInfo<ActionType> ActionOpt{
    "", "Action to perform:", Init{AC_Assemble}, ValuesRef(ActionVals),
    cat(MCCategory)};

// Legacy standalone action flags are now generated by the unnamed enum
// ActionOpt above (buildStandaloneEnumEntries).

static constexpr OptionInfo<unsigned> NumBenchmarkRunsOpt{
    "runs", "Number of runs for benchmarking", cat(MCCategory)};

static constexpr OptionInfo<bool> TimeTraceOpt{
    "time-trace", "Record time trace", Hidden, cat(MCCategory)};

static constexpr OptionInfo<unsigned> TimeTraceGranularityOpt{
    "time-trace-granularity",
    "Minimum time granularity (in microseconds) traced by time profiler",
    Init{500u}, Hidden, cat(MCCategory)};

static constexpr OptionInfo<std::string> TimeTraceFileOpt{
    "time-trace-file", "Specify time trace file destination", Hidden,
    cat(MCCategory)};

static constexpr OptionsRegistry<
    &InputFilenameOpt, &InstPrinterOptionsOpt, &OutputFilenameOpt,
    &SplitDwarfFileOpt, &ShowEncodingOpt, &CompressDebugSectionsOpt,
    &ShowInstOpt, &ShowInstOperandsOpt, &OutputAsmVariantOpt, &PrintImmHexOpt,
    &HexBytesOpt, &DefineSymbolOpt, &PreserveCommentsOpt, &CommentColumnOpt,
    &FileTypeOpt, &IncludeDirsOpt, &ArchNameOpt, &TripleNameOpt, &MCPUOpt,
    &MAttrsOpt, &PICOpt, &LargeCodeModelOpt, &NoInitialTextSectionOpt,
    &GenDwarfForAssemblyOpt, &DebugCompilationDirOpt, &DebugPrefixMapOpt,
    &MainFileNameOpt, &LexMasmIntegersOpt, &LexMasmHexFloatsOpt,
    &LexMotorolaIntegersOpt, &NoExecStackOpt, &ValidateCFIOpt, &ActionOpt,
    &NumBenchmarkRunsOpt, &TimeTraceOpt, &TimeTraceGranularityOpt,
    &TimeTraceFileOpt>
    MCToolReg;

// Registries parsed by this tool, with their bridge functions.
static void configureMCRegistries(clv2::OptionParser &P) {
  // Only registries for libraries llvm-mc actually links (see
  // LLVM_LINK_COMPONENTS): MC, MCParser, Support and the target descriptions.
  // It previously also registered the optimizer's registries, which it cannot
  // use -- ~1.7k options parsed on every run for a tool that exposes 49.
  P.add<&MCToolReg>();
  P.add<&MCOptsReg>();
  P.add<&SupportOptsReg, support::applySupportOptions>();
  P.add<&RemarksOptsReg>();
  P.add<&ObjectOptsReg>();
  P.add<&AsmParserOptsReg>();
  P.add<&IROptsReg>();
#if LLVM_HAS_ARC_TARGET
  P.add<&clv2::ARCOptsReg>();
#endif
#if LLVM_HAS_CSKY_TARGET
  P.add<&clv2::CSKYOptsReg>();
#endif
#if LLVM_HAS_M68K_TARGET
  P.add<&clv2::M68kOptsReg>();
#endif
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
}
} // namespace

struct MCArgs {
  std::string InputFilename;
  std::vector<std::string> InstPrinterOptions;
  std::string OutputFilename;
  std::string SplitDwarfFile;
  bool ShowEncoding;
  DebugCompressionType CompressDebugSections;
  bool CompressDebugSectionsSpecified;
  bool ShowInst;
  bool ShowInstOperands;
  unsigned OutputAsmVariant;
  bool OutputAsmVariantSpecified;
  bool PrintImmHex;
  bool HexBytes;
  std::vector<std::string> DefineSymbol;
  bool PreserveComments;
  unsigned CommentColumn;
  OutputFileType FileType;
  std::vector<std::string> IncludeDirs;
  std::string ArchName;
  std::string TripleName;
  std::string MCPU;
  std::vector<std::string> MAttrs;
  bool PIC;
  bool LargeCodeModel;
  bool NoInitialTextSection;
  bool GenDwarfForAssembly;
  std::string DebugCompilationDir;
  std::vector<std::string> DebugPrefixMap;
  std::string MainFileName;
  bool LexMasmIntegers;
  bool LexMasmHexFloats;
  bool LexMotorolaIntegers;
  bool NoExecStack;
  bool ValidateCFI;
  clv2::X86AsmSyntaxKind X86Syntax;
  bool X86SyntaxSpecified;
  ActionType Action;
  unsigned NumBenchmarkRuns;
  bool TimeTrace;
  unsigned TimeTraceGranularity;
  std::string TimeTraceFile;
};

static std::string DwarfDebugFlags;
static void setDwarfDebugFlags(int argc, char **argv) {
  if (!getenv("RC_DEBUG_OPTIONS"))
    return;
  for (int i = 0; i < argc; i++) {
    DwarfDebugFlags += argv[i];
    if (i + 1 < argc)
      DwarfDebugFlags += " ";
  }
}

static std::string DwarfDebugProducer;
static void setDwarfDebugProducer() {
  if (!getenv("DEBUG_PRODUCER"))
    return;
  DwarfDebugProducer += getenv("DEBUG_PRODUCER");
}

static int AsLexInput(SourceMgr &SrcMgr, MCAsmInfo &MAI, raw_ostream &OS) {
  AsmLexer Lexer(MAI);
  Lexer.setBuffer(SrcMgr.getMemoryBuffer(SrcMgr.getMainFileID())->getBuffer());

  bool Error = false;
  while (Lexer.Lex().isNot(AsmToken::Eof)) {
    Lexer.getTok().dump(OS);
    OS << "\n";
    if (Lexer.getTok().getKind() == AsmToken::Error)
      Error = true;
  }

  return Error;
}

static int
fillCommandLineSymbols(MCAsmParser &Parser,
                       const std::vector<std::string> &DefineSymbol) {
  for (auto &I : DefineSymbol) {
    auto Pair = StringRef(I).split('=');
    auto Sym = Pair.first;
    auto Val = Pair.second;

    if (Sym.empty() || Val.empty()) {
      WithColor::error() << "defsym must be of the form: sym=value: " << I
                         << "\n";
      return 1;
    }
    int64_t Value;
    if (Val.getAsInteger(0, Value)) {
      WithColor::error() << "value is not an integer: " << Val << "\n";
      return 1;
    }
    Parser.getContext().setSymbolValue(Parser.getStreamer(), Sym, Value);
  }
  return 0;
}

static int AssembleInput(const char *ProgName, const Target *TheTarget,
                         SourceMgr &SrcMgr, MCContext &Ctx, MCStreamer &Str,
                         MCAsmInfo &MAI, MCSubtargetInfo &STI,
                         MCInstrInfo &MCII, MCTargetOptions const &MCOptions,
                         const MCArgs &Args) {
  std::unique_ptr<MCAsmParser> Parser(
      createMCAsmParser(SrcMgr, Ctx, Str, MAI));
  std::unique_ptr<MCTargetAsmParser> TAP(
      TheTarget->createMCAsmParser(STI, *Parser, MCII));

  if (!TAP) {
    WithColor::error(errs(), ProgName)
        << "this target does not support assembly parsing.\n";
    return 1;
  }

  int SymbolResult = fillCommandLineSymbols(*Parser, Args.DefineSymbol);
  if (SymbolResult)
    return SymbolResult;
  Parser->setShowParsedOperands(Args.ShowInstOperands);
  Parser->setTargetParser(*TAP);
  Parser->getLexer().setLexMasmIntegers(Args.LexMasmIntegers);
  Parser->getLexer().setLexMasmHexFloats(Args.LexMasmHexFloats);
  Parser->getLexer().setLexMotorolaIntegers(Args.LexMotorolaIntegers);

  int Res = Parser->Run(Args.NoInitialTextSection);

  return Res;
}

static std::unique_ptr<ToolOutputFile>
GetOutputStream(StringRef Path, sys::fs::OpenFlags Flags) {
  std::error_code EC;
  auto Out = std::make_unique<ToolOutputFile>(Path, EC, Flags);
  if (EC) {
    WithColor::error() << EC.message() << '\n';
    return nullptr;
  }
  return Out;
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmParsers();
  llvm::InitializeAllDisassemblers();
  // Hide all library options.

  clv2::OptionParser P;
  configureMCRegistries(P);
  P.enableGlobalDynamicEntries();
  P.hideUnrelatedOptions({&MCCategory, &clv2::ColorOptionsCategory});
  auto OptsCtxOwner = P.parse(argc, argv, "llvm machine code playground\n",
                              /*Errs=*/nullptr);
  const auto &OptsCtx = *OptsCtxOwner;
  const auto *O = OptsCtx.getViewPtr<&MCToolReg>();

  MCArgs Args;
  Args.InputFilename = O->get<&InputFilenameOpt>();
  Args.InstPrinterOptions = O->get<&InstPrinterOptionsOpt>();
  Args.OutputFilename = O->get<&OutputFilenameOpt>();
  Args.SplitDwarfFile = O->get<&SplitDwarfFileOpt>();
  Args.ShowEncoding = O->get<&ShowEncodingOpt>();
  Args.CompressDebugSections = O->get<&CompressDebugSectionsOpt>();
  Args.CompressDebugSectionsSpecified =
      O->specified<&CompressDebugSectionsOpt>();
  Args.ShowInst = O->get<&ShowInstOpt>();
  Args.ShowInstOperands = O->get<&ShowInstOperandsOpt>();
  Args.OutputAsmVariant = O->get<&OutputAsmVariantOpt>();
  Args.OutputAsmVariantSpecified = O->specified<&OutputAsmVariantOpt>();
  Args.PrintImmHex = O->get<&PrintImmHexOpt>();
  Args.HexBytes = O->get<&HexBytesOpt>();
  Args.DefineSymbol = O->get<&DefineSymbolOpt>();
  Args.PreserveComments = O->get<&PreserveCommentsOpt>();
  Args.CommentColumn = O->get<&CommentColumnOpt>();
  Args.FileType = O->get<&FileTypeOpt>();
  Args.IncludeDirs = O->get<&IncludeDirsOpt>();
  Args.ArchName = O->get<&ArchNameOpt>();
  Args.TripleName = O->get<&TripleNameOpt>();
  Args.MCPU = O->get<&MCPUOpt>();
  Args.MAttrs = O->get<&MAttrsOpt>();
  Args.PIC = O->get<&PICOpt>();
  Args.LargeCodeModel = O->get<&LargeCodeModelOpt>();
  Args.NoInitialTextSection = O->get<&NoInitialTextSectionOpt>();
  Args.GenDwarfForAssembly = O->get<&GenDwarfForAssemblyOpt>();
  Args.DebugCompilationDir = O->get<&DebugCompilationDirOpt>();
  Args.DebugPrefixMap = O->get<&DebugPrefixMapOpt>();
  Args.MainFileName = O->get<&MainFileNameOpt>();
  Args.LexMasmIntegers = O->get<&LexMasmIntegersOpt>();
  Args.LexMasmHexFloats = O->get<&LexMasmHexFloatsOpt>();
  Args.LexMotorolaIntegers = O->get<&LexMotorolaIntegersOpt>();
  Args.NoExecStack = O->get<&NoExecStackOpt>();
  Args.ValidateCFI = O->get<&ValidateCFIOpt>();
  if (auto *X86O = OptsCtx.getViewPtr<&X86OptsReg>()) {
    Args.X86Syntax = X86O->get<&X86_AsmSyntax>();
    Args.X86SyntaxSpecified = X86O->specified<&X86_AsmSyntax>();
  }
  Args.Action = O->get<&ActionOpt>();
  Args.NumBenchmarkRuns = O->get<&NumBenchmarkRunsOpt>();
  Args.TimeTrace = O->get<&TimeTraceOpt>();
  Args.TimeTraceGranularity = O->get<&TimeTraceGranularityOpt>();
  Args.TimeTraceFile = O->get<&TimeTraceFileOpt>();

  // ShowMCInst is handled below when creating MCOptions (line
  // MCOptions.ShowMCInst = Args.ShowInst).

  if (Args.TimeTrace)
    timeTraceProfilerInitialize(Args.TimeTraceGranularity, argv[0]);

  llvm::scope_exit TimeTraceScopeExit([&Args]() {
    if (!Args.TimeTrace)
      return;
    if (auto E =
            timeTraceProfilerWrite(Args.TimeTraceFile, Args.OutputFilename)) {
      logAllUnhandledErrors(std::move(E), errs());
      return;
    }
    timeTraceProfilerCleanup();
  });

  MCTargetOptions MCOptions = mc::InitMCTargetOptionsFromFlags(OptsCtx);
  MCOptions.CompressDebugSections = Args.CompressDebugSections;
  MCOptions.ShowMCInst = Args.ShowInst;
  MCOptions.AsmVerbose = true;
  MCOptions.MCNoExecStack = Args.NoExecStack;
  MCOptions.MCUseDwarfDirectory = MCTargetOptions::EnableDwarfDirectory;
  MCOptions.InstPrinterOptions = Args.InstPrinterOptions;
  if (Args.X86SyntaxSpecified) {
    llvm::setX86AsmSyntax(static_cast<unsigned>(Args.X86Syntax));
    if (!Args.OutputAsmVariantSpecified)
      MCOptions.OutputAsmVariant = static_cast<int>(Args.X86Syntax);
  }
  if (Args.OutputAsmVariantSpecified)
    MCOptions.OutputAsmVariant = Args.OutputAsmVariant;

  setDwarfDebugFlags(argc, argv);
  setDwarfDebugProducer();

  // Figure out the target triple.
  std::string TripleName = Args.TripleName;
  if (TripleName.empty())
    TripleName = sys::getDefaultTargetTriple();
  Triple TheTriple(Triple::normalize(TripleName));

  // Get the target specific parser.
  const char *ProgName = argv[0];
  std::string Error;
  const Target *TheTarget =
      TargetRegistry::lookupTarget(Args.ArchName, TheTriple, Error);
  if (!TheTarget) {
    WithColor::error(errs(), ProgName) << Error;
    return 1;
  }
  // Update triple to the resolved one.
  TripleName = TheTriple.getTriple();

  ErrorOr<std::unique_ptr<MemoryBuffer>> BufferPtr =
      MemoryBuffer::getFileOrSTDIN(Args.InputFilename, /*IsText=*/true);
  if (std::error_code EC = BufferPtr.getError()) {
    WithColor::error(errs(), ProgName)
        << Args.InputFilename << ": " << EC.message() << '\n';
    return 1;
  }
  MemoryBuffer *Buffer = BufferPtr->get();

  SourceMgr SrcMgr;

  // Tell SrcMgr about this buffer, which is what the parser will pick up.
  SrcMgr.AddNewSourceBuffer(std::move(*BufferPtr), SMLoc());

  // Record the location of the include directories so that the lexer can find
  // it later.
  SrcMgr.setIncludeDirs(Args.IncludeDirs);
  SrcMgr.setVirtualFileSystem(vfs::getRealFileSystem());

  std::unique_ptr<MCRegisterInfo> MRI(TheTarget->createMCRegInfo(TheTriple));
  assert(MRI && "Unable to create target register info!");

  std::unique_ptr<MCAsmInfo> MAI(
      TheTarget->createMCAsmInfo(*MRI, TheTriple, MCOptions));
  assert(MAI && "Unable to create target asm info!");

  if (Args.CompressDebugSections != DebugCompressionType::None) {
    if (const char *Reason = compression::getReasonIfUnsupported(
            compression::formatFor(Args.CompressDebugSections))) {
      WithColor::error(errs(), ProgName)
          << "--compress-debug-sections: " << Reason;
      return 1;
    }
  }
  MAI->setPreserveAsmComments(Args.PreserveComments);
  MAI->setCommentColumn(Args.CommentColumn);

  // Package up features to be passed to target/subtarget
  SubtargetFeatures Features;
  std::string FeaturesStr;

  // Replace -mcpu=native with Host CPU and features.
  std::string MCPU = Args.MCPU;
  if (MCPU == "native") {
    MCPU = std::string(llvm::sys::getHostCPUName());

    llvm::StringMap<bool> TargetFeatures = llvm::sys::getHostCPUFeatures();
    for (auto const &[FeatureName, IsSupported] : TargetFeatures)
      Features.AddFeature(FeatureName, IsSupported);
  }

  // Handle features passed to target/subtarget.
  for (unsigned i = 0; i != Args.MAttrs.size(); ++i)
    Features.AddFeature(Args.MAttrs[i]);
  FeaturesStr = Features.getString();

  std::unique_ptr<MCSubtargetInfo> STI(TheTarget->createMCSubtargetInfo(
      TheTriple, MCPU, FeaturesStr, *&OptsCtx));
  if (!STI) {
    WithColor::error(errs(), ProgName) << "unable to create subtarget info\n";
    return 1;
  }

  // FIXME: This is not pretty. MCContext has a ptr to MCObjectFileInfo and
  // MCObjectFileInfo needs a MCContext reference in order to initialize itself.
  MCContext Ctx(TheTriple, *MAI, *MRI, *STI, &SrcMgr);
  Ctx.setOptionsContext(*&OptsCtx);
  std::unique_ptr<MCObjectFileInfo> MOFI(
      TheTarget->createMCObjectFileInfo(Ctx, Args.PIC, Args.LargeCodeModel));
  Ctx.setObjectFileInfo(MOFI.get());

  Ctx.setGenDwarfForAssembly(Args.GenDwarfForAssembly);
  // Default to 4 for dwarf version.
  unsigned DwarfVersion = MCOptions.DwarfVersion ? MCOptions.DwarfVersion : 4;
  if (DwarfVersion < 2 || DwarfVersion > 6) {
    errs() << ProgName << ": Dwarf version " << DwarfVersion
           << " is not supported." << '\n';
    return 1;
  }
  Ctx.setDwarfVersion(DwarfVersion);
  if (MCOptions.Dwarf64) {
    // The 64-bit DWARF format was introduced in DWARFv3.
    if (DwarfVersion < 3) {
      errs() << ProgName
             << ": the 64-bit DWARF format is not supported for DWARF versions "
                "prior to 3\n";
      return 1;
    }
    // 32-bit targets don't support DWARF64, which requires 64-bit relocations.
    if (MAI->getCodePointerSize() < 8) {
      errs() << ProgName
             << ": the 64-bit DWARF format is only supported for 64-bit "
                "targets\n";
      return 1;
    }
    // If needsDwarfSectionOffsetDirective is true, we would eventually call
    // MCStreamer::emitSymbolValue() with IsSectionRelative = true, but that
    // is supported only for 4-byte long references.
    if (MAI->needsDwarfSectionOffsetDirective()) {
      errs() << ProgName << ": the 64-bit DWARF format is not supported for "
             << TheTriple.normalize() << "\n";
      return 1;
    }
    Ctx.setDwarfFormat(dwarf::DWARF64);
  }
  if (!DwarfDebugFlags.empty())
    Ctx.setDwarfDebugFlags(StringRef(DwarfDebugFlags));
  if (!DwarfDebugProducer.empty())
    Ctx.setDwarfDebugProducer(StringRef(DwarfDebugProducer));
  if (!Args.DebugCompilationDir.empty())
    Ctx.setCompilationDir(Args.DebugCompilationDir);
  else {
    // If no compilation dir is set, try to use the current directory.
    SmallString<128> CWD;
    if (!sys::fs::current_path(CWD))
      Ctx.setCompilationDir(CWD);
  }
  for (const auto &Arg : Args.DebugPrefixMap) {
    const auto &KV = StringRef(Arg).split('=');
    Ctx.addDebugPrefixMapEntry(std::string(KV.first), std::string(KV.second));
  }
  if (!Args.MainFileName.empty())
    Ctx.setMainFileName(Args.MainFileName);
  if (Args.GenDwarfForAssembly)
    Ctx.setGenDwarfRootFile(Args.InputFilename, Buffer->getBuffer());

  sys::fs::OpenFlags Flags = (Args.FileType == OFT_AssemblyFile)
                                 ? sys::fs::OF_TextWithCRLF
                                 : sys::fs::OF_None;
  std::unique_ptr<ToolOutputFile> Out =
      GetOutputStream(Args.OutputFilename, Flags);
  if (!Out)
    return 1;

  std::unique_ptr<ToolOutputFile> DwoOut;
  if (!Args.SplitDwarfFile.empty()) {
    if (Args.FileType != OFT_ObjectFile) {
      WithColor::error() << "dwo output only supported with object files\n";
      return 1;
    }
    DwoOut = GetOutputStream(Args.SplitDwarfFile, sys::fs::OF_None);
    if (!DwoOut)
      return 1;
  }

  std::unique_ptr<buffer_ostream> BOS;
  raw_pwrite_stream *OS = &Out->os();
  std::unique_ptr<MCStreamer> Str;

  std::unique_ptr<MCInstrInfo> MCII(TheTarget->createMCInstrInfo());
  assert(MCII && "Unable to create instruction info!");

  std::unique_ptr<MCInstPrinter> IP;
  if (Args.ValidateCFI) {
    // TODO: The DWARF CFI checker support for emitting anything other than
    // errors and warnings has not been implemented yet. Because of this, it is
    // assert-checked that the filetype output is null.
    assert(Args.FileType == OFT_Null);
    auto FFA = std::make_unique<CFIFunctionFrameAnalyzer>(Ctx, *MCII);
    auto FFS = std::make_unique<CFIFunctionFrameStreamer>(Ctx, std::move(FFA));
    TheTarget->createNullTargetStreamer(*FFS);
    Str = std::move(FFS);
  } else if (Args.FileType == OFT_AssemblyFile) {
    unsigned AsmVariant = MAI->getOutputAssemblerDialect();
    IP.reset(TheTarget->createMCInstPrinter(Triple(TripleName), AsmVariant,
                                            *MAI, *MCII, *MRI));

    if (!IP) {
      WithColor::error()
          << "unable to create instruction printer for target triple '"
          << TheTriple.normalize() << "' with assembly variant " << AsmVariant
          << "\n";
      return 1;
    }
    IP->setOptionsContext(*&OptsCtx);

    for (StringRef Opt : Args.InstPrinterOptions)
      if (!IP->applyTargetSpecificCLOption(Opt)) {
        WithColor::error() << "invalid InstPrinter option '" << Opt << "'\n";
        return 1;
      }

    // Set the display preference for hex vs. decimal immediates.
    IP->setPrintImmHex(Args.PrintImmHex);

    switch (Args.Action) {
    case AC_MDisassemble:
      IP->setUseMarkup(true);
      break;
    case AC_CDisassemble:
      IP->setUseColor(true);
      break;
    default:
      break;
    }

    // Set up the AsmStreamer.
    std::unique_ptr<MCCodeEmitter> CE;
    if (Args.ShowEncoding)
      CE.reset(TheTarget->createMCCodeEmitter(*MCII, Ctx));

    std::unique_ptr<MCAsmBackend> MAB(
        TheTarget->createMCAsmBackend(*STI, *MRI, MCOptions));
    auto FOut = std::make_unique<formatted_raw_ostream>(*OS);
    Str.reset(TheTarget->createAsmStreamer(Ctx, std::move(FOut), std::move(IP),
                                           std::move(CE), std::move(MAB)));

    Triple T(TripleName);
    if (T.isLFI())
      initializeLFIMCStreamer(*Str.get(), Ctx, T);
  } else if (Args.FileType == OFT_Null) {
    Str.reset(TheTarget->createNullStreamer(Ctx));
  } else {
    assert(Args.FileType == OFT_ObjectFile && "Invalid file type!");

    if (!Out->os().supportsSeeking()) {
      BOS = std::make_unique<buffer_ostream>(Out->os());
      OS = BOS.get();
    }

    MCCodeEmitter *CE = TheTarget->createMCCodeEmitter(*MCII, Ctx);
    MCAsmBackend *MAB = TheTarget->createMCAsmBackend(*STI, *MRI, MCOptions);
    Str.reset(TheTarget->createMCObjectStreamer(
        TheTriple, Ctx, std::unique_ptr<MCAsmBackend>(MAB),
        DwoOut ? MAB->createDwoObjectWriter(*OS, DwoOut->os())
               : MAB->createObjectWriter(*OS),
        std::unique_ptr<MCCodeEmitter>(CE), *STI));
    Str->emitVersionForTarget(TheTriple, VersionTuple(), nullptr,
                              VersionTuple());
  }

  int Res = 1;
  bool disassemble = false;
  switch (Args.Action) {
  case AC_AsLex:
    Res = AsLexInput(SrcMgr, *MAI, Out->os());
    break;
  case AC_Assemble:
    Res = AssembleInput(ProgName, TheTarget, SrcMgr, Ctx, *Str, *MAI, *STI,
                        *MCII, MCOptions, Args);
    break;
  case AC_MDisassemble:
  case AC_CDisassemble:
  case AC_Disassemble:
    disassemble = true;
    break;
  }
  if (disassemble)
    Res = Disassembler::disassemble(*TheTarget, *STI, *Str, *Buffer, SrcMgr,
                                    Ctx, Args.HexBytes, Args.NumBenchmarkRuns);

  // Keep output if no errors.
  if (Res == 0) {
    Out->keep();
    if (DwoOut)
      DwoOut->keep();
  }

  return Res;
}
