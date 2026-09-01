//===- DeviceIndex.h - Recognise device index reads -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Recognises a read of blockIdx / threadIdx / blockDim / gridDim in device CIR.
//
// These are ordinary calls here, not dedicated operations, and they appear at
// two levels depending on how much inlining has happened:
//
//   1. the C++ accessor the HIP/CUDA headers define, e.g.
//      `_ZN24__hip_builtin_blockIdx_t7__get_xEv`
//   2. the ROCm device-library entry point it inlines to, e.g.
//      `__ockl_get_group_id(0)`, where the dimension is the argument
//
// Both are matched, so a pass gets the same answer whether it runs before or
// after inlining. Matching is by substring rather than by whole mangled name,
// because the Itanium length prefix moves with the type name.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_CIR_DIALECT_TRANSFORMS_OFFLOADOPT_DEVICEINDEX_H
#define LLVM_CLANG_CIR_DIALECT_TRANSFORMS_OFFLOADOPT_DEVICEINDEX_H

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "llvm/ADT/StringRef.h"

#include <optional>

namespace cir {

enum class DeviceIndexKind {
  BlockId,  // blockIdx  / workgroup id
  ThreadId, // threadIdx / workitem id
  BlockDim, // blockDim  / workgroup size
  GridDim,  // gridDim   / number of workgroups
};

struct DeviceIndex {
  DeviceIndexKind kind;
  // 0 = x, 1 = y, 2 = z.
  unsigned dim;
};

llvm::StringRef getDeviceIndexKindName(DeviceIndexKind kind);

// The device index `op` reads, or nullopt if it reads none. `op` need not be a
// call; anything else simply does not match.
std::optional<DeviceIndex> matchDeviceIndex(mlir::Operation *op);

// Convenience for the common "is this value a device index read" question.
inline std::optional<DeviceIndex> matchDeviceIndex(mlir::Value v) {
  return v ? matchDeviceIndex(v.getDefiningOp()) : std::nullopt;
}

} // namespace cir

#endif // LLVM_CLANG_CIR_DIALECT_TRANSFORMS_OFFLOADOPT_DEVICEINDEX_H
