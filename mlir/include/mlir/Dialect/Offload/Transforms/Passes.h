//===- Passes.h - Offload dialect transformation passes ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_DIALECT_OFFLOAD_TRANSFORMS_PASSES_H
#define MLIR_DIALECT_OFFLOAD_TRANSFORMS_PASSES_H

#include "mlir/Pass/Pass.h"

namespace mlir {
namespace offload {

// Generate declarations for all passes defined in Passes.td.
#define GEN_PASS_DECL
#include "mlir/Dialect/Offload/Transforms/Passes.h.inc"

// Registration helper — registers all offload transformation passes.
void registerOffloadPasses();

} // namespace offload
} // namespace mlir

#endif // MLIR_DIALECT_OFFLOAD_TRANSFORMS_PASSES_H
