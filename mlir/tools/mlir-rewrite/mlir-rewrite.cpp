//===- mlir-rewrite.cpp - MLIR Rewrite Driver -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Main entry function for mlir-rewrite.
//
//===----------------------------------------------------------------------===//

#include "mlir/AsmParser/AsmParser.h"
#include "mlir/AsmParser/AsmParserState.h"
#include "mlir/IR/AsmState.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/InitAllDialects.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Support/FileUtilities.h"
#include "mlir/Tools/ParseUtilities.h"
#include "llvm/ADT/RewriteBuffer.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/ToolOutputFile.h"
#include <deque>

using namespace mlir;
using namespace llvm::clv2;

namespace mlir {
using OperationDefinition = AsmParserState::OperationDefinition;

/// Return the source code associated with the OperationDefinition.
static SMRange getOpRange(const OperationDefinition &op) {
  const char *startOp = op.scopeLoc.Start.getPointer();
  const char *endOp = op.scopeLoc.End.getPointer();

  for (const auto &res : op.resultGroups) {
    SMRange range = res.definition.loc;
    startOp = std::min(startOp, range.Start.getPointer());
  }
  return {SMLoc::getFromPointer(startOp), SMLoc::getFromPointer(endOp)};
}

/// Helper to simplify rewriting the source file.
class RewritePad {
public:
  static std::unique_ptr<RewritePad> init(StringRef inputFilename,
                                          StringRef outputFilename);

  /// Return the context the file was parsed into.
  MLIRContext *getContext() { return &context; }

  /// Return the OperationDefinition's of the operation's parsed.
  iterator_range<AsmParserState::OperationDefIterator> getOpDefs() {
    return asmState.getOpDefs();
  }

  /// Insert the specified string at the specified location in the original
  /// buffer.
  void insertText(SMLoc pos, StringRef str, bool insertAfter = true) {
    rewriteBuffer.InsertText(pos.getPointer() - start, str, insertAfter);
  }

  /// Replace the range of the source text with the corresponding string in the
  /// output.
  void replaceRange(SMRange range, StringRef str) {
    rewriteBuffer.ReplaceText(range.Start.getPointer() - start,
                              range.End.getPointer() - range.Start.getPointer(),
                              str);
  }

  /// Replace the range of the operation in the source text with the
  /// corresponding string in the output.
  void replaceDef(const OperationDefinition &opDef, StringRef newDef) {
    replaceRange(getOpRange(opDef), newDef);
  }

  /// Return the source string corresponding to the source range.
  StringRef getSourceString(SMRange range) {
    return StringRef(range.Start.getPointer(),
                     range.End.getPointer() - range.Start.getPointer());
  }

  /// Return the source string corresponding to operation definition.
  StringRef getSourceString(const OperationDefinition &opDef) {
    auto range = getOpRange(opDef);
    return getSourceString(range);
  }

  /// Write to stream the result of applying all changes to the
  /// original buffer.
  /// Note that it isn't safe to use this function to overwrite memory mapped
  /// files in-place (PR17960).
  ///
  /// The original buffer is not actually changed.
  raw_ostream &write(raw_ostream &stream) const {
    return rewriteBuffer.write(stream);
  }

  /// Return lines that are purely comments.
  SmallVector<SMRange> getSingleLineComments() {
    unsigned curBuf = sourceMgr.getMainFileID();
    const llvm::MemoryBuffer *curMB = sourceMgr.getMemoryBuffer(curBuf);
    llvm::line_iterator lineIterator(*curMB);
    SmallVector<SMRange> ret;
    for (; !lineIterator.is_at_end(); ++lineIterator) {
      StringRef trimmed = lineIterator->ltrim();
      if (trimmed.starts_with("//")) {
        ret.emplace_back(
            SMLoc::getFromPointer(trimmed.data()),
            SMLoc::getFromPointer(trimmed.data() + trimmed.size()));
      }
    }
    return ret;
  }

  /// Return the IR from parsed file.
  Block *getParsed() { return &parsedIR; }

  /// Return the definition for the given operation, or nullptr if the given
  /// operation does not have a definition.
  const OperationDefinition &getOpDef(Operation *op) const {
    return *asmState.getOpDef(op);
  }

private:
  // The context and state required to parse.
  MLIRContext context;
  llvm::SourceMgr sourceMgr;
  DialectRegistry registry;
  FallbackAsmResourceMap fallbackResourceMap;

  // Storage of textual parsing results.
  AsmParserState asmState;

  // Parsed IR.
  Block parsedIR;

  // The RewriteBuffer  is doing most of the real work.
  llvm::RewriteBuffer rewriteBuffer;

  // Start of the original input, used to compute offset.
  const char *start;
};

std::unique_ptr<RewritePad> RewritePad::init(StringRef inputFilename,
                                             StringRef outputFilename) {
  std::unique_ptr<RewritePad> r = std::make_unique<RewritePad>();

  // Register all the dialects needed.
  registerAllDialects(r->registry);

  // Set up the input file.
  std::string errorMessage;
  std::unique_ptr<llvm::MemoryBuffer> file =
      openInputFile(inputFilename, &errorMessage);
  if (!file) {
    llvm::errs() << errorMessage << "\n";
    return nullptr;
  }
  r->sourceMgr.AddNewSourceBuffer(std::move(file), SMLoc());

  // Set up the MLIR context and error handling.
  r->context.appendDialectRegistry(r->registry);

  // Record the start of the buffer to compute offsets with.
  unsigned curBuf = r->sourceMgr.getMainFileID();
  const llvm::MemoryBuffer *curMB = r->sourceMgr.getMemoryBuffer(curBuf);
  r->start = curMB->getBufferStart();
  r->rewriteBuffer.Initialize(curMB->getBuffer());

  // Parse and populate the AsmParserState.
  ParserConfig parseConfig(&r->context, /*verifyAfterParse=*/true,
                           &r->fallbackResourceMap);
  // Always allow unregistered.
  r->context.allowUnregisteredDialects(true);
  if (failed(parseAsmSourceFile(r->sourceMgr, &r->parsedIR, parseConfig,
                                &r->asmState)))
    return nullptr;

  return r;
}

/// Return the source code associated with the operation name.
static SMRange getOpNameRange(const OperationDefinition &op) { return op.loc; }

/// Return whether the operation was printed using generic syntax in original
/// buffer.
static bool isGeneric(const OperationDefinition &op) {
  return op.loc.Start.getPointer()[0] == '"';
}

static inline int asMainReturnCode(LogicalResult r) {
  return r.succeeded() ? EXIT_SUCCESS : EXIT_FAILURE;
}

/// Reriter function to invoke.
using RewriterFunction = std::function<mlir::LogicalResult(
    mlir::RewritePad &rewriteState, llvm::raw_ostream &os)>;

/// Structure to group information about a rewriter (argument to invoke via
/// mlir-tblgen, description, and rewriter function).
class RewriterInfo {
public:
  /// RewriterInfo constructor should not be invoked directly, instead use
  /// RewriterRegistration or registerRewriter.
  RewriterInfo(StringRef arg, StringRef description, RewriterFunction rewriter)
      : arg(arg), description(description), rewriter(std::move(rewriter)) {}

  /// Invokes the rewriter and returns whether the rewriter failed.
  LogicalResult invoke(mlir::RewritePad &rewriteState, raw_ostream &os) const {
    assert(rewriter && "Cannot call rewriter with null rewriter");
    return rewriter(rewriteState, os);
  }

  /// Returns the command line option that may be passed to 'mlir-rewrite' to
  /// invoke this rewriter.
  StringRef getRewriterArgument() const { return arg; }

  /// Returns a description for the rewriter.
  StringRef getRewriterDescription() const { return description; }

private:
  // The argument with which to invoke the rewriter via mlir-tblgen.
  StringRef arg;

  // Description of the rewriter.
  StringRef description;

  // Rewritererator function.
  RewriterFunction rewriter;
};

static llvm::ManagedStatic<std::vector<RewriterInfo>> rewriterRegistry;

/// RewriterRegistration provides a global initializer that registers a rewriter
/// function.
struct RewriterRegistration {
  RewriterRegistration(StringRef arg, StringRef description,
                       const RewriterFunction &function);
};

RewriterRegistration::RewriterRegistration(StringRef arg, StringRef description,
                                           const RewriterFunction &function) {
  rewriterRegistry->emplace_back(arg, description, function);
}

} // namespace mlir

/// The selected rewriter, set by the command-line option.
static const mlir::RewriterInfo *selectedRewriter = nullptr;

// TODO: Make these injectable too in non-global way.
inline constexpr OptionCategory clSimpleRenameCategory{"simple-rename options"};
inline constexpr OptionInfo<std::string> simpleRenameOpNameOpt{
    "simple-rename-op-name", "Name of op to match on",
    cat(clSimpleRenameCategory)};
inline constexpr OptionInfo<std::string> simpleRenameMatchOpt{
    "simple-rename-match", "Match string for rename",
    cat(clSimpleRenameCategory)};
inline constexpr OptionInfo<std::string> simpleRenameReplaceOpt{
    "simple-rename-replace", "Replace string for rename",
    cat(clSimpleRenameCategory)};

inline constexpr OptionInfo<std::string> inputFilenameOpt{
    "", "<input file>", Positional{}, Init{"-"}};
inline constexpr OptionInfo<std::string> outputFilenameOpt{
    "o", "Output filename", value_desc("filename"), Init{"-"}};

static constexpr OptionsRegistry<&inputFilenameOpt, &outputFilenameOpt,
                                 &simpleRenameOpNameOpt, &simpleRenameMatchOpt,
                                 &simpleRenameReplaceOpt>
    RewriteReg;

// Rewriter that does simple renames.
static LogicalResult simpleRenameImpl(RewritePad &rewriteState, raw_ostream &os,
                                      StringRef opName, StringRef match,
                                      StringRef replace) {
  llvm::Regex regex(match);

  rewriteState.getParsed()->walk([&](Operation *op) {
    if (op->getName().getStringRef() != opName)
      return;

    const OperationDefinition &opDef = rewriteState.getOpDef(op);
    SMRange range = getOpRange(opDef);
    // This is a little bit overkill for simple.
    std::string str = regex.sub(replace, rewriteState.getSourceString(range));
    rewriteState.replaceRange(range, str);
  });
  return success();
}

// Placeholder - actual dispatch happens in main() with parsed options.
static LogicalResult simpleRename(RewritePad &rewriteState, raw_ostream &os) {
  return success();
}

static mlir::RewriterRegistration rewriteSimpleRename("simple-rename",
                                                      "Perform a simple rename",
                                                      simpleRename);

// Rewriter that insert range markers.
static LogicalResult markRanges(RewritePad &rewriteState, raw_ostream &os) {
  for (const auto &it : rewriteState.getOpDefs()) {
    auto [startOp, endOp] = getOpRange(it);

    rewriteState.insertText(startOp, "<");
    rewriteState.insertText(endOp, ">");

    auto nameRange = getOpNameRange(it);

    if (isGeneric(it)) {
      rewriteState.insertText(nameRange.Start, "[");
      rewriteState.insertText(nameRange.End, "]");
    } else {
      rewriteState.insertText(nameRange.Start, "![");
      rewriteState.insertText(nameRange.End, "]!");
    }
  }

  // Highlight all comment lines.
  // TODO: Could be replaced if this is kept in memory.
  for (auto commentLine : rewriteState.getSingleLineComments()) {
    rewriteState.insertText(commentLine.Start, "{");
    rewriteState.insertText(commentLine.End, "}");
  }

  return success();
}

static mlir::RewriterRegistration
    rewriteMarkRanges("mark-ranges", "Indicate ranges parsed", markRanges);

/// Ctx is the RewriterInfo this flag selects.
static bool selectRewriter(void *Ctx, const bool &) {
  selectedRewriter = static_cast<const mlir::RewriterInfo *>(Ctx);
  return true;
}

static void registerRewriterSelectors(llvm::clv2::OptionParser &P) {
  llvm::SmallVector<const mlir::RewriterInfo *, 4> sorted;
  for (const auto &rw : *mlir::rewriterRegistry)
    sorted.push_back(&rw);
  llvm::sort(sorted, [](const auto *a, const auto *b) {
    return a->getRewriterArgument() < b->getRewriterArgument();
  });
  static std::deque<llvm::clv2::RuntimeOption<bool>> Options;
  bool first = true;
  for (const auto *rw : sorted) {
    Options.emplace_back(
        rw->getRewriterArgument(), rw->getRewriterDescription(),
        llvm::clv2::ValueDisallowed,
        llvm::clv2::CtxCallback<bool>{&selectRewriter,
                                      const_cast<mlir::RewriterInfo *>(rw)});
    // Group display has no descriptor spelling, so it is set on the option's
    // own static info.
    Options.back().staticInfo().IsEnumGroupMember = true;
    if (first) {
      Options.back().staticInfo().EnumGroupHeader = "Rewriter to run";
      first = false;
    }
    llvm::clv2::detail::OptionEntry E = Options.back().makeEntry();
    P.addDynamicEntry(std::move(E));
  }
}

int main(int argc, char **argv) {
  llvm::clv2::OptionParser P;
  P.add<&RewriteReg>();
  llvm::RegisterCommonLLVMOptionsHidden(P);
  // Auto-generated showOptions for mlir-rewrite
  P.showOptions({
      "abort-on-max-devirt-iterations-reached",
      "allow-ginsert-as-artifact",
      "amdgpu-atomic-optimizer-strategy",
      "amdgpu-bypass-slow-div",
      "amdgpu-disable-loop-alignment",
      "amdgpu-dpp-combine",
      "amdgpu-dump-hsa-metadata",
      "amdgpu-enable-merge-m0",
      "amdgpu-indirect-call-specialization-threshold",
      "amdgpu-kernarg-preload",
      "amdgpu-kernarg-preload-count",
      "amdgpu-module-splitting-max-depth",
      "amdgpu-promote-alloca-to-vector-limit",
      "amdgpu-promote-alloca-to-vector-max-regs",
      "amdgpu-promote-alloca-to-vector-vgpr-ratio",
      "amdgpu-sdwa-peephole",
      "amdgpu-use-aa-in-codegen",
      "amdgpu-verify-hsa-metadata",
      "amdgpu-vgpr-index-mode",
      "arc-contract-use-objc-claim-rv",
      "atomic-counter-update-promoted",
      "atomic-first-counter",
      "basic-block-section-match-infer",
      "bounds-checking-single-trap",
      "cfg-hide-cold-paths",
      "cfg-hide-deoptimize-paths",
      "cfg-hide-unreachable-paths",
      "check-functions-filter",
      "conditional-counter-update",
      "cost-kind",
      "ir2vec-arg-weight",
      "ir2vec-kind",
      "ir2vec-opc-weight",
      "ir2vec-type-weight",
      "ir2vec-vocab-path",
      "mir2vec-common-operand-weight",
      "mir2vec-kind",
      "mir2vec-opc-weight",
      "mir2vec-print-all-vocab-entries",
      "mir2vec-reg-operand-weight",
      "mir2vec-vocab-path",
      "ctx-profile-force-is-specialized",
      "debugify-atoms",
      "debugify-func-limit",
      "debugify-level",
      "debugify-quiet",
      "devirtualize-speculatively",
      "disable-auto-upgrade-debug-info",
      "disable-i2p-p2i-opt",
      "disable-promote-alloca-to-lds",
      "disable-promote-alloca-to-vector",
      "do-counter-promotion",
      "dot-cfg-mssa",
      "elide-all-zero-branch-weights",
      "emit-bb-hash",
      "enable-cse-in-irtranslator",
      "enable-cse-in-legalizer",
      "enable-devirtualize-speculatively",
      "enable-gvn-hoist",
      "enable-gvn-memdep",
      "enable-gvn-memoryssa",
      "enable-gvn-sink",
      "enable-jump-table-to-switch",
      "enable-load-in-loop-pre",
      "enable-load-pre",
      "enable-loop-simplifycfg-term-folding",
      "enable-name-compression",
      "enable-poison-reuse-guard",
      "enable-split-backedge-in-load-pre",
      "enable-split-loopiv-heuristic",
      "enable-vtable-profile-use",
      "enable-vtable-value-profiling",
      "expand-variadics-override",
      "experimental-debug-variable-locations",
      "force-tail-folding-style",
      "fs-profile-debug-bw-threshold",
      "fs-profile-debug-prob-diff-threshold",
      "generate-merged-base-profiles",
      "hash-based-counter-split",
      "hot-cold-split",
      "hwasan-percentile-cutoff-hot",
      "hwasan-random-rate",
      "import-all-index",
      "instcombine-code-sinking",
      "instcombine-guard-widening-window",
      "instcombine-maxarray-size",
      "instcombine-max-num-phis",
      "instcombine-max-sink-users",
      "instcombine-negator-enabled",
      "instcombine-negator-max-depth",
      "instrprof-atomic-counter-update-all",
      "internalize-public-api-file",
      "internalize-public-api-list",
      "intrinsic-cost-strategy",
      "iterative-counter-promotion",
      "lower-allow-check-percentile-cutoff-hot",
      "lower-allow-check-random-rate",
      "mark-ranges",
      "matrix-default-layout",
      "matrix-print-after-transpose-opt",
      "max-counter-promotions",
      "max-counter-promotions-per-loop",
      "mir-strip-debugify-only",
      "misexpect-tolerance",
      "ms-secure-hotpatch-functions-file",
      "ms-secure-hotpatch-functions-list",
      "no-discriminators",
      "nvptx-approx-log2f32",
      "nvptx-sched4reg",
      "o",
      "object-size-offset-visitor-max-visit-instructions",
      "pgo-block-coverage",
      "pgo-temporal-instrumentation",
      "pgo-view-block-coverage-graph",
      "print-pipeline-passes",
      "profcheck-annotate-select",
      "profcheck-default-function-entry-count",
      "profcheck-default-select-false-weight",
      "profcheck-default-select-true-weight",
      "profcheck-weights-for-test",
      "profile-correlate",
      "promote-alloca-vector-loop-user-weight",
      "propeller-infer-threshold",
      "r600-ir-structurize",
      "runtime-counter-relocation",
      "safepoint-ir-verifier-print-only",
      "sampled-instr-burst-duration",
      "sampled-instr-period",
      "sampled-instrumentation",
      "sample-profile-check-record-coverage",
      "sample-profile-check-sample-coverage",
      "sample-profile-max-propagate-iterations",
      "simple-rename",
      "simple-rename-match",
      "simple-rename-op-name",
      "simple-rename-replace",
      "skip-ret-exit-block",
      "speculative-counter-promotion-max-exiting",
      "speculative-counter-promotion-to-loop",
      "spirv-emit-op-names",
      "spirv-ext",
      "spv-allow-unknown-intrinsics",
      "spv-dump-deps",
      "spv-emit-nonsemantic-debug-info",
      "summary-file",
      "translator-compatibility-mode",
      "verify-legalizer-debug-locs",
      "verify-region-info",
      "vp-counters-per-site",
      "vp-static-alloc",

      "cost-kind",
      "intrinsic-cost-strategy",
      "spirv-ext",
      "ir2vec-arg-weight",
      "ir2vec-kind",
      "ir2vec-opc-weight",
      "ir2vec-type-weight",
      "ir2vec-vocab-path",
      "mir2vec-common-operand-weight",
      "mir2vec-kind",
      "mir2vec-opc-weight",
      "mir2vec-print-all-vocab-entries",
      "mir2vec-reg-operand-weight",
      "mir2vec-vocab-path",
  });

  registerRewriterSelectors(P);
  auto OptsCtx = P.parse(argc, argv, "mlir-rewrite");
  auto *Opts = OptsCtx->getViewPtr<&RewriteReg>();

  // If no rewriter has been selected, exit with error code. Could also just
  // return but its unlikely this was intentionally being used as `cp`.
  if (!selectedRewriter) {
    llvm::errs() << "No rewriter selected!\n";
    return mlir::asMainReturnCode(mlir::failure());
  }

  // Set up rewrite buffer.
  auto rewriterOr = RewritePad::init(Opts->get<&inputFilenameOpt>(),
                                     Opts->get<&outputFilenameOpt>());
  if (!rewriterOr)
    return mlir::asMainReturnCode(mlir::failure());

  // Set up the output file.
  std::string errorMessage;
  auto output = openOutputFile(Opts->get<&outputFilenameOpt>(), &errorMessage);
  if (!output) {
    llvm::errs() << errorMessage << "\n";
    return mlir::asMainReturnCode(mlir::failure());
  }

  // Handle simple-rename specially: call the implementation with parsed
  // clv2 options since the registered function cannot access them.
  LogicalResult result = mlir::failure();
  if (selectedRewriter->getRewriterArgument() == "simple-rename") {
    result = simpleRenameImpl(*rewriterOr, output->os(),
                              Opts->get<&simpleRenameOpNameOpt>(),
                              Opts->get<&simpleRenameMatchOpt>(),
                              Opts->get<&simpleRenameReplaceOpt>());
  } else {
    result = selectedRewriter->invoke(*rewriterOr, output->os());
  }

  if (succeeded(result)) {
    rewriterOr->write(output->os());
    output->keep();
  }
  return mlir::asMainReturnCode(result);
}
