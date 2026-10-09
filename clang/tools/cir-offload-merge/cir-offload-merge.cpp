//===-- cir-offload-merge/cir-offload-merge.cpp ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
// This tool merges target-specific CIR modules into a single top-level module
// marked as an offload container (carrying the `cir.offload.container` unit
// attribute), and can split such a module back into its nested host and device
// modules.
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/DLTI/DLTI.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/OpenMP/OpenMPDialect.h"
#include "mlir/IR/AttrTypeSubElements.h"
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
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
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

llvm::cl::opt<bool> DisableCirInferLaunchBounds(
    "disable-cir-infer-launch-bounds",
    llvm::cl::desc("Disable launch-bound inference from host launch sites"),
    llvm::cl::cat(CIROffloadMergeCategory));
llvm::cl::opt<bool> DisableCirPropKernelArgs(
    "disable-cir-prop-kernel-args",
    llvm::cl::desc("Disable constant kernel-argument propagation"),
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
  // dialect attrs or ops before this tool wraps them in an offload container.
  registry.insert<mlir::BuiltinDialect, cir::CIRDialect,
                  mlir::memref::MemRefDialect, mlir::LLVM::LLVMDialect,
                  mlir::DLTIDialect, mlir::omp::OpenMPDialect>();
  cir::omp::registerOpenMPExtensions(registry);
}

void initializeContext(mlir::MLIRContext &context) {
  mlir::DialectRegistry registry;
  registerDialects(registry);
  context.loadDialect<cir::CIRDialect, mlir::memref::MemRefDialect,
                      mlir::LLVM::LLVMDialect, mlir::DLTIDialect,
                      mlir::omp::OpenMPDialect>();
  context.appendDialectRegistry(registry);
}

mlir::OwningOpRef<mlir::ModuleOp> parseCIRInput(llvm::StringRef inputFileName,
                                                mlir::MLIRContext &context) {
  mlir::ParserConfig parserConfig(&context);
  return mlir::parseSourceFile<mlir::ModuleOp>(inputFileName, parserConfig);
}

//===----------------------------------------------------------------------===//
// Records across modules
//
// Host and device are generated separately, so one record name can carry a
// different body on each side: anonymous records are numbered per module, and
// a layout can depend on the target (long double, members guarded by
// __HIP_DEVICE_COMPILE__). A context holds a single record per kind and name,
// and parsing a second definition of a name silently yields the first one. A
// device module is therefore parsed into a context of its own first, and the
// records it defines differently from the combined module are renamed before
// it joins the shared context.
//===----------------------------------------------------------------------===//

/// Returns the member types of `record` and, for a union, its padding.
llvm::SmallVector<mlir::Type> getRecordBody(cir::RecordType record) {
  llvm::SmallVector<mlir::Type> body(record.getMembers());
  if (auto unionTy = mlir::dyn_cast<cir::UnionType>(record))
    if (mlir::Type padding = unionTy.getPadding())
      body.push_back(padding);
  return body;
}

/// Collects the records reachable from the operations under `root`. Records do
/// not expose their members as sub-elements, so the walker never enters a
/// record body on its own; each body is walked once its record is found.
llvm::SetVector<cir::RecordType> collectRecords(mlir::Operation *root) {
  llvm::SetVector<cir::RecordType> records;
  llvm::SmallVector<cir::RecordType> worklist;
  mlir::AttrTypeWalker walker;
  walker.addWalk([&](cir::RecordType record) {
    if (records.insert(record))
      worklist.push_back(record);
  });

  // The same elements AttrTypeReplacer::replaceElementsIn rewrites.
  root->walk([&](mlir::Operation *op) {
    walker.walk(op->getRawDictionaryAttrs());
    if (op->getPropertiesStorageSize())
      op->getName().walkInherentAttrs(
          op,
          [&](llvm::StringRef, mlir::Attribute &attr) { walker.walk(attr); });
    for (mlir::Type type : op->getResultTypes())
      walker.walk(type);
    for (mlir::Region &region : op->getRegions())
      for (mlir::Block &block : region)
        for (mlir::BlockArgument arg : block.getArguments())
          walker.walk(arg.getType());
  });

  while (!worklist.empty())
    for (mlir::Type type : getRecordBody(worklist.pop_back_val()))
      walker.walk(type);
  return records;
}

/// Calls `fn` on each named record that the body of `record` refers to,
/// looking through unnamed records, which are identified by their bodies.
void forEachNamedRecordIn(cir::RecordType record,
                          llvm::function_ref<void(cir::RecordType)> fn) {
  llvm::SmallVector<cir::RecordType> worklist{record};
  llvm::DenseSet<mlir::Type> seenUnnamed;
  mlir::AttrTypeWalker walker;
  walker.addWalk([&](cir::RecordType inner) {
    if (inner.getName())
      fn(inner);
    else if (seenUnnamed.insert(inner).second)
      worklist.push_back(inner);
  });
  while (!worklist.empty())
    for (mlir::Type type : getRecordBody(worklist.pop_back_val()))
      walker.walk(type);
}

/// Prints the definitions of `record` and of every record it reaches, so that
/// two records print alike exactly when their whole definitions agree. The
/// record is printed from a top-level module: only there does MLIR emit type
/// aliases, so each reachable record is defined once rather than spelled out
/// at every use, which grows exponentially when records share members.
std::string printRecord(cir::RecordType record) {
  mlir::MLIRContext *context = record.getContext();
  mlir::OwningOpRef<mlir::ModuleOp> holder =
      mlir::ModuleOp::create(mlir::UnknownLoc::get(context));
  (*holder)->setAttr("record", mlir::TypeAttr::get(record));
  std::string text;
  llvm::raw_string_ostream os(text);
  // The holder's attribute name is not dialect-prefixed, which the module
  // verifier rejects; a module failing verification is printed in generic
  // form, without aliases.
  holder->print(os, mlir::OpPrintingFlags().assumeVerified());
  return text;
}

/// Gives each record in `newNames` the name it maps to, rewriting `module` to
/// use the renamed records. A record's name is its identity and a complete
/// body cannot change, so renaming builds new records; unnamed records that
/// contain a renamed one are rebuilt from their replaced members.
void renameRecords(
    mlir::ModuleOp module,
    const llvm::DenseMap<cir::RecordType, std::string> &newNames) {
  mlir::MLIRContext *context = module.getContext();

  // Create every renamed record before completing any, so records that refer
  // to each other are rebuilt referring to each other.
  llvm::DenseMap<cir::RecordType, cir::RecordType> renamed;
  for (const auto &[record, newName] : newNames) {
    auto name = mlir::StringAttr::get(context, newName);
    if (mlir::isa<cir::UnionType>(record))
      renamed[record] = cir::UnionType::get(context, name);
    else
      renamed[record] = cir::StructType::get(context, name, record.isClass());
  }

  mlir::AttrTypeReplacer replacer;
  auto replaceTypes = [&](llvm::ArrayRef<mlir::Type> types) {
    return llvm::map_to_vector(
        types, [&](mlir::Type type) { return replacer.replace(type); });
  };
  // A union's padding is optional; a missing one stays missing.
  auto replacePadding = [&](cir::RecordType record) -> mlir::Type {
    mlir::Type padding = mlir::cast<cir::UnionType>(record).getPadding();
    return padding ? replacer.replace(padding) : padding;
  };

  replacer.addReplacement(
      [&](cir::RecordType record)
          -> std::optional<std::pair<mlir::Type, mlir::WalkResult>> {
        if (record.getName()) {
          cir::RecordType newRecord = renamed.lookup(record);
          return std::make_pair(newRecord ? newRecord : record,
                                mlir::WalkResult::skip());
        }

        llvm::SmallVector<mlir::Type> members =
            replaceTypes(record.getMembers());
        mlir::Type rebuilt;
        if (mlir::isa<cir::UnionType>(record))
          rebuilt = cir::UnionType::get(context, members, record.getPacked(),
                                        replacePadding(record),
                                        record.getMemberKinds());
        else
          rebuilt =
              cir::StructType::get(context, members, record.getPacked(),
                                   record.isClass(), record.getMemberKinds());
        return std::make_pair(rebuilt, mlir::WalkResult::skip());
      });

  // The generic rebuild of a type calls its context-less builder when it has
  // one, and FuncType's takes the context from the return type, which is null
  // for a void function. FuncType is therefore rebuilt here, spelling void out
  // as its builder expects. This covers FuncType only: another type whose
  // context-less builder reads its context from an optional parameter would
  // need the same handling.
  replacer.addReplacement([&](cir::FuncType funcType) {
    mlir::Type returnType = funcType.getOptionalReturnType();
    returnType =
        returnType ? replacer.replace(returnType) : cir::VoidType::get(context);
    return std::make_pair(
        mlir::Type(cir::FuncType::get(replaceTypes(funcType.getInputs()),
                                      returnType, funcType.getVarArg())),
        mlir::WalkResult::skip());
  });

  for (const auto &[record, newRecord] : renamed) {
    if (record.isIncomplete())
      continue;
    llvm::SmallVector<mlir::Type> members = replaceTypes(record.getMembers());
    if (auto unionTy = mlir::dyn_cast<cir::UnionType>(newRecord))
      unionTy.complete(members, record.getPacked(), replacePadding(record),
                       record.getMemberKinds());
    else
      mlir::cast<cir::StructType>(newRecord).complete(
          members, record.getPacked(), record.getMemberKinds());
  }

  replacer.recursivelyReplaceElementsIn(module, /*replaceAttrs=*/true,
                                        /*replaceLocs=*/false,
                                        /*replaceTypes=*/true);
}

/// The named records of the modules combined so far, by the key their context
/// uniques them under.
class CombinedRecords {
public:
  void add(mlir::ModuleOp module) {
    for (cir::RecordType record : collectRecords(module))
      if (record.getName())
        records.try_emplace(record.getPrefixedName(), record);
  }

  /// Returns new names for the records of `module`, parsed in a context of its
  /// own, that cannot share the combined context's definition: those defined
  /// differently there, and those whose bodies refer to a renamed record.
  llvm::DenseMap<cir::RecordType, std::string>
  getRecordsToRename(mlir::ModuleOp module, llvm::StringRef moduleName) {
    llvm::SetVector<cir::RecordType> moduleRecords = collectRecords(module);
    llvm::StringSet<> moduleKeys;
    llvm::DenseMap<cir::RecordType, llvm::SmallVector<cir::RecordType>> users;
    llvm::SmallVector<cir::RecordType> worklist;
    llvm::DenseMap<cir::RecordType, std::string> newNames;

    for (cir::RecordType record : moduleRecords) {
      if (!record.getName())
        continue;
      moduleKeys.insert(record.getPrefixedName());
      forEachNamedRecordIn(record, [&](cir::RecordType referenced) {
        users[referenced].push_back(record);
      });

      auto combined = records.find(record.getPrefixedName());
      if (combined == records.end())
        continue;
      std::string &combinedText = printedRecords[combined->getKey()];
      if (combinedText.empty())
        combinedText = printRecord(combined->getValue());
      if (combinedText != printRecord(record))
        worklist.push_back(record);
    }

    while (!worklist.empty()) {
      cir::RecordType record = worklist.pop_back_val();
      if (newNames.contains(record))
        continue;
      newNames[record] = getUnusedName(record, moduleName, moduleKeys);
      llvm::append_range(worklist, users.lookup(record));
    }
    return newNames;
  }

private:
  std::string getUnusedName(cir::RecordType record, llvm::StringRef moduleName,
                            llvm::StringSet<> &moduleKeys) {
    std::string base = (record.getName().getValue() + "." + moduleName).str();
    std::string name = base;
    for (unsigned i = 1;; ++i) {
      std::string key = record.getKindAsStr() + "." + name;
      if (!records.contains(key) && moduleKeys.insert(key).second)
        return name;
      name = base + "." + std::to_string(i);
    }
  }

  llvm::StringMap<cir::RecordType> records;
  llvm::StringMap<std::string> printedRecords;
};

/// Parses a device input into `context`, renaming the records that conflict
/// with those already combined.
mlir::OwningOpRef<mlir::ModuleOp>
parseDeviceInput(llvm::StringRef inputFileName, llvm::StringRef moduleName,
                 mlir::MLIRContext &context, CombinedRecords &combined) {
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> buffer =
      llvm::MemoryBuffer::getFileOrSTDIN(inputFileName);
  if (!buffer) {
    reportError("cannot open input file '" + inputFileName +
                "': " + buffer.getError().message());
    return {};
  }
  llvm::StringRef source = (*buffer)->getBuffer();

  std::string renamedSource;
  {
    mlir::MLIRContext deviceContext(mlir::MLIRContext::Threading::DISABLED);
    initializeContext(deviceContext);
    mlir::OwningOpRef<mlir::ModuleOp> deviceModule =
        mlir::parseSourceString<mlir::ModuleOp>(
            source, mlir::ParserConfig(&deviceContext), inputFileName);
    if (!deviceModule)
      return {};

    llvm::DenseMap<cir::RecordType, std::string> newNames =
        combined.getRecordsToRename(*deviceModule, moduleName);
    if (!newNames.empty()) {
      renameRecords(*deviceModule, newNames);
      llvm::raw_string_ostream os(renamedSource);
      mlir::OpPrintingFlags flags;
      flags.enableDebugInfo(/*enable=*/true, /*prettyForm=*/false);
      deviceModule->print(os, flags);
      source = renamedSource;
    }
  }

  return mlir::parseSourceString<mlir::ModuleOp>(
      source, mlir::ParserConfig(&context), inputFileName);
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
  llvm::SmallVector<mlir::OwningOpRef<mlir::ModuleOp>, 4> deviceModules;
  llvm::StringMap<unsigned> deviceNames;

  // Parse the host first whatever the input order, so that it keeps its record
  // names and the device modules are the ones renamed on a conflict.
  const InputTarget *hostTarget = llvm::find_if(
      inputTargets, [](const InputTarget &target) { return target.IsHost; });
  mlir::OwningOpRef<mlir::ModuleOp> hostModule =
      parseCIRInput(hostTarget->Input, context);
  if (!hostModule)
    return {};
  setOffloadAttrs(*hostModule, "host", cir::OffloadKind::Host,
                  hostTarget->Target);

  CombinedRecords combinedRecords;
  combinedRecords.add(*hostModule);

  for (const InputTarget &inputTarget : inputTargets) {
    if (inputTarget.IsHost)
      continue;

    std::string deviceName =
        makeUnique(sanitizeModuleName(inputTarget.Target), deviceNames);
    mlir::OwningOpRef<mlir::ModuleOp> cirModule = parseDeviceInput(
        inputTarget.Input, deviceName, context, combinedRecords);
    if (!cirModule)
      return {};
    combinedRecords.add(*cirModule);

    setOffloadAttrs(*cirModule, deviceName, cir::OffloadKind::Device,
                    inputTarget.Target);
    deviceModules.push_back(std::move(cirModule));
  }

  auto loc = mlir::UnknownLoc::get(&context);
  // The combined module is itself the offload container: it carries the
  // `cir.offload.container` unit attribute and holds the host and device
  // modules directly, host first, matching the container verifier.
  mlir::OwningOpRef<mlir::ModuleOp> combinedModule(mlir::ModuleOp::create(loc));
  (*combinedModule)->setAttr(cir::CIRDialect::getOffloadContainerAttrName(),
                             mlir::UnitAttr::get(&context));

  // Preserve the container invariant expected by the verifier: the host module
  // is the first nested op, followed by device modules in input order.
  combinedModule->getBody()->push_back(hostModule.release().getOperation());
  for (mlir::OwningOpRef<mlir::ModuleOp> &deviceModule : deviceModules)
    combinedModule->getBody()->push_back(deviceModule.release().getOperation());

  return combinedModule;
}

// Run the offload-container optimization passes on the freshly combined module.
// This is the pipeline seam where cross-boundary opts (DKE, and other passes)
// run while host and device modules are still joined in one container.
//
// The container is a module, so passes are scheduled over the module; DKE and
// KACP early-return on modules that do not carry the `cir.offload.container`
// unit attribute.
int runOffloadOptPasses(mlir::ModuleOp module) {
  mlir::PassManager pm(module.getContext(), mlir::ModuleOp::getOperationName());
  mlir::OpPassManager &modulePM = pm.nest<mlir::ModuleOp>();

  // Promote stack slots and propagate constants so that a kernel argument known
  // at compile time reaches the device stub call site as a cir.const. Runs on
  // every nested module, host and device.
  modulePM.addPass(mlir::createMem2Reg());
  modulePM.addPass(mlir::createSCCPPass());

  // Container passes run on the container itself; the nested modules carry no
  // container attribute and would make them early-return.
  pm.addPass(mlir::createOffloadDeadKernelEliminationPass());
  if (!DisableCirPropKernelArgs)
    pm.addPass(mlir::createOffloadKernelArgConstantPropagationPass());
  if (!DisableCirInferLaunchBounds)
    pm.addPass(mlir::createOffloadLaunchBoundsPropagationPass());

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

// The single top-level offload container in `cirModule`, or null if there is
// not exactly one module carrying the `cir.offload.container` unit attribute.
mlir::ModuleOp findContainer(mlir::ModuleOp cirModule) {
  // -combine writes the container as the module itself, so check there first;
  // the nested scan below keeps the hand-written container fixtures working.
  if (cir::isOffloadContainer(cirModule))
    return cirModule;

  mlir::ModuleOp container;
  for (mlir::ModuleOp candidate : cirModule.getOps<mlir::ModuleOp>()) {
    if (!cir::isOffloadContainer(candidate))
      continue;
    if (container)
      return {};
    container = candidate;
  }
  return container;
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

  mlir::ModuleOp container = findContainer(*cirModule);
  if (!container)
    return reportError(
        "expected exactly one direct top-level offload container");

  clang::OffloadBundlerConfig bundlerConfig;
  llvm::StringSet<> seenBundleIDs;

  // Host first, then devices: the host is a legitimate split target too, and
  // getOffloadContainerDeviceModules() deliberately skips it.
  for (mlir::ModuleOp nestedModule : container.getOps<mlir::ModuleOp>()) {
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

  mlir::MLIRContext context;
  initializeContext(context);

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