//===-- KernelCloning.h - Clone a kernel and redirect launches -*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Clone-and-redirect: duplicate a kernel under a fresh name and point selected
// host launch sites at the copy, so a pass can specialize the copy while the
// original keeps serving every launch it cannot see.
//
// This is what makes specialisation possible at all for a kernel whose address
// escapes. Targets such as HIP and CUDA allow kernels to be launched through
// paths the compiler cannot see, e.g. by string name, so the original kernel
// must remain available.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_CIR_DIALECT_TRANSFORMS_OFFLOADOPT_KERNELCLONING_H
#define LLVM_CLANG_CIR_DIALECT_TRANSFORMS_OFFLOADOPT_KERNELCLONING_H

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelBindingTable.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include <optional>
#include <string>

namespace cir {

// Marks a host stub produced by the kernel clone functions.
inline constexpr llvm::StringRef kSpecializationCloneAttr =
    "cir.offload.specialization_clone";

// Marks a host launch helper cloned by SpecializeConstantArgs. Such a clone
// exists for no reason other than to carry a particular set of constants, so
// the launches inside it form a group by construction rather than by accident
// of the source -- which is what lets a specialising pass treat them apart
// from the helper's other callers. Matching on this rather than on the clone's
// name is deliberate: a kernel's first clone can take the plain suffix, so
// name-counting is not a reliable detector.
inline constexpr llvm::StringRef kConstArgsCloneAttr =
    "cir.offload.const_args_clone";

struct KernelClone {
  // The cloned device kernels, one per device module that held the original.
  llvm::SmallVector<cir::FuncOp, 2> deviceKernels;
  cir::FuncOp hostStub;
  std::string kernelName;
};

// Clone a binding's device kernels and host stub, and retarget launch sites at
// the copy. Returns nullopt when the binding is not in the shape this can copy
// in which case the IR is left untouched.
// The sites must be launch sites of the binding, but passing a subset is
// allowed and is the point of the routine.
std::optional<KernelClone>
cloneKernelForSites(mlir::ModuleOp container,
                    const cir::KernelBinding &binding, llvm::StringRef suffix,
                    llvm::ArrayRef<cir::LaunchSite> sites);

// Representation of the target for a kernel specialization. The target is
// either the original kernel and stub, or a clone of them. The target is used
// to determine whether the kernel can be specialized in place, or whether a
// clone is needed.
struct SpecializationTarget {
  cir::FuncOp hostStub;
  llvm::SmallVector<cir::FuncOp, 2> deviceKernels;
  bool cloned = false;

  // False when neither specialising in place nor cloning was possible.
  explicit operator bool() const { return hostStub != nullptr; }
};

// Determine the target for a kernel specialization. If the kernel can be
// specialized in place, the target is the original kernel and stub. If a clone
// is needed, the target is the clone. If neither is possible, the target is
// empty.
SpecializationTarget getSpecializationTarget(
    mlir::ModuleOp container, const cir::KernelBinding &binding,
    llvm::StringRef suffix, llvm::ArrayRef<cir::LaunchSite> sites);

} // namespace cir

#endif // LLVM_CLANG_CIR_DIALECT_TRANSFORMS_OFFLOADOPT_KERNELCLONING_H
