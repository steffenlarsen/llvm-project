//===- LocalSlots.h - Locals assigned once in CIR functions -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Helpers for following a value through the stack slot CIRGen spills it to.
// CIRGen keeps every parameter and local in an alloca, and on the device puts
// it in target_address_space(5) and casts it to generic before use, so a value
// assigned once is reached as a load of such a slot through those casts.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_CIR_DIALECT_TRANSFORMS_OFFLOADOPT_LOCALSLOTS_H
#define LLVM_CLANG_CIR_DIALECT_TRANSFORMS_OFFLOADOPT_LOCALSLOTS_H

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "llvm/ADT/SmallVector.h"

#include <optional>

namespace cir {

/// The slot behind an address, looking through the address-space casts CIRGen
/// puts on every device-side local. Null when the address is not a slot.
cir::AllocaOp findSlot(mlir::Value addr);

/// Users of a slot, reached through those same casts. Lifetime markers are
/// left out: they neither write the slot nor let it escape.
void collectSlotUsers(cir::AllocaOp slot,
                      llvm::SmallVectorImpl<mlir::Operation *> &out);

/// Whether \p addr refers to \p slot, modulo those casts.
bool addrIsSlot(mlir::Value addr, cir::AllocaOp slot);

/// The one store into \p slot, when every other use of it reads it: a local
/// assigned once whose address goes nowhere. Null otherwise.
cir::StoreOp onlyStore(cir::AllocaOp slot);

/// Resolve \p v to a constant, additionally looking through a slot that is
/// written exactly once and never escapes.
///
/// `tryResolveToConstant` stops at the address-space cast every device-side
/// local carries, so a `constexpr` that CIRGen spilled to a slot is invisible
/// to it. ggml's loop bound `nint` is exactly this.
std::optional<int64_t> tryConstThroughSlot(mlir::Value v);

} // namespace cir

#endif // LLVM_CLANG_CIR_DIALECT_TRANSFORMS_OFFLOADOPT_LOCALSLOTS_H
