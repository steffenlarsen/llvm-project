//===- MlirTranslateMain.cpp - MLIR Translation entry point ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Tools/mlir-translate/MlirTranslateMain.h"
#include "mlir/IR/AsmState.h"
#include "mlir/IR/MLIROptionsOptInfos.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Support/FileUtilities.h"
#include "mlir/Support/Timing.h"
#include "mlir/Support/ToolUtilities.h"
#include "mlir/Target/MLIRTranslateOptionsOptInfos.h"
#include "mlir/Tools/mlir-translate/Translation.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/IROptionsOptInfos.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/ToolOutputFile.h"
#include <deque>

using namespace mlir;
using namespace llvm::clv2;

//===----------------------------------------------------------------------===//
// Command-line options
//===----------------------------------------------------------------------===//

static constexpr OptionInfo<std::string> transInputOpt{"", "<input file>",
                                                       Positional{}, Init{"-"}};
static constexpr OptionInfo<std::string> transOutputOpt{
    "o", "Output filename", value_desc("filename"), Init{"-"}};
static constexpr OptionInfo<bool> transAllowUnregisteredOpt{
    "allow-unregistered-dialect",
    "Allow operation with no registered dialects (discouraged: testing only!)"};
static constexpr OptionInfo<std::string> transSplitInputFileOpt{
    "split-input-file",
    "Split the input file into chunks using the given or default marker "
    "and process each chunk independently",
    ValueOptional};

enum class TransVerifyDiagLevel : int { All = 0, OnlyExpected };
static constexpr EnumVal<TransVerifyDiagLevel> transVerifyDiagVals[] = {
    {"all", TransVerifyDiagLevel::All,
     "Check all diagnostics (expected, unexpected, near-misses)"},
    {"", TransVerifyDiagLevel::All,
     "Check all diagnostics (expected, unexpected, near-misses)"},
    {"only-expected", TransVerifyDiagLevel::OnlyExpected,
     "Check only expected diagnostics"},
};
static constexpr auto transVerifyDiagnosticsOpt =
    makeEnumOption<TransVerifyDiagLevel>(
        "verify-diagnostics",
        "Check that emitted diagnostics match expected-* lines on the "
        "corresponding line",
        transVerifyDiagVals, ValueOptional);

static constexpr OptionInfo<bool> transErrorDiagnosticsOnlyOpt{
    "error-diagnostics-only",
    "Filter all non-error diagnostics (discouraged: testing only!)"};
static constexpr OptionInfo<std::string> transOutputSplitMarkerOpt{
    "output-split-marker", "Split marker to use for merging the ouput"};

static constexpr OptionsRegistry<
    &transInputOpt, &transOutputOpt, &transAllowUnregisteredOpt,
    &transSplitInputFileOpt, &transVerifyDiagnosticsOpt,
    &transErrorDiagnosticsOnlyOpt, &transOutputSplitMarkerOpt>
    MlirTranslateToolReg;

// Function-local: see above -- mlir/lib forbids global constructors.
static std::vector<const Translation *> &translationsRequested() {
  static std::vector<const Translation *> V;
  return V;
}

/// Ctx is the Translation this flag selects.
static bool selectTranslation(void *Ctx, const bool &) {
  translationsRequested().push_back(static_cast<const Translation *>(Ctx));
  return true;
}

static void registerTranslationSelectors(OptionParser &P) {
  const auto &registry = getRegisteredTranslations();
  // All translation options share a single occurrence counter so that
  // specifying any one satisfies the OneOrMore check across the group.
  static unsigned SharedCounter = 0;
  // Descriptors are built at runtime (names come from the registry) and are
  // kept in a deque so their addresses stay stable for the parser.
  static std::deque<llvm::clv2::RuntimeOption<bool>> Options;
  // Sort by name for deterministic output.
  SmallVector<std::pair<llvm::StringRef, const Translation *>> sorted;
  for (const auto &kv : registry)
    sorted.push_back({kv.first(), &kv.second});
  llvm::sort(sorted,
             [](const auto &A, const auto &B) { return A.first < B.first; });
  for (size_t I = 0; I < sorted.size(); ++I) {
    const auto &[name, trans] = sorted[I];
    Options.emplace_back(
        name, trans->getDescription(), ValueDisallowed, OneOrMore,
        llvm::clv2::CtxCallback<bool>{&selectTranslation,
                                      const_cast<Translation *>(trans)});
    // Group display has no descriptor spelling, so it is set on the option's
    // own static info.
    Options.back().staticInfo().IsEnumGroupMember = true;
    Options.back().staticInfo().GroupSortKeyOverride = "split-input-filez";
    if (I == 0)
      Options.back().staticInfo().EnumGroupHeader = "Translations to perform";
    llvm::clv2::detail::OptionEntry E = Options.back().makeEntry(SharedCounter);
    P.addDynamicEntry(std::move(E));
  }
}

//===----------------------------------------------------------------------===//
// Diagnostic Filter
//===----------------------------------------------------------------------===//

namespace {
/// A scoped diagnostic handler that marks non-error diagnostics as handled. As
/// a result, the main diagnostic handler does not print non-error diagnostics.
class ErrorDiagnosticFilter : public ScopedDiagnosticHandler {
public:
  ErrorDiagnosticFilter(MLIRContext *ctx) : ScopedDiagnosticHandler(ctx) {
    setHandler([](Diagnostic &diag) {
      if (diag.getSeverity() != DiagnosticSeverity::Error)
        return success();
      return failure();
    });
  }
};
} // namespace

//===----------------------------------------------------------------------===//
// Translate Entry Point
//===----------------------------------------------------------------------===//

LogicalResult mlir::mlirTranslateMain(
    int argc, char **argv, llvm::StringRef toolName,
    std::function<void(llvm::clv2::OptionParser &)> ConfigureParser) {
  llvm::InitLLVM y(argc, argv);

  // Register translation selectors and other options.
  registerAsmPrinterCLOptions();
  registerMLIRContextCLOptions();
  registerTranslationCLOptions();
  registerDefaultTimingManagerCLOptions();
  llvm::clv2::OptionParser P;
  registerTranslationSelectors(P);
  P.add<&MlirTranslateToolReg>();
  P.add<&IROptsReg, llvm::ir_opts::applyIROptions>();
  P.add<&MLIROptsReg>();
  P.add<&MLIRTranslateOptsReg>();
  llvm::RegisterCommonLLVMOptionsHidden(P);
  // mlir-translate showOptions (53 options)
  P.showOptions({
      "allow-unregistered-dialect",
      "cfg-hide-cold-paths",
      "cfg-hide-deoptimize-paths",
      "cfg-hide-unreachable-paths",
      "convert-debug-rec-to-intrinsics",
      "declare-variables-at-top",
      "deserialize-spirv",
      "disable-auto-upgrade-debug-info",
      "disable-i2p-p2i-opt",
      "dot-cfg-mssa",
      "drop-di-composite-type-elements",
      "elide-all-zero-branch-weights",
      "emit-expensive-warnings",
      "error-diagnostics-only",
      "export-smtlib",
      "file-id",
      "generate-merged-base-profiles",
      "import-llvm",
      "import-structs-as-literals",
      "import-wasm",
      "irdl-to-cpp",
      "mlir-disable-threading",
      "mlir-elide-elementsattrs-if-larger",
      "mlir-elide-resource-strings-if-larger",
      "mlir-output-format",
      "mlir-pretty-debuginfo",
      "mlir-print-debuginfo",
      "mlir-print-elementsattrs-with-hex-if-larger",
      "mlir-print-local-scope",
      "mlir-print-op-on-diagnostic",
      "mlir-print-skip-regions",
      "mlir-print-stacktrace-on-diagnostic",
      "mlir-print-unique-ssa-ids",
      "mlir-print-value-users",
      "mlir-timing",
      "mlir-timing-display",
      "mlir-to-cpp",
      "mlir-to-llvmir",
      "mlir-use-nameloc-as-prefix",
      "no-implicit-module",
      "o",
      "object-size-offset-visitor-max-visit-instructions",
      "output-split-marker",
      "prefer-unregistered-intrinsics",
      "serialize-spirv",
      "smtlibexport-inline-single-use-values",
      "spirv-save-validation-files-with-prefix",
      "spirv-structurize-control-flow",
      "split-input-file",
      "test-import-llvmir",
      "test-spirv-roundtrip",
      "test-spirv-roundtrip-debug",
      "test-to-llvmir",
      "verify-diagnostics",
  });

  // Add visible versions of the IR/Analysis/ProfileData options.
  {
    static constexpr llvm::clv2::OptionInfo<bool> V_DisableAutoUpgrade{
        "disable-auto-upgrade-debug-info", "Disable autoupgrade of debug info"};
    static constexpr llvm::clv2::OptionInfo<bool> V_DisableI2pP2i{
        "disable-i2p-p2i-opt",
        "Disables inttoptr/ptrtoint roundtrip optimization"};
    static constexpr llvm::clv2::OptionInfo<bool> V_ElideZeroBW{
        "elide-all-zero-branch-weights", "", llvm::clv2::Init{true}};
    static constexpr llvm::clv2::OptionInfo<float> V_CfgHideCold{
        "cfg-hide-cold-paths",
        "Hide blocks with relative frequency below the given value",
        llvm::clv2::value_desc("number")};
    static constexpr llvm::clv2::OptionInfo<bool> V_CfgHideDeopt{
        "cfg-hide-deoptimize-paths", ""};
    static constexpr llvm::clv2::OptionInfo<bool> V_CfgHideUnreach{
        "cfg-hide-unreachable-paths", ""};
    static constexpr llvm::clv2::OptionInfo<std::string> V_DotCfgMssa{
        "dot-cfg-mssa", "file name for generated dot file",
        llvm::clv2::value_desc("file name for generated dot file")};
    static constexpr llvm::clv2::OptionInfo<bool> V_GenMerged{
        "generate-merged-base-profiles",
        "When generating nested context-sensitive profiles, always generate "
        "extra base profile for function with all its context profiles "
        "merged into it."};
    static constexpr llvm::clv2::OptionInfo<unsigned> V_ObjSizeMax{
        "object-size-offset-visitor-max-visit-instructions",
        "Maximum number of instructions for ObjectSizeOffsetVisitor to "
        "look at"};
    static constexpr llvm::clv2::OptionsRegistry<
        &V_DisableAutoUpgrade, &V_DisableI2pP2i, &V_ElideZeroBW, &V_CfgHideCold,
        &V_CfgHideDeopt, &V_CfgHideUnreach, &V_DotCfgMssa, &V_GenMerged,
        &V_ObjSizeMax>
        VisReg;
    using PT = decltype(VisReg)::ParsedOptionsT;
    auto *S = new PT();
    decltype(VisReg)::applyDefaultsTo(*S);
    std::vector<llvm::clv2::detail::OptionEntry> Es;
    std::vector<llvm::clv2::detail::AliasEntry> As;
    std::vector<llvm::clv2::detail::SubCommandSpec> Ss;
    decltype(VisReg)::staticBuildInto(*S, Es, As, Ss);
    for (auto &E : Es)
      P.addDynamicEntry(std::move(E));
  }
  P.hideOptions({
      "mlir-pass-pipeline-crash-reproducer",
      "mlir-pass-pipeline-local-reproducer",
      "mlir-print-ir-before-all",
      "mlir-print-ir-after-all",
      "mlir-print-ir-after-change",
      "mlir-print-ir-after-failure",
      "mlir-print-ir-module-scope",
      "mlir-print-ir-tree-dir",
      "mlir-pass-statistics",
      "mlir-pass-statistics-display",
      "log-actions-to",
      "profile-actions-to",
      "log-mlir-actions-filter",
      "mlir-enable-debugger-hook",
      "mlir-debug-counter",
      "mlir-print-debug-counter",
      "mlir-print-assume-verified",
      "mlir-print-op-generic",
  });
  if (ConfigureParser)
    ConfigureParser(P);
  auto OptsCtx = P.parse(argc, argv, toolName);
  auto *Opts = OptsCtx->getViewPtr<&MlirTranslateToolReg>();

  // Initialize the timing manager.
  DefaultTimingManager tm;
  applyDefaultTimingManagerCLOptions(tm, *OptsCtx);
  TimingScope timing = tm.getRootScope();

  auto &inputFilename = Opts->get<&transInputOpt>();
  auto &outputFilename = Opts->get<&transOutputOpt>();
  bool allowUnregisteredDialects = Opts->get<&transAllowUnregisteredOpt>();
  bool errorDiagnosticsOnly = Opts->get<&transErrorDiagnosticsOnlyOpt>();
  auto &outputSplitMarker = Opts->get<&transOutputSplitMarkerOpt>();

  bool verifyDiagnosticsWasSet = Opts->specified<&transVerifyDiagnosticsOpt>();
  auto verifyDiagLevel = Opts->get<&transVerifyDiagnosticsOpt>();
  auto verifyDiagnostics = SourceMgrDiagnosticVerifierHandler::Level::All;
  if (verifyDiagnosticsWasSet) {
    if (verifyDiagLevel == TransVerifyDiagLevel::OnlyExpected)
      verifyDiagnostics =
          SourceMgrDiagnosticVerifierHandler::Level::OnlyExpected;
  }

  std::string inputSplitMarker;
  if (Opts->specified<&transSplitInputFileOpt>()) {
    auto marker = Opts->get<&transSplitInputFileOpt>();
    inputSplitMarker =
        marker.empty() ? kDefaultSplitMarker : std::string(marker);
  }

  std::string errorMessage;
  std::unique_ptr<llvm::MemoryBuffer> input;
  if (auto inputAlignment = translationsRequested()[0]->getInputAlignment())
    input = openInputFile(inputFilename, *inputAlignment, &errorMessage);
  else
    input = openInputFile(inputFilename, &errorMessage);
  if (!input) {
    llvm::errs() << errorMessage << "\n";
    return failure();
  }

  auto output = openOutputFile(outputFilename, &errorMessage);
  if (!output) {
    llvm::errs() << errorMessage << "\n";
    return failure();
  }

  // Processes the memory buffer with a new MLIRContext.
  auto processBuffer = [&](std::unique_ptr<llvm::MemoryBuffer> ownedBuffer,
                           raw_ostream &os) {
    // Many of the translations expect a null-terminated buffer while splitting
    // the buffer does not guarantee null-termination. Make a copy of the buffer
    // to ensure null-termination.
    if (!ownedBuffer->getBuffer().ends_with('\0')) {
      ownedBuffer = llvm::MemoryBuffer::getMemBufferCopy(
          ownedBuffer->getBuffer(), ownedBuffer->getBufferIdentifier());
    }
    // Temporary buffers for chained translation processing.
    std::string dataIn;
    std::string dataOut;
    LogicalResult result = LogicalResult::success();

    for (size_t i = 0, e = translationsRequested().size(); i < e; ++i) {
      llvm::raw_ostream *stream;
      llvm::raw_string_ostream dataStream(dataOut);

      if (i == e - 1) {
        // Output last translation to output.
        stream = &os;
      } else {
        // Output translation to temporary data buffer.
        stream = &dataStream;
      }

      const Translation *translationRequested = translationsRequested()[i];
      TimingScope translationTiming =
          timing.nest(translationRequested->getDescription());

      MLIRContext context(*OptsCtx);
      context.allowUnregisteredDialects(allowUnregisteredDialects);
      context.printOpOnDiagnostic(!verifyDiagnosticsWasSet);
      auto sourceMgr = std::make_shared<llvm::SourceMgr>();
      sourceMgr->AddNewSourceBuffer(std::move(ownedBuffer), SMLoc());

      if (verifyDiagnosticsWasSet) {
        // In the diagnostic verification flow, we ignore whether the
        // translation failed (in most cases, it is expected to fail) and we do
        // not filter non-error diagnostics even if `errorDiagnosticsOnly` is
        // set. Instead, we check if the diagnostics were produced as expected.
        SourceMgrDiagnosticVerifierHandler sourceMgrHandler(
            *sourceMgr, &context, verifyDiagnostics);
        (void)(*translationRequested)(sourceMgr, os, &context);
        result = sourceMgrHandler.verify();
      } else if (errorDiagnosticsOnly) {
        SourceMgrDiagnosticHandler sourceMgrHandler(*sourceMgr, &context);
        ErrorDiagnosticFilter diagnosticFilter(&context);
        result = (*translationRequested)(sourceMgr, *stream, &context);
      } else {
        SourceMgrDiagnosticHandler sourceMgrHandler(*sourceMgr, &context);
        result = (*translationRequested)(sourceMgr, *stream, &context);
      }
      if (failed(result))
        return result;

      if (i < e - 1) {
        // If there are further translations, create a new buffer with the
        // output data.
        dataIn = dataOut;
        dataOut.clear();
        ownedBuffer = llvm::MemoryBuffer::getMemBuffer(dataIn);
      }
    }
    return result;
  };

  if (failed(splitAndProcessBuffer(std::move(input), processBuffer,
                                   output->os(), inputSplitMarker,
                                   outputSplitMarker)))
    return failure();

  output->keep();
  return success();
}
