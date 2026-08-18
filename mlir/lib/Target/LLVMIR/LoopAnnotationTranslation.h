//===- LoopAnnotationTranslation.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the translation between an MLIR loop annotations and
// the corresponding LLVMIR metadata representation.
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_LIB_TARGET_LLVMIR_LOOPANNOTATIONTRANSLATION_H_
#define MLIR_LIB_TARGET_LLVMIR_LOOPANNOTATIONTRANSLATION_H_

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Target/LLVMIR/ModuleTranslation.h"

namespace mlir {
namespace LLVM {
namespace detail {

/// A helper class that converts LoopAnnotationAttrs and AccessGroupAttrs into
/// corresponding llvm::MDNodes.
class LoopAnnotationTranslation {
public:
  LoopAnnotationTranslation(ModuleTranslation &moduleTranslation,
                            llvm::Module &llvmModule)
      : moduleTranslation(moduleTranslation), llvmModule(llvmModule) {}

  llvm::MDNode *translateLoopAnnotation(LoopAnnotationAttr attr, Operation *op);

  /// Returns the LLVM metadata corresponding to an mlir LLVM dialect access
  /// group attribute.
  llvm::MDNode *getAccessGroup(AccessGroupAttr accessGroupAttr);

  /// Returns the LLVM metadata corresponding to the access group attribute
  /// referenced by the AccessGroupOpInterface or null if there are none.
  llvm::MDNode *getAccessGroups(AccessGroupOpInterface op);

  /// The ModuleTranslation owning this instance.
  ModuleTranslation &moduleTranslation;

private:
  /// Returns the LLVM metadata corresponding to a llvm loop metadata attribute.
  llvm::MDNode *lookupLoopMetadata(Attribute options, Operation *op) const {
    return loopMetadataMapping.lookup({options, op});
  }

  void mapLoopMetadata(Attribute options, Operation *op,
                       llvm::MDNode *metadata) {
    auto result = loopMetadataMapping.try_emplace({options, op}, metadata);
    (void)result;
    assert(result.second &&
           "attempting to map loop options that was already mapped");
  }

  /// Mapping from an annotated operation to its LLVM loop metadata. The
  /// metadata is attached to Latch block branches with this attribute.
  ///
  /// Keyed on the operation and not on the attribute alone: attributes are
  /// uniqued, so every structurally identical annotation in a module would
  /// otherwise collapse onto one node and leave unrelated loops sharing an
  /// identifier. LLVM requires the identifier to be distinct per loop -- passes
  /// that record progress on a loop rewrite that node, and Loop::getLoopID()
  /// compares the metadata across latches -- so sharing makes a whole module's
  /// loops indistinguishable to them.
  DenseMap<std::pair<Attribute, Operation *>, llvm::MDNode *>
      loopMetadataMapping;

  /// Mapping from an access group attribute to its LLVM metadata.
  /// This map is populated on module entry and is used to annotate loops (as
  /// identified via their branches) and contained memory accesses.
  DenseMap<AccessGroupAttr, llvm::MDNode *> accessGroupMetadataMapping;

  llvm::Module &llvmModule;
};

} // namespace detail
} // namespace LLVM
} // namespace mlir

#endif // MLIR_LIB_TARGET_LLVMIR_LOOPANNOTATIONTRANSLATION_H_
