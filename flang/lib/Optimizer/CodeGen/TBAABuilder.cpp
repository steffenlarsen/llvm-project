//===-- TBAABuilder.cpp -- TBAA builder definitions -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Coding style: https://mlir.llvm.org/getting_started/DeveloperGuide/
//
//===----------------------------------------------------------------------===//

#include "flang/Optimizer/CodeGen/TBAABuilder.h"
#include "flang/Common/FlangOptionsOptInfos.h"
#include "flang/Optimizer/Dialect/FIRType.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/OptionsContext.h"
#include <mlir/Dialect/LLVMIR/LLVMAttrs.h>
#include <mlir/Dialect/LLVMIR/LLVMDialect.h>
#include <mlir/Dialect/LLVMIR/LLVMTypes.h>

#define DEBUG_TYPE "flang-tbaa-builder"

using namespace mlir;
using namespace mlir::LLVM;

static constexpr unsigned kTagAttachmentUnlimited =
    std::numeric_limits<unsigned>::max();

namespace fir {

TBAABuilder::TBAABuilder(MLIRContext *context, bool applyTBAA,
                         bool forceUnifiedTree)
    : enableTBAA(
          applyTBAA &&
          !llvm::clv2::getOptValOrDefault<&llvm::clv2::FLANG_DisableTBAA>(
              context->getOptionsContext())),
      trees(/*separatePerFunction=*/llvm::clv2::getOptValOrDefault<
                &llvm::clv2::FLANG_PerFunctionTBAATrees>(
                context->getOptionsContext()) &&
            !forceUnifiedTree) {
  // TODO: the TBAA tags created here are rooted in the root scope
  // of the enclosing function. This does not work best with MLIR inlining.
  // A better approach is to root them according to the scopes they belong to
  // and that were used by AddAliasTagsPass to create TBAA tags before
  // the CodeGen. For example:
  //   subroutine caller(a, b, ptr)
  //     real, target :: a(:), b(:)
  //     integer, pointer :: ptr(:)
  //     call callee(a, b, ptr)
  //   end
  //   subroutine callee(a, b, ptr)
  //     real :: a(:), b(:)
  //     integer, pointer :: ptr(:)
  //     do i=...
  //       a(ptr(i)) = b(ptr(i))
  //     end do
  //   end
  //
  // When callee is inlined, the dummy arguments 'a' and 'b' will
  // be rooted in TBAA tree corresponding to the `call callee` call site,
  // saying that the references to 'a' and 'b' cannot alias each other.
  // These tags will be created by AddAliasTagsPass, but it will not be able
  // to create any tags for 'ptr' references.
  // During the CodeGen, we create 'any data access' tags for the
  // 'ptr' acceses. If they are rooted within the root scope of `caller`,
  // they end up in a different TBAA tree with the 'a' and 'b' access
  // tags, so 'ptr', 'a' and 'b' references MayAlias. Moreover,
  // the box access of 'ptr' will also be in a different TBAA tree
  // with 'a' and 'b' tags, meaning they can also alias.
  // This will prevent LLVM vectorization even with memory conflict checks.
  // It seems that we'd better move all TBAA tags assignment to
  // AddAliasTagsPass, which can at least rely on the dummy arguments scopes.
  if (!enableTBAA)
    return;
}

TBAATagAttr TBAABuilder::getAccessTag(TBAATypeDescriptorAttr baseTypeDesc,
                                      TBAATypeDescriptorAttr accessTypeDesc,
                                      int64_t offset) {
  TBAATagAttr &tag = tagsMap[{baseTypeDesc, accessTypeDesc, offset}];
  if (tag)
    return tag;

  // Initialize new tag.
  tag = TBAATagAttr::get(baseTypeDesc, accessTypeDesc, offset);
  return tag;
}

TBAATagAttr TBAABuilder::getAnyBoxAccessTag(mlir::LLVM::LLVMFuncOp func) {
  TBAATypeDescriptorAttr boxMemberTypeDesc = trees[func].boxMemberTypeDesc;
  return getAccessTag(boxMemberTypeDesc, boxMemberTypeDesc, /*offset=*/0);
}

TBAATagAttr TBAABuilder::getBoxAccessTag(Type baseFIRType, Type accessFIRType,
                                         GEPOp gep,
                                         mlir::LLVM::LLVMFuncOp func) {
  return getAnyBoxAccessTag(func);
}

TBAATagAttr TBAABuilder::getAnyDataAccessTag(mlir::LLVM::LLVMFuncOp func) {
  TBAATypeDescriptorAttr anyDataAccessTypeDesc = trees[func].anyDataTypeDesc;
  return getAccessTag(anyDataAccessTypeDesc, anyDataAccessTypeDesc,
                      /*offset=*/0);
}

TBAATagAttr TBAABuilder::getDataAccessTag(Type baseFIRType, Type accessFIRType,
                                          GEPOp gep,
                                          mlir::LLVM::LLVMFuncOp func) {
  return getAnyDataAccessTag(func);
}

TBAATagAttr TBAABuilder::getAnyAccessTag(mlir::LLVM::LLVMFuncOp func) {
  TBAATypeDescriptorAttr anyAccessTypeDesc = trees[func].anyAccessDesc;
  return getAccessTag(anyAccessTypeDesc, anyAccessTypeDesc, /*offset=*/0);
}

void TBAABuilder::attachTBAATag(AliasAnalysisOpInterface op, Type baseFIRType,
                                Type accessFIRType, GEPOp gep) {
  if (!enableTBAA)
    return;

  mlir::LLVM::LLVMFuncOp func = op->getParentOfType<mlir::LLVM::LLVMFuncOp>();
  if (!func)
    return;

  ++tagAttachmentCounter;
  unsigned tagLimit =
      llvm::clv2::getOptValOrDefault<&llvm::clv2::FLANG_TBAAAttachTagMax>(
          op->getContext()->getOptionsContext());
  if (tagLimit != kTagAttachmentUnlimited && tagAttachmentCounter > tagLimit)
    return;

  LLVM_DEBUG(llvm::dbgs() << "Attaching TBAA tag #" << tagAttachmentCounter
                          << "\n");

  TBAATagAttr tbaaTagSym;
  if (fir::isRecordWithDescriptorMember(baseFIRType)) {
    // A memory access that addresses an aggregate that contains
    // a mix of data members and descriptor members may alias
    // with both data and descriptor accesses.
    // Conservatively set any-access tag if there is any descriptor member.
    tbaaTagSym = getAnyAccessTag(func);
  } else if (mlir::isa<fir::BaseBoxType>(baseFIRType)) {
    tbaaTagSym = getBoxAccessTag(baseFIRType, accessFIRType, gep, func);
  } else {
    tbaaTagSym = getDataAccessTag(baseFIRType, accessFIRType, gep, func);
  }

  if (!tbaaTagSym)
    return;

  op.setTBAATags(ArrayAttr::get(op->getContext(), tbaaTagSym));
}

} // namespace fir
