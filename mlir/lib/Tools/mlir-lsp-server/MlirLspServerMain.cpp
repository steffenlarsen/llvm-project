//===- MlirLspServerMain.cpp - MLIR Language Server main ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Tools/mlir-lsp-server/MlirLspServerMain.h"
#include "LSPServer.h"
#include "MLIRServer.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/LSP/Logging.h"
#include "llvm/Support/LSP/Transport.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/RegisterLLVMOptions.h"

using namespace mlir;
using namespace mlir::lsp;

using llvm::lsp::JSONStreamStyle;
using llvm::lsp::JSONTransport;
using llvm::lsp::Logger;

//===----------------------------------------------------------------------===//
// Command-line options
//===----------------------------------------------------------------------===//

using llvm::clv2::EnumVal;
using llvm::clv2::Hidden;
using llvm::clv2::Init;
using llvm::clv2::OptionInfo;

enum class LspInputStyle : int { Standard = 0, Delimited };
static constexpr EnumVal<LspInputStyle> lspInputStyleVals[] = {
    {"standard", LspInputStyle::Standard, "usual LSP protocol"},
    {"delimited", LspInputStyle::Delimited,
     "messages delimited by `// -----` lines, with // comment support"},
};
static constexpr auto lspInputStyleOpt =
    llvm::clv2::makeEnumOption<LspInputStyle>(
        "input-style", "Input JSON stream encoding", lspInputStyleVals,
        Init{LspInputStyle::Standard}, Hidden);

static constexpr OptionInfo<bool> lspLitTestOpt{
    "lit-test", "Abbreviation for -input-style=delimited -pretty -log=verbose. "
                "Intended to simplify lit tests"};

enum class LspLogLevel : int { Error = 0, Info, Verbose };
static constexpr EnumVal<LspLogLevel> lspLogLevelVals[] = {
    {"error", LspLogLevel::Error, "Error messages only"},
    {"info", LspLogLevel::Info, "High level execution tracing"},
    {"verbose", LspLogLevel::Verbose, "Low level details"},
};
static constexpr auto lspLogOpt = llvm::clv2::makeEnumOption<LspLogLevel>(
    "log", "Verbosity of log messages written to stderr", lspLogLevelVals,
    Init{LspLogLevel::Info});

static constexpr OptionInfo<bool> lspPrettyOpt{"pretty",
                                               "Pretty-print JSON output"};

static constexpr llvm::clv2::OptionsRegistry<&lspInputStyleOpt, &lspLitTestOpt,
                                             &lspLogOpt, &lspPrettyOpt>
    MlirLspReg;

LogicalResult mlir::MlirLspServerMain(int argc, char **argv,
                                      DialectRegistryFn registry_fn) {
  llvm::clv2::OptionParser P;
  P.add<&MlirLspReg>();
  llvm::RegisterCommonLLVMOptionsHidden(P);
  // Auto-generated showOptions for mlir-lsp-server
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
      "lit-test",
      "log",
      "lower-allow-check-percentile-cutoff-hot",
      "lower-allow-check-random-rate",
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
      "object-size-offset-visitor-max-visit-instructions",
      "pgo-block-coverage",
      "pgo-temporal-instrumentation",
      "pgo-view-block-coverage-graph",
      "pretty",
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

  auto OptsCtx = P.parse(argc, argv, "MLIR LSP Language Server");
  auto *Opts = OptsCtx->getViewPtr<&MlirLspReg>();

  auto inputStyle =
      static_cast<JSONStreamStyle>(Opts->get<&lspInputStyleOpt>());
  bool litTest = Opts->get<&lspLitTestOpt>();
  auto logLevel = static_cast<Logger::Level>(Opts->get<&lspLogOpt>());
  bool prettyPrint = Opts->get<&lspPrettyOpt>();

  if (litTest) {
    inputStyle = JSONStreamStyle::Delimited;
    logLevel = Logger::Level::Debug;
    prettyPrint = true;
  }

  Logger::setLogLevel(logLevel);

  llvm::sys::ChangeStdinToBinary();
  JSONTransport transport(stdin, llvm::outs(), inputStyle, prettyPrint);

  // Register the additionally supported URI schemes for the MLIR server.
  URIForFile::registerSupportedScheme("mlir.bytecode-mlir");

  // Configure the servers and start the main language server.
  MLIRServer server(registry_fn);
  return runMlirLSPServer(server, transport);
}

llvm::LogicalResult mlir::MlirLspServerMain(int argc, char **argv,
                                            DialectRegistry &registry) {
  auto registry_fn =
      [&registry](const lsp::URIForFile &uri) -> DialectRegistry & {
    return registry;
  };
  return MlirLspServerMain(argc, argv, registry_fn);
}
