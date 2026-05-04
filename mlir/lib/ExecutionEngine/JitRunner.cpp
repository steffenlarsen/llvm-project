//===- jit-runner.cpp - MLIR CPU Execution Driver Library -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a library that provides a shared implementation for command line
// utilities that execute an MLIR file on the CPU by translating MLIR to LLVM
// IR before JIT-compiling and executing the latter.
//
// The translation can be customized by providing an MLIR to MLIR
// transformation.
//===----------------------------------------------------------------------===//

#include "mlir/ExecutionEngine/JitRunner.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/ExecutionEngine/ExecutionEngine.h"
#include "mlir/ExecutionEngine/OptUtils.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Support/FileUtilities.h"
#include "mlir/Tools/ParseUtilities.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/SupportOptions.h"
#include "llvm/Support/SupportOptionsOptInfos.h"
#include "llvm/Support/ToolOutputFile.h"
#include <cstdint>
#include <numeric>
#include <optional>
#include <utility>

#define DEBUG_TYPE "jit-runner"

using namespace mlir;
using namespace llvm::clv2;
using llvm::Error;

static constexpr OptionInfo<std::string> jrInputOpt{"", "<input file>",
                                                    Positional{}, Init{"-"}};
static constexpr OptionInfo<std::string> jrMainFuncOpt{
    "e", "The function to be called", value_desc("<function name>"),
    Init{"main"}};
static constexpr OptionInfo<std::string> jrMainFuncTypeOpt{
    "entry-point-result",
    "Textual description of the function type to be called",
    value_desc("f32 | i32 | i64 | void"), Init{"f32"}};
static constexpr OptionCategory jrOptFlagsCat{"opt-like flags"};
static constexpr OptionCategory jrLinkingCat{"linking options"};
static constexpr OptionInfo<bool> jrO0Opt{
    "O0", "Run opt passes and codegen at O0", cat(jrOptFlagsCat)};
static constexpr OptionInfo<bool> jrO1Opt{
    "O1", "Run opt passes and codegen at O1", cat(jrOptFlagsCat)};
static constexpr OptionInfo<bool> jrO2Opt{
    "O2", "Run opt passes and codegen at O2", cat(jrOptFlagsCat)};
static constexpr OptionInfo<bool> jrO3Opt{
    "O3", "Run opt passes and codegen at O3", cat(jrOptFlagsCat)};
static constexpr OptionInfo<std::string> jrMArchOpt{
    "march", "Architecture to generate code for (see --version)"};
static constexpr ListOptionInfo<std::string> jrMAttrsOpt{
    "mattr", "Target specific attributes (-mattr=help for details)",
    CommaSeparated, value_desc("a1,+a2,-a3,..."), cat(jrOptFlagsCat)};
static constexpr ListOptionInfo<std::string> jrSharedLibsOpt{
    "shared-libs", "Libraries to link dynamically", CommaSeparated,
    cat(jrLinkingCat)};
static constexpr OptionInfo<bool> jrDumpObjectFileOpt{
    "dump-object-file", "Dump JITted-compiled object to file specified with "
                        "-object-filename (<input file>.o by default)."};
static constexpr OptionInfo<std::string> jrObjectFilenameOpt{
    "object-filename", "Dump JITted-compiled object to file <input file>.o"};
static constexpr OptionInfo<bool> jrHostSupportsJitOpt{
    "host-supports-jit", "Report host JIT support and exit"};
static constexpr OptionInfo<bool> jrNoImplicitModuleOpt{
    "no-implicit-module",
    "Disable implicit addition of a top-level module op during parsing"};

static constexpr OptionsRegistry<
    &jrInputOpt, &jrMainFuncOpt, &jrMainFuncTypeOpt, &jrO0Opt, &jrO1Opt,
    &jrO2Opt, &jrO3Opt, &jrMArchOpt, &jrMAttrsOpt, &jrSharedLibsOpt,
    &jrDumpObjectFileOpt, &jrObjectFilenameOpt, &jrHostSupportsJitOpt,
    &jrNoImplicitModuleOpt>
    JitRunnerReg;

namespace {

struct CompileAndExecuteConfig {
  /// LLVM module transformer that is passed to ExecutionEngine.
  std::function<llvm::Error(llvm::Module *)> transformer;

  /// A custom function that is passed to ExecutionEngine. It processes MLIR
  /// module and creates LLVM IR module.
  llvm::function_ref<std::unique_ptr<llvm::Module>(Operation *,
                                                   llvm::LLVMContext &)>
      llvmModuleBuilder;

  /// A custom function that is passed to ExecutinEngine to register symbols at
  /// runtime.
  llvm::function_ref<llvm::orc::SymbolMap(llvm::orc::MangleAndInterner)>
      runtimeSymbolMap;

  /// Optimisation level selected by -O0/-O1/-O2/-O3, or nullopt if none was
  /// given.
  std::optional<unsigned> optLevel;

  /// Libraries to load into the JIT (--shared-libs).
  llvm::ArrayRef<std::string> sharedLibs;

  /// --dump-object-file, and where to write it (--object-filename).  When the
  /// filename is empty the input filename plus ".o" is used.
  bool dumpObjectFile = false;
  llvm::StringRef objectFilename;
  llvm::StringRef inputFilename;
};

} // namespace

static OwningOpRef<Operation *> parseMLIRInput(StringRef inputFilename,
                                               bool insertImplicitModule,
                                               MLIRContext *context) {
  // Set up the input file.
  std::string errorMessage;
  auto file = openInputFile(inputFilename, &errorMessage);
  if (!file) {
    llvm::errs() << errorMessage << "\n";
    return nullptr;
  }

  auto sourceMgr = std::make_shared<llvm::SourceMgr>();
  sourceMgr->AddNewSourceBuffer(std::move(file), SMLoc());
  OwningOpRef<Operation *> module =
      parseSourceFileForTool(sourceMgr, context, insertImplicitModule);
  if (!module)
    return nullptr;
  if (!module.get()->hasTrait<OpTrait::SymbolTable>()) {
    llvm::errs() << "Error: top-level op must be a symbol table.\n";
    return nullptr;
  }
  return module;
}

static inline Error makeStringError(const Twine &message) {
  return llvm::make_error<llvm::StringError>(message.str(),
                                             llvm::inconvertibleErrorCode());
}

static std::optional<unsigned> selectOptLevel(bool o0, bool o1, bool o2,
                                              bool o3) {
  std::optional<unsigned> optLevel;
  bool optLevels[] = {o0, o1, o2, o3};

  // Determine if there is an optimization flag present.
  for (unsigned j = 0; j < 4; ++j) {
    if (optLevels[j]) {
      optLevel = j;
      break;
    }
  }
  return optLevel;
}

// JIT-compile the given module and run "entryPoint" with "args" as arguments.
static Error
compileAndExecute(Operation *module, StringRef entryPoint,
                  CompileAndExecuteConfig config, void **args,
                  std::unique_ptr<llvm::TargetMachine> tm = nullptr) {
  std::optional<llvm::CodeGenOptLevel> jitCodeGenOptLevel;
  if (config.optLevel)
    jitCodeGenOptLevel = static_cast<llvm::CodeGenOptLevel>(*config.optLevel);

  SmallVector<StringRef, 4> sharedLibs(config.sharedLibs.begin(),
                                       config.sharedLibs.end());

  mlir::ExecutionEngineOptions engineOptions;
  engineOptions.llvmModuleBuilder = config.llvmModuleBuilder;
  if (config.transformer)
    engineOptions.transformer = config.transformer;
  engineOptions.jitCodeGenOptLevel = jitCodeGenOptLevel;
  engineOptions.sharedLibPaths = sharedLibs;
  engineOptions.enableObjectDump = true;
  auto expectedEngine =
      mlir::ExecutionEngine::create(module, engineOptions, std::move(tm));
  if (!expectedEngine)
    return expectedEngine.takeError();

  auto engine = std::move(*expectedEngine);

  engine->initialize();

  auto expectedFPtr = engine->lookupPacked(entryPoint);
  if (!expectedFPtr)
    return expectedFPtr.takeError();

  if (config.dumpObjectFile)
    engine->dumpToObjectFile(config.objectFilename.empty()
                                 ? config.inputFilename.str() + ".o"
                                 : config.objectFilename.str());

  void (*fptr)(void **) = *expectedFPtr;
  (*fptr)(args);

  return Error::success();
}

static Error
compileAndExecuteVoidFunction(Operation *module, StringRef entryPoint,
                              CompileAndExecuteConfig config,
                              std::unique_ptr<llvm::TargetMachine> tm) {
  auto mainFunction = dyn_cast_or_null<LLVM::LLVMFuncOp>(
      SymbolTable::lookupSymbolIn(module, entryPoint));
  if (!mainFunction || mainFunction.isExternal())
    return makeStringError("entry point not found");

  if (cast<LLVM::LLVMFunctionType>(mainFunction.getFunctionType())
          .getNumParams() != 0)
    return makeStringError(
        "JIT can't invoke a main function expecting arguments");

  auto resultType = dyn_cast<LLVM::LLVMVoidType>(
      mainFunction.getFunctionType().getReturnType());
  if (!resultType)
    return makeStringError("expected void function");

  void *empty = nullptr;
  return compileAndExecute(module, entryPoint, std::move(config), &empty,
                           std::move(tm));
}

template <typename Type>
Error checkCompatibleReturnType(LLVM::LLVMFuncOp mainFunction);
template <>
Error checkCompatibleReturnType<int32_t>(LLVM::LLVMFuncOp mainFunction) {
  auto resultType = dyn_cast<IntegerType>(
      cast<LLVM::LLVMFunctionType>(mainFunction.getFunctionType())
          .getReturnType());
  if (!resultType || resultType.getWidth() != 32)
    return makeStringError("only single i32 function result supported");
  return Error::success();
}
template <>
Error checkCompatibleReturnType<int64_t>(LLVM::LLVMFuncOp mainFunction) {
  auto resultType = dyn_cast<IntegerType>(
      cast<LLVM::LLVMFunctionType>(mainFunction.getFunctionType())
          .getReturnType());
  if (!resultType || resultType.getWidth() != 64)
    return makeStringError("only single i64 function result supported");
  return Error::success();
}
template <>
Error checkCompatibleReturnType<float>(LLVM::LLVMFuncOp mainFunction) {
  if (!isa<Float32Type>(
          cast<LLVM::LLVMFunctionType>(mainFunction.getFunctionType())
              .getReturnType()))
    return makeStringError("only single f32 function result supported");
  return Error::success();
}
template <typename Type>
static Error
compileAndExecuteSingleReturnFunction(Operation *module, StringRef entryPoint,
                                      CompileAndExecuteConfig config,
                                      std::unique_ptr<llvm::TargetMachine> tm) {
  auto mainFunction = dyn_cast_or_null<LLVM::LLVMFuncOp>(
      SymbolTable::lookupSymbolIn(module, entryPoint));
  if (!mainFunction || mainFunction.isExternal())
    return makeStringError("entry point not found");

  if (cast<LLVM::LLVMFunctionType>(mainFunction.getFunctionType())
          .getNumParams() != 0)
    return makeStringError(
        "JIT can't invoke a main function expecting arguments");

  if (Error error = checkCompatibleReturnType<Type>(mainFunction))
    return error;

  Type res;
  struct {
    void *data;
  } data;
  data.data = &res;
  if (auto error = compileAndExecute(module, entryPoint, std::move(config),
                                     (void **)&data, std::move(tm)))
    return error;

  // Intentional printing of the output so we can test.
  llvm::outs() << res << '\n';

  return Error::success();
}

/// Entry point for all CPU runners. Expects the common argc/argv arguments for
/// standard C++ main functions.
int mlir::JitRunnerMain(int argc, char **argv, const DialectRegistry &registry,
                        JitRunnerConfig config) {
  llvm::ExitOnError exitOnErr;

  llvm::clv2::OptionParser P;
  P.add<&JitRunnerReg>();
  if (config.configureParser)
    config.configureParser(P);
  llvm::RegisterCommonLLVMOptionsHidden(P);
  // Auto-generated showOptions for mlir-runner
  P.showOptions({
      "e",
      "abort-on-max-devirt-iterations-reached",
      "allow-ginsert-as-artifact",
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
      "do-counter-promotion",
      "dot-cfg-mssa",
      "dump-object-file",
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
      "entry-point-result",
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
      "link-nested-modules",
      "lower-allow-check-percentile-cutoff-hot",
      "lower-allow-check-random-rate",
      "march",
      "matrix-default-layout",
      "matrix-print-after-transpose-opt",
      "mattr",
      "max-counter-promotions",
      "max-counter-promotions-per-loop",
      "mir-strip-debugify-only",
      "misexpect-tolerance",
      "ms-secure-hotpatch-functions-file",
      "ms-secure-hotpatch-functions-list",
      "no-discriminators",
      "no-implicit-module",
      "O0",
      "O1",
      "O2",
      "O3",
      "object-filename",
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
      "propeller-infer-threshold",
      "runtime-counter-relocation",
      "safepoint-ir-verifier-print-only",
      "sampled-instr-burst-duration",
      "sampled-instr-period",
      "sampled-instrumentation",
      "sample-profile-check-record-coverage",
      "sample-profile-check-sample-coverage",
      "sample-profile-max-propagate-iterations",
      "shared-libs",
      "skip-ret-exit-block",
      "speculative-counter-promotion-max-exiting",
      "speculative-counter-promotion-to-loop",
      "summary-file",
      "verify-legalizer-debug-locs",
      "verify-region-info",
      "vp-counters-per-site",
      "vp-static-alloc",
      "x86-align-branch",
      "x86-align-branch-boundary",
      "x86-branches-within-32B-boundaries",
      "x86-enable-apx-for-relocation",
      "x86-pad-max-prefix-size",

  });

  auto OptsCtx = P.parse(argc, argv, "MLIR CPU execution driver\n");
  auto *Opts = OptsCtx->getViewPtr<&JitRunnerReg>();

  // Locals, not file-scope state: each of these either stays inside this
  // function or travels to compileAndExecute on CompileAndExecuteConfig.
  const std::string inputFilename = Opts->get<&jrInputOpt>();
  const std::string mainFuncName = Opts->get<&jrMainFuncOpt>();
  const std::string mainFuncType = Opts->get<&jrMainFuncTypeOpt>();
  const std::string mArch = Opts->get<&jrMArchOpt>();
  const std::vector<std::string> mAttrs = Opts->get<&jrMAttrsOpt>();
  const std::vector<std::string> sharedLibs = Opts->get<&jrSharedLibsOpt>();
  const std::string objectFilename = Opts->get<&jrObjectFilenameOpt>();
  const bool dumpObjectFile = Opts->get<&jrDumpObjectFileOpt>();
  const bool hostSupportsJit = Opts->get<&jrHostSupportsJitOpt>();
  const bool noImplicitModule = Opts->get<&jrNoImplicitModuleOpt>();

  if (hostSupportsJit) {
    auto j = llvm::orc::LLJITBuilder().create();
    if (j)
      llvm::outs() << "true\n";
    else {
      llvm::outs() << "false\n";
      exitOnErr(j.takeError());
    }
    return 0;
  }

  std::optional<unsigned> optLevel =
      selectOptLevel(Opts->get<&jrO0Opt>(), Opts->get<&jrO1Opt>(),
                     Opts->get<&jrO2Opt>(), Opts->get<&jrO3Opt>());

  MLIRContext context(registry);

  auto m = parseMLIRInput(inputFilename, !noImplicitModule, &context);
  if (!m) {
    llvm::errs() << "could not parse the input IR\n";
    return 1;
  }

  JitRunnerOptions runnerOptions{mainFuncName, mainFuncType};
  if (config.mlirTransformer)
    if (failed(config.mlirTransformer(m.get(), runnerOptions)))
      return EXIT_FAILURE;

  auto tmBuilderOrError = llvm::orc::JITTargetMachineBuilder::detectHost();
  if (!tmBuilderOrError) {
    llvm::errs() << "Failed to create a JITTargetMachineBuilder for the host\n";
    return EXIT_FAILURE;
  }

  // Configure TargetMachine builder based on the command line options
  llvm::SubtargetFeatures features;
  if (!mAttrs.empty()) {
    for (StringRef attr : mAttrs)
      features.AddFeature(attr);
    tmBuilderOrError->addFeatures(features.getFeatures());
  }

  if (!mArch.empty()) {
    tmBuilderOrError->getTargetTriple().setArchName(mArch);
  }

  // Build TargetMachine
  auto tmOrError = tmBuilderOrError->createTargetMachine();

  if (!tmOrError) {
    llvm::errs() << "Failed to create a TargetMachine for the host\n";
    exitOnErr(tmOrError.takeError());
  }

  LLVM_DEBUG({
    llvm::dbgs() << "  JITTargetMachineBuilder is "
                 << llvm::orc::JITTargetMachineBuilderPrinter(*tmBuilderOrError,
                                                              "\n");
  });

  CompileAndExecuteConfig compileAndExecuteConfig;
  if (optLevel) {
    compileAndExecuteConfig.transformer = mlir::makeOptimizingTransformer(
        *optLevel, /*sizeLevel=*/0, /*targetMachine=*/tmOrError->get());
  }
  compileAndExecuteConfig.llvmModuleBuilder = config.llvmModuleBuilder;
  compileAndExecuteConfig.runtimeSymbolMap = config.runtimesymbolMap;
  compileAndExecuteConfig.optLevel = optLevel;
  compileAndExecuteConfig.sharedLibs = sharedLibs;
  compileAndExecuteConfig.dumpObjectFile = dumpObjectFile;
  compileAndExecuteConfig.objectFilename = objectFilename;
  compileAndExecuteConfig.inputFilename = inputFilename;

  // Get the function used to compile and execute the module.
  using CompileAndExecuteFnT =
      Error (*)(Operation *, StringRef, CompileAndExecuteConfig,
                std::unique_ptr<llvm::TargetMachine> tm);
  auto compileAndExecuteFn =
      StringSwitch<CompileAndExecuteFnT>(mainFuncType)
          .Case("i32", compileAndExecuteSingleReturnFunction<int32_t>)
          .Case("i64", compileAndExecuteSingleReturnFunction<int64_t>)
          .Case("f32", compileAndExecuteSingleReturnFunction<float>)
          .Case("void", compileAndExecuteVoidFunction)
          .Default(nullptr);

  Error error =
      compileAndExecuteFn
          ? compileAndExecuteFn(m.get(), mainFuncName, compileAndExecuteConfig,
                                std::move(tmOrError.get()))
          : makeStringError("unsupported function type");

  int exitCode = EXIT_SUCCESS;
  llvm::handleAllErrors(std::move(error),
                        [&exitCode](const llvm::ErrorInfoBase &info) {
                          llvm::errs() << "Error: ";
                          info.log(llvm::errs());
                          llvm::errs() << '\n';
                          exitCode = EXIT_FAILURE;
                        });

  return exitCode;
}
