//===--- tools/clang-repl/ClangRepl.cpp - clang-repl - the Clang REPL -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements a REPL tool on top of clang.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticFrontend.h"
#include "clang/Basic/Version.h"
#include "clang/Config/config.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Interpreter/CodeCompletion.h"
#include "clang/Interpreter/IncrementalExecutor.h"
#include "clang/Interpreter/Interpreter.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/Sema.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/LineEditor/LineEditor.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h" // llvm_shutdown
#include "llvm/Support/Path.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include <optional>

#include <string>
#include <vector>

#include "llvm/ExecutionEngine/Orc/Debugging/DebuggerSupport.h"

// Disable LSan for this test.
// FIXME: Re-enable once we can assume GCC 13.2 or higher.
// https://llvm.org/github.com/llvm/llvm-project/issues/67586.
#if LLVM_ADDRESS_SANITIZER_BUILD || LLVM_HWADDRESS_SANITIZER_BUILD
#include <sanitizer/lsan_interface.h>
LLVM_ATTRIBUTE_USED int __lsan_is_turned_off() { return 1; }
#endif

#define DEBUG_TYPE "clang-repl"

inline constexpr llvm::clv2::OptionCategory OOPCategory{
    "Out-of-process Execution Options"};

// --- constexpr option descriptors ---
inline constexpr llvm::clv2::OptionInfo<bool> CRCudaOpt{"cuda", "",
                                                        llvm::clv2::Hidden};

inline constexpr llvm::clv2::OptionInfo<std::string> CRCudaPathOpt{
    "cuda-path", "", llvm::clv2::Hidden};

inline constexpr llvm::clv2::OptionInfo<std::string> CROffloadArchOpt{
    "offload-arch", "", llvm::clv2::Hidden};

inline constexpr llvm::clv2::OptionInfo<std::string> CRSlabAllocateOpt{
    "slab-allocate",
    "Allocate from a slab of the given size "
    "(allowable suffixes: Kb, Mb, Gb. default = "
    "Kb)",
    llvm::clv2::cat(OOPCategory)};

inline constexpr llvm::clv2::OptionInfo<std::string> CROOPExecutorOpt{
    "oop-executor", "Launch an out-of-process executor to run code",
    llvm::clv2::ValueOptional, llvm::clv2::cat(OOPCategory)};

inline constexpr llvm::clv2::OptionInfo<std::string> CROOPExecutorConnectOpt{
    "oop-executor-connect",
    "Connect to an out-of-process executor through a TCP socket",
    llvm::clv2::value_desc("<hostname>:<port>")};

inline constexpr llvm::clv2::OptionInfo<std::string> CROrcRuntimePathOpt{
    "orc-runtime", "Path to the ORC runtime", llvm::clv2::ValueOptional,
    llvm::clv2::cat(OOPCategory)};

inline constexpr llvm::clv2::OptionInfo<bool> CRUseSharedMemoryOpt{
    "use-shared-memory",
    "Use shared memory to transfer generated code and data",
    llvm::clv2::cat(OOPCategory)};

inline constexpr llvm::clv2::ListOptionInfo<std::string> CRClangArgsOpt{
    "Xcc", "Argument to pass to the CompilerInvocation",
    llvm::clv2::CommaSeparated};

inline constexpr llvm::clv2::OptionInfo<bool> CRHostSupportsJitOpt{
    "host-supports-jit", "", llvm::clv2::Hidden};

inline constexpr llvm::clv2::OptionInfo<bool> CRHostJitTripleOpt{
    "host-jit-triple", "", llvm::clv2::Hidden};

inline constexpr llvm::clv2::ListOptionInfo<std::string> CRInputsOpt{
    "code to run", "[code to run]", llvm::clv2::Positional{},
    llvm::clv2::ZeroOrMore};

inline constexpr llvm::clv2::OptionsRegistry<
    &CRCudaOpt, &CRCudaPathOpt, &CROffloadArchOpt, &CRSlabAllocateOpt,
    &CROOPExecutorOpt, &CROOPExecutorConnectOpt, &CROrcRuntimePathOpt,
    &CRUseSharedMemoryOpt, &CRClangArgsOpt, &CRHostSupportsJitOpt,
    &CRHostJitTripleOpt, &CRInputsOpt>
    ClangReplReg;

namespace {
struct ClangReplOptions {
  bool CudaEnabled = false;
  std::string CudaPath;
  std::string OffloadArch;
  std::string SlabAllocateSizeString;
  std::string OOPExecutor;
  unsigned OOPExecutorCount = 0;
  std::string OOPExecutorConnect;
  unsigned OOPExecutorConnectCount = 0;
  std::string OrcRuntimePath;
  bool UseSharedMemory = false;
  std::vector<std::string> ClangArgs;
  bool HostSupportsJit = false;
  bool HostJitTriple = false;
  std::vector<std::string> Inputs;
};
} // namespace

static llvm::Error sanitizeOopArguments(const char *ArgV0,
                                        ClangReplOptions &Opts) {
  // Only one of -oop-executor and -oop-executor-connect can be used.
  if (Opts.OOPExecutorCount && Opts.OOPExecutorConnectCount)
    return llvm::make_error<llvm::StringError>(
        "Only one of -oop-executor and -oop-executor-connect can be specified",
        llvm::inconvertibleErrorCode());

  llvm::Triple SystemTriple(llvm::sys::getProcessTriple());
  // TODO: Remove once out-of-process execution support is implemented for
  // non-Unix platforms.
  if ((!SystemTriple.isOSBinFormatELF() &&
       !SystemTriple.isOSBinFormatMachO()) &&
      (Opts.OOPExecutorCount || Opts.OOPExecutorConnectCount))
    return llvm::make_error<llvm::StringError>(
        "Out-of-process execution is only supported on Unix platforms",
        llvm::inconvertibleErrorCode());

  // If -slab-allocate is passed, check that we're not trying to use it in
  // -oop-executor or -oop-executor-connect mode.
  //
  // FIXME: Remove once we enable remote slab allocation.
  if (Opts.SlabAllocateSizeString != "") {
    if (Opts.OOPExecutorCount || Opts.OOPExecutorConnectCount)
      return llvm::make_error<llvm::StringError>(
          "-slab-allocate cannot be used with -oop-executor or "
          "-oop-executor-connect",
          llvm::inconvertibleErrorCode());
  }

  // Out-of-process executors require the ORC runtime. ORC Runtime Path
  // resolution is done in Interpreter.cpp.

  // If -oop-executor was used but no value was specified then use a sensible
  // default.
  if (Opts.OOPExecutorCount && Opts.OOPExecutor.empty()) {
    llvm::SmallString<256> OOPExecutorPath(llvm::sys::fs::getMainExecutable(
        ArgV0, reinterpret_cast<void *>(&sanitizeOopArguments)));
    llvm::sys::path::remove_filename(OOPExecutorPath);
    llvm::sys::path::append(OOPExecutorPath, "llvm-jitlink-executor");
    Opts.OOPExecutor = OOPExecutorPath.str().str();
  }

  return llvm::Error::success();
}

static llvm::Expected<unsigned> getSlabAllocSize(llvm::StringRef SizeString) {
  SizeString = SizeString.trim();

  uint64_t Units = 1024;

  if (SizeString.ends_with_insensitive("kb"))
    SizeString = SizeString.drop_back(2).rtrim();
  else if (SizeString.ends_with_insensitive("mb")) {
    Units = 1024 * 1024;
    SizeString = SizeString.drop_back(2).rtrim();
  } else if (SizeString.ends_with_insensitive("gb")) {
    Units = 1024 * 1024 * 1024;
    SizeString = SizeString.drop_back(2).rtrim();
  } else if (SizeString.empty())
    return 0;

  uint64_t SlabSize = 0;
  if (SizeString.getAsInteger(10, SlabSize))
    return llvm::make_error<llvm::StringError>(
        "Invalid numeric format for slab size", llvm::inconvertibleErrorCode());

  return SlabSize * Units;
}

static void LLVMErrorHandler(void *UserData, const char *Message,
                             bool GenCrashDiag) {
  auto &Diags = *static_cast<clang::DiagnosticsEngine *>(UserData);

  Diags.Report(clang::diag::err_fe_error_backend) << Message;

  // Run the interrupt handlers to make sure any special cleanups get done, in
  // particular that we remove files registered with RemoveFileOnSignal.
  llvm::sys::RunInterruptHandlers();

  // We cannot recover from llvm errors.  When reporting a fatal error, exit
  // with status 70 to generate crash diagnostics.  For BSD systems this is
  // defined as an internal software error. Otherwise, exit with status 1.

  exit(GenCrashDiag ? 70 : 1);
}

// If we are running with -verify a reported has to be returned as unsuccess.
// This is relevant especially for the test suite.
static int checkDiagErrors(const clang::CompilerInstance *CI, bool HasError) {
  unsigned Errs = CI->getDiagnostics().getClient()->getNumErrors();
  if (CI->getDiagnosticOpts().VerifyDiagnostics) {
    // If there was an error that came from the verifier we must return 1 as
    // an exit code for the process. This will make the test fail as expected.
    clang::DiagnosticConsumer *Client = CI->getDiagnostics().getClient();
    Client->EndSourceFile();
    Errs = Client->getNumErrors();

    // The interpreter expects BeginSourceFile/EndSourceFiles to be balanced.
    Client->BeginSourceFile(CI->getLangOpts(), &CI->getPreprocessor());
  }
  return (Errs || HasError) ? EXIT_FAILURE : EXIT_SUCCESS;
}

struct ReplListCompleter {
  clang::IncrementalCompilerBuilder &CB;
  clang::Interpreter &MainInterp;
  ReplListCompleter(clang::IncrementalCompilerBuilder &CB,
                    clang::Interpreter &Interp)
      : CB(CB), MainInterp(Interp) {};

  std::vector<llvm::LineEditor::Completion> operator()(llvm::StringRef Buffer,
                                                       size_t Pos) const;
  std::vector<llvm::LineEditor::Completion>
  operator()(llvm::StringRef Buffer, size_t Pos, llvm::Error &ErrRes) const;
};

std::vector<llvm::LineEditor::Completion>
ReplListCompleter::operator()(llvm::StringRef Buffer, size_t Pos) const {
  auto Err = llvm::Error::success();
  auto res = (*this)(Buffer, Pos, Err);
  if (Err)
    llvm::logAllUnhandledErrors(std::move(Err), llvm::errs(), "error: ");
  return res;
}

std::vector<llvm::LineEditor::Completion>
ReplListCompleter::operator()(llvm::StringRef Buffer, size_t Pos,
                              llvm::Error &ErrRes) const {
  std::vector<llvm::LineEditor::Completion> Comps;
  std::vector<std::string> Results;

  auto CI = CB.CreateCpp();
  if (auto Err = CI.takeError()) {
    ErrRes = std::move(Err);
    return {};
  }

  size_t Lines =
      std::count(Buffer.begin(), std::next(Buffer.begin(), Pos), '\n') + 1;
  auto Interp = clang::Interpreter::create(std::move(*CI));

  if (auto Err = Interp.takeError()) {
    // log the error and returns an empty vector;
    ErrRes = std::move(Err);

    return {};
  }
  auto *MainCI = (*Interp)->getCompilerInstance();
  auto CC = clang::ReplCodeCompleter();
  CC.codeComplete(MainCI, Buffer, Lines, Pos + 1,
                  MainInterp.getCompilerInstance(), Results);
  for (auto c : Results) {
    if (c.find(CC.Prefix) == 0)
      Comps.push_back(
          llvm::LineEditor::Completion(c.substr(CC.Prefix.size()), c));
  }
  return Comps;
}

llvm::ExitOnError ExitOnErr;
int main(int argc, const char **argv) {
  llvm::sys::PrintStackTraceOnErrorSignal(argv[0]);

  ExitOnErr.setBanner("clang-repl: ");
  llvm::clv2::OptionParser P;
  P.add<&ClangReplReg>();
  llvm::RegisterCommonLLVMOptionsHidden(P);
  // Auto-generated showOptions for clang-repl
  P.showOptions({
      "aarch64-neon-syntax",
      "aarch64-use-aa",
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
      "argext-abi-check",
      "arm-add-build-attributes",
      "arm-implicit-it",
      "atomic-counter-update-promoted",
      "atomic-first-counter",
      "basic-block-section-match-infer",
      "bounds-checking-single-trap",
      "bpf-stack-size",
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
      "disable-qfp-opt",
      "disable-qfp-opt-mul",
      "do-counter-promotion",
      "dot-cfg-mssa",
      "elide-all-zero-branch-weights",
      "emit-bb-hash",
      "emit-gnuas-syntax-on-zos",
      "emscripten-cxx-exceptions-allowed",
      "enable-cse-in-irtranslator",
      "enable-cse-in-legalizer",
      "enable-devirtualize-speculatively",
      "enable-emscripten-cxx-exceptions",
      "enable-emscripten-sjlj",
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
      "gpsize",
      "hash-based-counter-split",
      "hexagon-add-build-attributes",
      "hexagon-rdf-limit",
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
      "loongarch-use-aa",
      "lower-allow-check-percentile-cutoff-hot",
      "lower-allow-check-random-rate",
      "lto-embed-bitcode",
      "matrix-default-layout",
      "matrix-print-after-transpose-opt",
      "max-counter-promotions",
      "max-counter-promotions-per-loop",
      "mcabac",
      "merror-missing-parenthesis",
      "merror-noncontigious-register",
      "mhvx",
      "mips16-constant-islands",
      "mips16-hard-float",
      "mips-compact-branches",
      "mir-strip-debugify-only",
      "misexpect-tolerance",
      "mno-compound",
      "mno-fixup",
      "mno-ldc1-sdc1",
      "mno-pairing",
      "ms-secure-hotpatch-functions-file",
      "ms-secure-hotpatch-functions-list",
      "mwarn-missing-parenthesis",
      "mwarn-noncontigious-register",
      "mwarn-sign-mismatch",
      "no-discriminators",
      "nvptx-approx-log2f32",
      "nvptx-sched4reg",
      "object-size-offset-visitor-max-visit-instructions",
      "oop-executor",
      "oop-executor-connect",
      "orc-runtime",
      "pgo-block-coverage",
      "pgo-temporal-instrumentation",
      "pgo-view-block-coverage-graph",
      "polly",
      "polly-2nd-level-tiling",
      "polly-annotate-metadata-vectorize",
      "polly-ast-print-accesses",
      "polly-context",
      "polly-dce-precise-steps",
      "polly-delicm-max-ops",
      "polly-detect-full-functions",
      "polly-dump-after",
      "polly-dump-after-file",
      "polly-dump-before",
      "polly-dump-before-file",
      "polly-enable-simplify",
      "polly-ignore-func",
      "polly-isl-arg",
      "polly-matmul-opt",
      "polly-on-isl-error-abort",
      "polly-only-func",
      "polly-only-region",
      "polly-only-scop-detection",
      "polly-optimized-scops",
      "polly-parallel",
      "polly-parallel-force",
      "polly-pattern-matching-based-opts",
      "polly-postopts",
      "polly-pragma-based-opts",
      "polly-pragma-ignore-depcheck",
      "polly-print-ast",
      "polly-print-delicm",
      "polly-print-deps",
      "polly-print-detect",
      "polly-print-flatten-schedule",
      "polly-print-import-jscop",
      "polly-print-mse",
      "polly-print-opt-isl",
      "polly-print-optree",
      "polly-print-scops",
      "polly-print-simplify",
      "polly-process-unprofitable",
      "polly-register-tiling",
      "polly-report",
      "polly-reschedule",
      "polly-show",
      "polly-show-only",
      "polly-stmt-granularity",
      "polly-tc-opt",
      "polly-tiling",
      "polly-vectorizer",
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
      "riscv-add-build-attributes",
      "riscv-use-aa",
      "runtime-counter-relocation",
      "safepoint-ir-verifier-print-only",
      "sampled-instr-burst-duration",
      "sampled-instr-period",
      "sampled-instrumentation",
      "sample-profile-check-record-coverage",
      "sample-profile-check-sample-coverage",
      "sample-profile-max-propagate-iterations",
      "sanitizer-early-opt-ep",
      "skip-ret-exit-block",
      "slab-allocate",
      "speculative-counter-promotion-max-exiting",
      "speculative-counter-promotion-to-loop",
      "spirv-emit-op-names",
      "spirv-ext",
      "spv-allow-unknown-intrinsics",
      "spv-dump-deps",
      "spv-emit-nonsemantic-debug-info",
      "summary-file",
      "sve-tail-folding",
      "tail-predication",
      "thinlto-assume-merged",
      "translator-compatibility-mode",
      "ubsan-guard-checks",
      "use-shared-memory",
      "verify-legalizer-debug-locs",
      "verify-region-info",
      "vp-counters-per-site",
      "vp-static-alloc",
      "wasm-enable-eh",
      "wasm-enable-sjlj",
      "wasm-use-legacy-eh",
      "x86-align-branch",
      "x86-align-branch-boundary",
      "x86-branches-within-32B-boundaries",
      "x86-enable-apx-for-relocation",
      "x86-pad-max-prefix-size",
      "polly",
      "polly-2nd-level-tiling",
      "polly-annotate-metadata-vectorize",
      "polly-ast-print-accesses",
      "polly-context",
      "polly-dce-precise-steps",
      "polly-delicm-max-ops",
      "polly-detect-full-functions",
      "polly-dump-after",
      "polly-dump-after-file",
      "polly-dump-before",
      "polly-dump-before-file",
      "polly-enable-simplify",
      "polly-ignore-func",
      "polly-isl-arg",
      "polly-matmul-opt",
      "polly-on-isl-error-abort",
      "polly-only-func",
      "polly-only-region",
      "polly-only-scop-detection",
      "polly-optimized-scops",
      "polly-parallel",
      "polly-parallel-force",
      "polly-pattern-matching-based-opts",
      "polly-postopts",
      "polly-pragma-based-opts",
      "polly-pragma-ignore-depcheck",
      "polly-print-ast",
      "polly-print-delicm",
      "polly-print-deps",
      "polly-print-detect",
      "polly-print-flatten-schedule",
      "polly-print-import-jscop",
      "polly-print-mse",
      "polly-print-opt-isl",
      "polly-print-optree",
      "polly-print-scops",
      "polly-print-simplify",
      "polly-process-unprofitable",
      "polly-register-tiling",
      "polly-report",
      "polly-reschedule",
      "polly-show",
      "polly-show-only",
      "polly-stmt-granularity",
      "polly-tc-opt",
      "polly-tiling",
      "polly-vectorizer",

      "Xcc",
  });

  auto OptsCtx = P.parse(argc, argv);
  auto *Opts = OptsCtx->getViewPtr<&ClangReplReg>();

  // Extract parsed values.
  ClangReplOptions ReplOpts;
  ReplOpts.CudaEnabled = Opts->get<&CRCudaOpt>();
  ReplOpts.CudaPath = Opts->get<&CRCudaPathOpt>();
  ReplOpts.OffloadArch = Opts->get<&CROffloadArchOpt>();
  ReplOpts.SlabAllocateSizeString = Opts->get<&CRSlabAllocateOpt>();
  ReplOpts.OOPExecutor = Opts->get<&CROOPExecutorOpt>();
  ReplOpts.OOPExecutorCount = Opts->occurrences<&CROOPExecutorOpt>();
  ReplOpts.OOPExecutorConnect = Opts->get<&CROOPExecutorConnectOpt>();
  ReplOpts.OOPExecutorConnectCount =
      Opts->occurrences<&CROOPExecutorConnectOpt>();
  ReplOpts.OrcRuntimePath = Opts->get<&CROrcRuntimePathOpt>();
  ReplOpts.UseSharedMemory = Opts->get<&CRUseSharedMemoryOpt>();
  ReplOpts.ClangArgs = Opts->get<&CRClangArgsOpt>();
  ReplOpts.HostSupportsJit = Opts->get<&CRHostSupportsJitOpt>();
  ReplOpts.HostJitTriple = Opts->get<&CRHostJitTripleOpt>();
  ReplOpts.Inputs = Opts->get<&CRInputsOpt>();

  llvm::llvm_shutdown_obj Y; // Call llvm_shutdown() on exit.

  std::vector<const char *> ClangArgv(ReplOpts.ClangArgs.size());
  std::transform(ReplOpts.ClangArgs.begin(), ReplOpts.ClangArgs.end(),
                 ClangArgv.begin(),
                 [](const std::string &s) -> const char * { return s.data(); });
  // Initialize all targets (required for device offloading)
  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmPrinters();
  llvm::InitializeAllAsmParsers();

  if (ReplOpts.HostSupportsJit) {
    auto J = llvm::orc::LLJITBuilder().create();
    if (J)
      llvm::outs() << "true\n";
    else {
      llvm::consumeError(J.takeError());
      llvm::outs() << "false\n";
    }
    return 0;
  } else if (ReplOpts.HostJitTriple) {
    auto J = ExitOnErr(llvm::orc::LLJITBuilder().create());
    auto T = J->getTargetTriple();
    llvm::outs() << T.normalize() << '\n';
    return 0;
  }

  ExitOnErr(sanitizeOopArguments(argv[0], ReplOpts));

  clang::IncrementalCompilerBuilder CB;
  CB.SetCompilerArgs(ClangArgv);

  auto IEB = std::make_unique<clang::IncrementalExecutorBuilder>();
  IEB->IsOutOfProcess =
      !ReplOpts.OOPExecutor.empty() || !ReplOpts.OOPExecutorConnect.empty();
  IEB->OOPExecutor = ReplOpts.OOPExecutor;
  if (!ReplOpts.OrcRuntimePath.empty())
    IEB->OrcRuntimePath = ReplOpts.OrcRuntimePath;
  else
    CB.SetDriverCompilationCallback(IEB->UpdateOrcRuntimePathCB);

  auto SizeOrErr = getSlabAllocSize(ReplOpts.SlabAllocateSizeString);
  if (!SizeOrErr) {
    llvm::logAllUnhandledErrors(SizeOrErr.takeError(), llvm::errs(), "error: ");
    return EXIT_FAILURE;
  }
  IEB->SlabAllocateSize = *SizeOrErr;
  IEB->UseSharedMemory = ReplOpts.UseSharedMemory;

  std::unique_ptr<clang::CompilerInstance> DeviceCI;
  if (ReplOpts.CudaEnabled) {
    if (!ReplOpts.CudaPath.empty())
      CB.SetCudaSDK(ReplOpts.CudaPath);

    if (ReplOpts.OffloadArch.empty()) {
      ReplOpts.OffloadArch = "sm_35";
    }
    CB.SetOffloadArch(ReplOpts.OffloadArch);

    DeviceCI = ExitOnErr(CB.CreateCudaDevice());
  }

  // FIXME: Investigate if we could use runToolOnCodeWithArgs from tooling. It
  // can replace the boilerplate code for creation of the compiler instance.
  std::unique_ptr<clang::CompilerInstance> CI;
  if (ReplOpts.CudaEnabled) {
    CI = ExitOnErr(CB.CreateCudaHost());
  } else {
    CI = ExitOnErr(CB.CreateCpp());
  }

  // Set an error handler, so that any LLVM backend diagnostics go through our
  // error handler.
  llvm::install_fatal_error_handler(LLVMErrorHandler,
                                    static_cast<void *>(&CI->getDiagnostics()));

  // Load any requested plugins.
  CI->LoadRequestedPlugins();
  if (ReplOpts.CudaEnabled)
    DeviceCI->LoadRequestedPlugins();

  std::unique_ptr<clang::Interpreter> Interp;

  if (ReplOpts.CudaEnabled) {
    Interp = ExitOnErr(
        clang::Interpreter::createWithCUDA(std::move(CI), std::move(DeviceCI)));

    if (ReplOpts.CudaPath.empty()) {
      ExitOnErr(Interp->LoadDynamicLibrary("libcudart.so"));
    } else {
      auto CudaRuntimeLibPath = ReplOpts.CudaPath + "/lib/libcudart.so";
      ExitOnErr(Interp->LoadDynamicLibrary(CudaRuntimeLibPath.c_str()));
    }
  } else {
    Interp =
        ExitOnErr(clang::Interpreter::create(std::move(CI), std::move(IEB)));
  }

  bool HasError = false;

  for (const std::string &input : ReplOpts.Inputs) {
    if (auto Err = Interp->ParseAndExecute(input)) {
      llvm::logAllUnhandledErrors(std::move(Err), llvm::errs(), "error: ");
      HasError = true;
    }
  }

  if (ReplOpts.Inputs.empty()) {
    llvm::LineEditor LE("clang-repl");
    std::string Input;
    LE.setListCompleter(ReplListCompleter(CB, *Interp));
    while (std::optional<std::string> Line = LE.readLine()) {
      llvm::StringRef L = *Line;
      L = L.trim();
      if (L.ends_with("\\")) {
        Input += L.drop_back(1);
        // If it is a preprocessor directive, new lines matter.
        if (L.starts_with('#'))
          Input += "\n";
        LE.setPrompt("clang-repl...   ");
        continue;
      }

      Input += L;
      // If we add more % commands, there should be better architecture than
      // this.
      if (Input == R"(%quit)") {
        break;
      }
      if (Input == R"(%undo)") {
        if (auto Err = Interp->Undo())
          llvm::logAllUnhandledErrors(std::move(Err), llvm::errs(), "error: ");
      } else if (Input == R"(%help)") {
        llvm::outs() << "%help\t\tlist clang-repl %commands\n"
                     << "%undo\t\tundo the previous input\n"
                     << "%lib\t<path>\tlink a dynamic library\n"
                     << "%quit\t\texit clang-repl\n";
      } else if (Input == R"(%lib)") {
        auto Err = llvm::make_error<llvm::StringError>(
            "%lib expects 1 argument: the path to a dynamic library\n",
            std::error_code());
        llvm::logAllUnhandledErrors(std::move(Err), llvm::errs(), "error: ");
      } else if (Input.rfind("%lib ", 0) == 0) {
        if (auto Err = Interp->LoadDynamicLibrary(Input.data() + 5))
          llvm::logAllUnhandledErrors(std::move(Err), llvm::errs(), "error: ");
      } else if (Input[0] == '%') {
        auto Err = llvm::make_error<llvm::StringError>(
            llvm::formatv(
                "Invalid % command \"{0}\", use \"%help\" to list commands\n",
                Input),
            std::error_code());
        llvm::logAllUnhandledErrors(std::move(Err), llvm::errs(), "error: ");
      } else if (auto Err = Interp->ParseAndExecute(Input)) {
        llvm::logAllUnhandledErrors(std::move(Err), llvm::errs(), "error: ");
      }

      Input = "";
      LE.setPrompt("clang-repl> ");
    }
  }

  // Our error handler depends on the Diagnostics object, which we're
  // potentially about to delete. Uninstall the handler now so that any
  // later errors use the default handling behavior instead.
  llvm::remove_fatal_error_handler();

  return checkDiagErrors(Interp->getCompilerInstance(), HasError);
}
