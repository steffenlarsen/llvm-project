//===- CIRTBAABuilder.h - Build TBAA metadata for CIR→LLVM lowering ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_CIR_LOWERING_DIRECTTOLLVM_CIRTBAABUILDER_H
#define CLANG_CIR_LOWERING_DIRECTTOLLVM_CIRTBAABUILDER_H

#include "mlir/Dialect/LLVMIR/LLVMAttrs.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Types.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "llvm/ADT/DenseMap.h"

namespace cir {
namespace direct {

/// Builds C/C++ TBAA metadata and attaches it to LLVM dialect LoadOp/StoreOp.
/// Works on LLVM dialect types (i32, f32, ptr, etc.) after CIR→LLVM conversion.
/// Supports both scalar TBAA and struct-path TBAA for struct member accesses.
class CIRTBAABuilder {
public:
  explicit CIRTBAABuilder(mlir::MLIRContext *ctx);

  /// Attach TBAA metadata to all LoadOp/StoreOp in an operation tree.
  /// Call this after CIR→LLVM conversion completes.
  void attachTBAAMetadata(mlir::Operation *op, const mlir::DataLayout &dl);

private:
  // Scalar TBAA.
  mlir::LLVM::TBAATagAttr getScalarTag(mlir::Type llvmType);
  mlir::ArrayAttr getScalarTagArray(mlir::Type llvmType);
  mlir::LLVM::TBAATypeDescriptorAttr getScalarDesc(mlir::Type llvmType);
  mlir::LLVM::TBAATypeDescriptorAttr getOrCreateDesc(llvm::StringRef name);

  // Struct-path TBAA.
  mlir::LLVM::TBAATagAttr getStructPathTag(mlir::LLVM::GEPOp gep,
                                            mlir::Type accessType,
                                            const mlir::DataLayout &dl);
  mlir::LLVM::TBAATypeDescriptorAttr
  getOrCreateStructDesc(mlir::LLVM::LLVMStructType structTy,
                        const mlir::DataLayout &dl);

  mlir::MLIRContext *ctx;
  mlir::LLVM::TBAARootAttr root;
  mlir::LLVM::TBAATypeDescriptorAttr charType;
  llvm::DenseMap<mlir::Type, mlir::LLVM::TBAATagAttr> scalarTagCache;
  llvm::StringMap<mlir::LLVM::TBAATypeDescriptorAttr> descCache;
  llvm::DenseMap<mlir::Type, mlir::LLVM::TBAATypeDescriptorAttr>
      structDescCache;
};

} // namespace direct
} // namespace cir

#endif // CLANG_CIR_LOWERING_DIRECTTOLLVM_CIRTBAABUILDER_H
