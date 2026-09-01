//===-- cir-offload-merge/cir-offload-merge.cpp ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This tool merges target-specific CIR modules into a single top-level module
/// containing a cir.offload.container operation.
///
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/DLTI/DLTI.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/OpenMP/OpenMPDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Support/FileUtilities.h"
#include "mlir/Support/LogicalResult.h"
#include "mlir/Transforms/Passes.h"
#include "clang/Basic/TargetID.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/OpenMP/RegisterOpenMPExtensions.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/Driver/OffloadBundler.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"

#include <cctype>
#include <optional>
#include <string>

namespace {

llvm::cl::OptionCategory CIROffloadMergeCategory("cir-offload-merge options");

llvm::cl::opt<bool> Combine("combine", llvm::cl::desc("Combine CIR inputs"),
                            llvm::cl::cat(CIROffloadMergeCategory));

llvm::cl::opt<bool> Split("split", llvm::cl::desc("Split combined CIR input"),
                          llvm::cl::cat(CIROffloadMergeCategory));

llvm::cl::list<std::string>
    InputFileNames("input",
                   llvm::cl::desc("Input CIR file. Can be specified multiple "
                                  "times for multiple input files."),
                   llvm::cl::cat(CIROffloadMergeCategory));

llvm::cl::list<std::string>
    TargetNames("targets", llvm::cl::CommaSeparated,
                llvm::cl::desc("[<offload kind>-<target triple>,...]"),
                llvm::cl::cat(CIROffloadMergeCategory));

llvm::cl::list<std::string>
    OutputFileNames("output",
                    llvm::cl::desc("Output CIR file. Can be specified "
                                   "multiple times in split mode."),
                    llvm::cl::cat(CIROffloadMergeCategory));

// Each optimization runs by default; these turn one off so its effect can be
// isolated, which is the only way to attribute a codegen or performance
// difference to a single pass.
llvm::cl::opt<bool> NoOffloadOpts(
    "no-offload-opts",
    llvm::cl::desc("Disable every container-level offload optimization"),
    llvm::cl::init(false), llvm::cl::cat(CIROffloadMergeCategory));
llvm::cl::opt<bool> NoModuleOpts(
    "no-module-opts",
    llvm::cl::desc("Disable mem2reg and SCCP on the nested modules"),
    llvm::cl::init(false), llvm::cl::cat(CIROffloadMergeCategory));
// Off by default: the pass is correct on every case inspected so far, but the
// resulting ggml build hangs during inference and the cause is not yet known.
// See the cir-launch-wrapper-port memory note.
// On by default: measured +0.69% prefill on llama.cpp / qwen3-8b-q8_0, which is
// what takes this pipeline from behind OGCG to ahead of it. It puts literals
// from one frame above the launch at the launch site, where constant
// propagation can use them, at the cost of a host-side clone per distinct
// constant set.
llvm::cl::opt<bool> NoKernelArgConstProp(
    "no-kernel-arg-const-prop",
    llvm::cl::desc("Disable kernel argument constant propagation"),
    llvm::cl::init(false), llvm::cl::cat(CIROffloadMergeCategory));
// Off by default: correct and it fires widely (1744 kernels on ggml), but it
// measures null on llama while costing +18.5% device binary, because every
// specialization is a clone. See the cir-pointer-facts-port memory note.
llvm::cl::opt<bool>
    NoPropagateBlockShape("no-propagate-block-shape",
                          llvm::cl::desc("Disable block shape propagation"),
                          llvm::cl::init(false),
                          llvm::cl::cat(CIROffloadMergeCategory));
llvm::cl::opt<bool>
    NoTightenLaunchBounds("no-tighten-launch-bounds",
                          llvm::cl::desc("Disable launch bounds tightening"),
                          llvm::cl::init(false),
                          llvm::cl::cat(CIROffloadMergeCategory));
llvm::cl::opt<bool>
    NoDeadKernelElimination("no-dead-kernel-elimination",
                            llvm::cl::desc("Disable dead kernel elimination"),
                            llvm::cl::init(false),
                            llvm::cl::cat(CIROffloadMergeCategory));

struct InputTarget {
  std::string Input;
  std::string Target;
  bool IsHost = false;
};

struct TargetOutput {
  std::string Target;
  std::string Output;
  bool Written = false;
};

int reportError(const llvm::Twine &message) {
  llvm::errs() << "error: " << message << '\n';
  return 1;
}

std::string sanitizeModuleName(llvm::StringRef target) {
  std::string name = "device_";
  for (char c : target) {
    if (std::isalnum(static_cast<unsigned char>(c)))
      name.push_back(c);
    else
      name.push_back('_');
  }
  return name;
}

std::string makeUnique(llvm::StringRef name,
                       llvm::StringMap<unsigned> &seenNames) {
  unsigned &count = seenNames[name];
  if (count++ == 0)
    return name.str();
  return (name + "_" + llvm::Twine(count - 1)).str();
}

bool isValidOffloadKind(const clang::OffloadTargetInfo &offloadInfo) {
  // OffloadTargetInfo rejects CUDA today, even though CUDA-shaped bundle IDs
  // are valid input for this CIR combine scaffold.
  return offloadInfo.isOffloadKindValid() || offloadInfo.OffloadKind == "cuda";
}

bool isValidTarget(llvm::StringRef target,
                   const clang::OffloadBundlerConfig &bundlerConfig) {
  if (!clang::checkOffloadBundleID(target))
    return false;

  clang::OffloadTargetInfo offloadInfo(target, bundlerConfig);
  return isValidOffloadKind(offloadInfo) && offloadInfo.isTripleValid();
}

bool isCompatibleTarget(llvm::StringRef bundleID, llvm::StringRef target,
                        const clang::OffloadBundlerConfig &bundlerConfig) {
  clang::OffloadTargetInfo bundleInfo(bundleID, bundlerConfig);
  clang::OffloadTargetInfo targetInfo(target, bundlerConfig);
  if (bundleInfo == targetInfo)
    return true;

  // Match clang-offload-bundler's code-object compatibility policy: exact
  // bundle IDs are not required when the target ID feature sets are compatible.
  if (!bundleInfo.isOffloadKindCompatible(targetInfo.OffloadKind) ||
      !bundleInfo.Triple.isCompatibleWith(targetInfo.Triple))
    return false;

  llvm::StringMap<bool> bundleFeatureMap;
  llvm::StringMap<bool> targetFeatureMap;
  std::optional<llvm::StringRef> bundleProcessor = clang::parseTargetID(
      bundleInfo.Triple, bundleInfo.TargetID, &bundleFeatureMap);
  std::optional<llvm::StringRef> targetProcessor = clang::parseTargetID(
      targetInfo.Triple, targetInfo.TargetID, &targetFeatureMap);
  if (!bundleProcessor || !targetProcessor ||
      bundleProcessor.value() != targetProcessor.value())
    return false;

  if (bundleFeatureMap.getNumItems() > targetFeatureMap.getNumItems())
    return false;

  for (const auto &bundleFeature : bundleFeatureMap) {
    auto targetFeature = targetFeatureMap.find(bundleFeature.getKey());
    if (targetFeature == targetFeatureMap.end() ||
        targetFeature->getValue() != bundleFeature.getValue())
      return false;
  }

  return true;
}

int validateCombineCommandLine(
    llvm::SmallVectorImpl<InputTarget> &inputTargets) {
  if (Combine == Split)
    return reportError("expected exactly one of -combine or -split");
  if (InputFileNames.empty())
    return reportError("missing required -input");
  if (TargetNames.empty())
    return reportError("missing required -targets");
  if (OutputFileNames.empty())
    return reportError("missing required -output");
  if (OutputFileNames.size() != 1)
    return reportError("combine mode expects exactly one output");
  if (InputFileNames.size() != TargetNames.size())
    return reportError("number of input files and targets should match in "
                       "combine mode");

  clang::OffloadBundlerConfig bundlerConfig;
  llvm::StringSet<> seenTargets;
  unsigned numHostTargets = 0;
  unsigned numDeviceTargets = 0;

  for (auto [input, target] : llvm::zip_equal(InputFileNames, TargetNames)) {
    if (!seenTargets.insert(target).second)
      return reportError("duplicate target '" + target + "'");

    if (!isValidTarget(target, bundlerConfig))
      return reportError("invalid target '" + target + "'");

    clang::OffloadTargetInfo offloadInfo(target, bundlerConfig);
    bool isHost = offloadInfo.hasHostKind();
    if (isHost)
      ++numHostTargets;
    else
      ++numDeviceTargets;

    inputTargets.push_back({input, target, isHost});
  }

  if (numHostTargets != 1)
    return reportError("expected exactly one host target");
  if (numDeviceTargets == 0)
    return reportError("expected at least one device target");

  return 0;
}

int validateSplitCommandLine(
    llvm::SmallVectorImpl<TargetOutput> &targetOutputs) {
  if (Combine == Split)
    return reportError("expected exactly one of -combine or -split");
  if (InputFileNames.empty())
    return reportError("missing required -input");
  if (InputFileNames.size() != 1)
    return reportError("split mode expects exactly one input");
  if (TargetNames.empty())
    return reportError("missing required -targets");
  if (OutputFileNames.empty())
    return reportError("missing required -output");
  if (OutputFileNames.size() != TargetNames.size())
    return reportError("number of output files and targets should match in "
                       "split mode");

  clang::OffloadBundlerConfig bundlerConfig;
  llvm::StringSet<> seenTargets;
  llvm::StringSet<> seenOutputs;

  for (auto [target, output] : llvm::zip_equal(TargetNames, OutputFileNames)) {
    if (!seenTargets.insert(target).second)
      return reportError("duplicate target '" + target + "'");
    if (!seenOutputs.insert(output).second)
      return reportError("duplicate output '" + output + "'");
    if (!isValidTarget(target, bundlerConfig))
      return reportError("invalid target '" + target + "'");

    targetOutputs.push_back({target, output, false});
  }

  return 0;
}

void registerDialects(mlir::DialectRegistry &registry) {
  // Match cir-opt's parser surface: offload CIR inputs may carry OpenMP/LLVM
  // dialect attrs or ops before this tool wraps them in cir.offload.container.
  registry.insert<mlir::BuiltinDialect, cir::CIRDialect,
                  mlir::memref::MemRefDialect, mlir::LLVM::LLVMDialect,
                  mlir::DLTIDialect, mlir::omp::OpenMPDialect>();
  cir::omp::registerOpenMPExtensions(registry);
}

mlir::OwningOpRef<mlir::ModuleOp> parseCIRInput(llvm::StringRef inputFileName,
                                                mlir::MLIRContext &context) {
  mlir::ParserConfig parserConfig(&context);
  return mlir::parseSourceFile<mlir::ModuleOp>(inputFileName, parserConfig);
}

void setOffloadAttrs(mlir::ModuleOp cirModule, llvm::StringRef name,
                     cir::OffloadKind offloadKind, llvm::StringRef bundleID) {
  cirModule.setSymName(name);
  cirModule->setAttr(
      cir::CIRDialect::getOffloadKindAttrName(),
      cir::OffloadKindAttr::get(cirModule.getContext(), offloadKind));
  // This is bundler metadata used to recover output routing during split, not
  // target lowering state for the CIR module.
  cirModule->setAttr(cir::CIRDialect::getOffloadBundleIDAttrName(),
                     mlir::StringAttr::get(cirModule.getContext(), bundleID));
}

mlir::OwningOpRef<mlir::ModuleOp>
combineInputs(llvm::ArrayRef<InputTarget> inputTargets,
              mlir::MLIRContext &context) {
  mlir::OwningOpRef<mlir::ModuleOp> hostModule;
  llvm::SmallVector<mlir::OwningOpRef<mlir::ModuleOp>, 4> deviceModules;
  llvm::StringMap<unsigned> deviceNames;

  for (const InputTarget &inputTarget : inputTargets) {
    mlir::OwningOpRef<mlir::ModuleOp> cirModule =
        parseCIRInput(inputTarget.Input, context);
    if (!cirModule)
      return {};

    if (inputTarget.IsHost) {
      setOffloadAttrs(*cirModule, "host", cir::OffloadKind::Host,
                      inputTarget.Target);
      hostModule = std::move(cirModule);
      continue;
    }

    std::string deviceName =
        makeUnique(sanitizeModuleName(inputTarget.Target), deviceNames);
    setOffloadAttrs(*cirModule, deviceName, cir::OffloadKind::Device,
                    inputTarget.Target);
    deviceModules.push_back(std::move(cirModule));
  }

  auto loc = mlir::UnknownLoc::get(&context);
  mlir::OwningOpRef<mlir::ModuleOp> combinedModule(mlir::ModuleOp::create(loc));
  mlir::OpBuilder builder(&context);
  builder.setInsertionPointToStart(combinedModule->getBody());
  auto container = cir::OffloadContainerOp::create(builder, loc);
  mlir::Block &body = container.getBody().emplaceBlock();

  // Preserve the container invariant expected by the verifier: the host module
  // is the first nested op, followed by device modules in input order.
  body.push_back(hostModule.release().getOperation());
  for (mlir::OwningOpRef<mlir::ModuleOp> &deviceModule : deviceModules)
    body.push_back(deviceModule.release().getOperation());

  return combinedModule;
}

// Run the offload-container optimization passes on the freshly combined module.
// This is the pipeline seam where cross-boundary opts (DKE, and other passes)
// run while host and device modules are still joined in one container.
int runOffloadOptPasses(mlir::ModuleOp module) {
  mlir::PassManager pm(module.getContext(), mlir::ModuleOp::getOperationName());
  mlir::OpPassManager &containerPM = pm.nest<cir::OffloadContainerOp>();

  // Promote stack slots and propagate constants so that a kernel argument known
  // at compile time reaches the device stub call site as a cir.const. Runs on
  // every nested module, host and device.
  // mem2reg and SCCP are ordinary MLIR optimizations rather than offload ones,
  // so --no-offload-opts leaves them alone; --no-module-opts turns them off.
  if (!NoModuleOpts) {
    mlir::OpPassManager &modulePM = containerPM.nest<mlir::ModuleOp>();
    modulePM.addPass(mlir::createMem2Reg());
    modulePM.addPass(mlir::createSCCPPass());
  }

  // Bake launch arguments that every visible call site agrees on into the
  // device kernels. Runs after the module-level passes above so those
  // constants have reached the stub call sites, and before dead-kernel
  // elimination so the latter still sees the full binding table.
  if (!NoOffloadOpts) {
    // First: a launch hidden behind a kernel-pointer wrapper is invisible to
    // the binding table, so every pass below would skip it.
    // Then: a launch argument that is a literal one frame up is recorded as a
    // runtime value, so pin it before anything reads the binding table.
    if (!NoKernelArgConstProp)
      containerPM.addPass(
          mlir::createOffloadKernelArgConstantPropagationPass());
    if (!NoPropagateBlockShape)
      containerPM.addPass(mlir::createOffloadPropagateBlockShapePass());
    if (!NoTightenLaunchBounds)
      containerPM.addPass(mlir::createOffloadTightenLaunchBoundsPass());
    // After the geometry passes: pointer facts do not feed them, but their
    // clones are what the launches now reach, so annotating afterwards puts the
    // facts on the kernel that actually runs.
    if (!NoDeadKernelElimination)
      containerPM.addPass(mlir::createOffloadDeadKernelEliminationPass());
  }

  // ABI, not an optimization: a HIP kernel's pointer arguments are global, and
  // OGCG states that on every build. Outside the --no-offload-opts gate for the
  // same reason, and last so the body is already in its final shape.

  if (mlir::failed(pm.run(module)))
    return reportError("offload-container passes failed");
  return 0;
}

int writeModuleToOutput(mlir::ModuleOp module, llvm::StringRef outputFileName) {
  std::string errorMessage;
  std::unique_ptr<llvm::ToolOutputFile> outputFile =
      mlir::openOutputFile(outputFileName, &errorMessage);
  if (!outputFile)
    return reportError(errorMessage);

  // Emit the combined CIR module as textual MLIR.
  // TODO: Eventually when bytecode support lands for CIR, we should handle
  // such case here.
  module->print(outputFile->os());
  outputFile->os() << '\n';
  outputFile->keep();
  return 0;
}

int splitInput(llvm::StringRef inputFileName,
               llvm::MutableArrayRef<TargetOutput> targetOutputs,
               mlir::MLIRContext &context) {
  mlir::OwningOpRef<mlir::ModuleOp> cirModule =
      parseCIRInput(inputFileName, context);
  if (!cirModule)
    return reportError("failed to parse input file");

  if (mlir::failed(mlir::verify(*cirModule)))
    return reportError("failed to verify input module");

  cir::OffloadContainerOp container;
  for (cir::OffloadContainerOp candidate :
       cirModule->getBody()->getOps<cir::OffloadContainerOp>()) {
    if (container)
      return reportError("expected exactly one direct top-level "
                         "cir.offload.container");
    container = candidate;
  }
  if (!container)
    return reportError(
        "expected exactly one direct top-level cir.offload.container");

  clang::OffloadBundlerConfig bundlerConfig;
  llvm::StringSet<> seenBundleIDs;

  for (mlir::ModuleOp nestedModule :
       container.getBody().front().getOps<mlir::ModuleOp>()) {
    auto bundleIDAttr = nestedModule->getAttrOfType<mlir::StringAttr>(
        cir::CIRDialect::getOffloadBundleIDAttrName());
    if (!bundleIDAttr)
      return reportError("nested module is missing 'cir.offload.bundle_id'");

    llvm::StringRef bundleID = bundleIDAttr.getValue();
    if (!seenBundleIDs.insert(bundleID).second)
      return reportError(llvm::Twine("duplicate module bundle ID '") +
                         bundleID + "'");
    if (!isValidTarget(bundleID, bundlerConfig))
      return reportError(llvm::Twine("invalid module bundle ID '") + bundleID +
                         "'");

    // The requested -targets/-output pairs only form the output map. The
    // stored bundle ID decides where this nested module is written.
    TargetOutput *match = nullptr;
    for (TargetOutput &targetOutput : targetOutputs) {
      if (!isCompatibleTarget(bundleID, targetOutput.Target, bundlerConfig))
        continue;
      if (match)
        return reportError(llvm::Twine("module bundle ID '") + bundleID +
                           "' matches multiple requested targets");
      match = &targetOutput;
    }

    if (!match)
      continue;

    if (match->Written)
      return reportError("multiple modules match target '" + match->Target +
                         "'");
    if (int errorCode = writeModuleToOutput(nestedModule, match->Output))
      return errorCode;
    match->Written = true;
  }

  for (const TargetOutput &targetOutput : targetOutputs)
    if (!targetOutput.Written)
      return reportError("can't find module for target '" +
                         targetOutput.Target + "'");

  return 0;
}

} // namespace

int main(int argc, char **argv) {
  llvm::InitLLVM y(argc, argv);
  llvm::cl::HideUnrelatedOptions(CIROffloadMergeCategory);
  llvm::cl::ParseCommandLineOptions(argc, argv,
                                    "CIR host-device offload merge\n");

  mlir::DialectRegistry registry;
  registerDialects(registry);
  mlir::MLIRContext context;
  context.loadDialect<cir::CIRDialect, mlir::memref::MemRefDialect,
                      mlir::LLVM::LLVMDialect, mlir::DLTIDialect,
                      mlir::omp::OpenMPDialect>();
  context.appendDialectRegistry(registry);

  if (Combine) {
    llvm::SmallVector<InputTarget, 4> inputTargets;
    if (int errorCode = validateCombineCommandLine(inputTargets))
      return errorCode;

    mlir::OwningOpRef<mlir::ModuleOp> combinedModule =
        combineInputs(inputTargets, context);
    if (!combinedModule)
      return reportError("failed to parse input file");

    if (mlir::failed(mlir::verify(*combinedModule)))
      return reportError("failed to verify combined module");

    if (int errorCode = runOffloadOptPasses(*combinedModule))
      return errorCode;

    return writeModuleToOutput(*combinedModule, OutputFileNames.front());
  }

  llvm::SmallVector<TargetOutput, 4> targetOutputs;
  if (int errorCode = validateSplitCommandLine(targetOutputs))
    return errorCode;

  return splitInput(InputFileNames.front(), targetOutputs, context);
}
