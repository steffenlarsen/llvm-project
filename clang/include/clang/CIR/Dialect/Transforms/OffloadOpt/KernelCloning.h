//===- KernelCloning.h - Clone a kernel for a subset of launches -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Clone-and-redirect: duplicate a kernel under a fresh name and point selected
// host launch sites at the copy, so a pass can specialise the copy while the
// original keeps serving every launch it cannot see.
//
// This is what makes specialisation possible at all for a kernel whose address
// escapes. CIRGen emits a handle global for every kernel, and user code can
// take `&kernel` and call `hipLaunchKernel` with arbitrary arguments; a pass
// that rewrites the kernel in place would be wrong for such a launch. Cloning
// sidesteps the question: unseen launches keep reaching the untouched original.
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

// Marks a host stub produced by `cloneKernelForSites`. Such a stub is named
// after nothing in the source, so no user code can refer to it: its only
// references are the launches that were retargeted at it and its own handle
// global. That makes every launch of a clone visible by construction, which is
// what stops a chain of specialising passes from cloning the clone each time.
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

// The copy produced by `cloneKernelForSites`.
struct KernelClone {
  // The cloned device kernels, one per device module that held the original.
  llvm::SmallVector<cir::FuncOp, 2> deviceKernels;
  cir::FuncOp hostStub;
  // Mangled name the clone is registered under; also the name of its handle
  // global, which `LoweringPrepare` looks up when emitting the registration.
  std::string kernelName;
};

// Clone `binding`'s device kernels and host stub, and retarget `sites` at the
// copy. Returns nullopt when the binding is not in the shape this can copy --
// no device kernel, a stub that does not reference its handle global, or a
// name collision -- in which case the IR is left untouched.
//
// `sites` must be launch sites of `binding`; passing a subset is allowed and is
// the point of the routine.
std::optional<KernelClone>
cloneKernelForSites(cir::OffloadContainerOp container,
                    const cir::KernelBinding &binding, llvm::StringRef suffix,
                    llvm::ArrayRef<cir::LaunchSite> sites);

// Whether the launch sites recorded for `stub` are all of them, i.e. whether
// specialising the kernel in place is sound. False means some other launch --
// from another TU, or through an escaped address -- may reach the kernel with
// arguments or geometry the pass never saw.
bool allLaunchSitesVisible(cir::FuncOp stub, llvm::StringRef kernelName,
                           mlir::Operation *scope);

// What a pass may specialise on behalf of `sites`: the originals when that is
// sound, otherwise a fresh clone the sites have been retargeted at.
//
// `cloned` reports which of the two happened, since a clone is a change to the
// IR even before the caller specialises anything.
struct SpecializationTarget {
  cir::FuncOp hostStub;
  llvm::SmallVector<cir::FuncOp, 2> deviceKernels;
  bool cloned = false;

  // False when neither specialising in place nor cloning was possible, which
  // the caller should treat as "leave this kernel alone".
  explicit operator bool() const { return hostStub != nullptr; }
};

SpecializationTarget getSpecializationTarget(
    cir::OffloadContainerOp container, llvm::StringRef kernelName,
    const cir::KernelBinding &binding, llvm::StringRef suffix,
    llvm::ArrayRef<cir::LaunchSite> sites);

} // namespace cir

#endif // LLVM_CLANG_CIR_DIALECT_TRANSFORMS_OFFLOADOPT_KERNELCLONING_H
