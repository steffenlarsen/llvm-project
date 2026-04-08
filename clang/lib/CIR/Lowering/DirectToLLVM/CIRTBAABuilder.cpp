//===- CIRTBAABuilder.cpp - Build TBAA metadata for CIR→LLVM lowering ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Builds a C/C++ TBAA type tree from LLVM dialect types and attaches
// TBAATagAttr to LoadOp/StoreOp after CIR→LLVM conversion.  Supports both
// scalar TBAA and struct-path TBAA for struct member accesses.
//
//===----------------------------------------------------------------------===//

#include "CIRTBAABuilder.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinTypes.h"

using namespace mlir;

namespace cir {
namespace direct {

CIRTBAABuilder::CIRTBAABuilder(MLIRContext *ctx) : ctx(ctx) {
  root = LLVM::TBAARootAttr::get(ctx,
                                  StringAttr::get(ctx, "Simple C/C++ TBAA"));
  charType = LLVM::TBAATypeDescriptorAttr::get(
      ctx, "omnipotent char", LLVM::TBAAMemberAttr::get(root, 0));
}

LLVM::TBAATypeDescriptorAttr
CIRTBAABuilder::getOrCreateDesc(llvm::StringRef name) {
  auto it = descCache.find(name);
  if (it != descCache.end())
    return it->second;
  auto desc = LLVM::TBAATypeDescriptorAttr::get(
      ctx, name, LLVM::TBAAMemberAttr::get(charType, 0));
  descCache[name] = desc;
  return desc;
}

LLVM::TBAATypeDescriptorAttr CIRTBAABuilder::getScalarDesc(Type llvmType) {
  if (auto intTy = dyn_cast<IntegerType>(llvmType)) {
    switch (intTy.getWidth()) {
    case 1:
      return getOrCreateDesc("bool");
    case 8:
      return charType;
    case 16:
      return getOrCreateDesc("short");
    case 32:
      return getOrCreateDesc("int");
    case 64:
      return getOrCreateDesc("long long");
    default:
      return nullptr;
    }
  }
  if (llvmType.isF16())
    return getOrCreateDesc("half");
  if (llvmType.isF32())
    return getOrCreateDesc("float");
  if (llvmType.isF64())
    return getOrCreateDesc("double");
  if (isa<LLVM::LLVMPointerType>(llvmType))
    return getOrCreateDesc("any pointer");
  return nullptr;
}

LLVM::TBAATagAttr CIRTBAABuilder::getScalarTag(Type llvmType) {
  auto it = scalarTagCache.find(llvmType);
  if (it != scalarTagCache.end())
    return it->second;

  auto desc = getScalarDesc(llvmType);
  if (!desc || desc == charType)
    return nullptr;

  auto tag = LLVM::TBAATagAttr::get(desc, desc, 0);
  scalarTagCache[llvmType] = tag;
  return tag;
}

ArrayAttr CIRTBAABuilder::getScalarTagArray(Type llvmType) {
  auto tag = getScalarTag(llvmType);
  if (!tag)
    return nullptr;
  return ArrayAttr::get(ctx, tag);
}

//===----------------------------------------------------------------------===//
// Struct-path TBAA
//===----------------------------------------------------------------------===//

LLVM::TBAATypeDescriptorAttr
CIRTBAABuilder::getOrCreateStructDesc(LLVM::LLVMStructType structTy,
                                       const DataLayout &dl) {
  auto it = structDescCache.find(structTy);
  if (it != structDescCache.end())
    return it->second;

  // Build member list with byte offsets.
  SmallVector<LLVM::TBAAMemberAttr> members;
  auto body = structTy.getBody();
  if (body.empty()) {
    structDescCache[structTy] = nullptr;
    return nullptr;
  }

  // Compute byte offsets for each field.  Use the data layout's struct
  // layout computation.
  uint64_t offset = 0;
  unsigned alignment = 1;
  for (auto fieldTy : body) {
    // Align the offset.
    unsigned fieldAlign = dl.getTypeABIAlignment(fieldTy);
    offset = llvm::alignTo(offset, fieldAlign);

    auto memberDesc = getScalarDesc(fieldTy);
    if (!memberDesc)
      memberDesc = charType; // fallback for non-scalar fields
    members.push_back(LLVM::TBAAMemberAttr::get(memberDesc, offset));

    offset += dl.getTypeSize(fieldTy);
    alignment = std::max(alignment, fieldAlign);
  }

  // Use the struct's name if available, otherwise a synthetic name.
  std::string name;
  if (structTy.isIdentified())
    name = structTy.getName().str();
  else
    name = "<anonymous struct>";

  auto desc = LLVM::TBAATypeDescriptorAttr::get(ctx, name, members);
  structDescCache[structTy] = desc;
  return desc;
}

LLVM::TBAATagAttr CIRTBAABuilder::getStructPathTag(LLVM::GEPOp gep,
                                                     Type accessType,
                                                     const DataLayout &dl) {
  // Only handle the GetMemberOp pattern: gep struct, 0, const_idx
  auto indices = gep.getIndices();
  if (indices.size() != 2)
    return nullptr;

  // First index must be constant 0.
  auto firstIdx = dyn_cast<IntegerAttr>(indices[0]);
  if (!firstIdx || firstIdx.getInt() != 0)
    return nullptr;

  // Second index must be a constant (the member index).
  auto secondIdx = dyn_cast<IntegerAttr>(indices[1]);
  if (!secondIdx)
    return nullptr;

  // The source element type must be a struct.
  auto elemTy = gep.getElemType();
  auto structTy = dyn_cast<LLVM::LLVMStructType>(elemTy);
  if (!structTy)
    return nullptr;

  auto structDesc = getOrCreateStructDesc(structTy, dl);
  if (!structDesc)
    return nullptr;

  auto memberDesc = getScalarDesc(accessType);
  if (!memberDesc || memberDesc == charType)
    return nullptr;

  // Compute byte offset of the member.
  unsigned memberIdx = secondIdx.getInt();
  auto body = structTy.getBody();
  if (memberIdx >= body.size())
    return nullptr;

  uint64_t byteOffset = 0;
  for (unsigned i = 0; i < memberIdx; ++i) {
    unsigned fieldAlign = dl.getTypeABIAlignment(body[i]);
    byteOffset = llvm::alignTo(byteOffset, fieldAlign);
    byteOffset += dl.getTypeSize(body[i]);
  }
  unsigned fieldAlign = dl.getTypeABIAlignment(body[memberIdx]);
  byteOffset = llvm::alignTo(byteOffset, fieldAlign);

  return LLVM::TBAATagAttr::get(structDesc, memberDesc, byteOffset);
}

//===----------------------------------------------------------------------===//
// Attach TBAA to all loads/stores
//===----------------------------------------------------------------------===//

void CIRTBAABuilder::attachTBAAMetadata(Operation *op, const DataLayout &dl) {
  op->walk([&](LLVM::LoadOp loadOp) {
    if (loadOp.getVolatile_())
      return;
    if (loadOp.getTbaaAttr())
      return;

    Type accessType = loadOp.getResult().getType();

    // Try struct-path TBAA: check if the address comes from a struct GEP.
    if (auto gep = loadOp.getAddr().getDefiningOp<LLVM::GEPOp>()) {
      if (auto tag = getStructPathTag(gep, accessType, dl)) {
        loadOp.setTbaaAttr(ArrayAttr::get(ctx, tag));
        return;
      }
    }

    // Fall back to scalar TBAA.
    if (auto tag = getScalarTagArray(accessType))
      loadOp.setTbaaAttr(tag);
  });

  op->walk([&](LLVM::StoreOp storeOp) {
    if (storeOp.getVolatile_())
      return;
    if (storeOp.getTbaaAttr())
      return;

    Type accessType = storeOp.getValue().getType();

    if (auto gep = storeOp.getAddr().getDefiningOp<LLVM::GEPOp>()) {
      if (auto tag = getStructPathTag(gep, accessType, dl)) {
        storeOp.setTbaaAttr(ArrayAttr::get(ctx, tag));
        return;
      }
    }

    if (auto tag = getScalarTagArray(accessType))
      storeOp.setTbaaAttr(tag);
  });
}

} // namespace direct
} // namespace cir
