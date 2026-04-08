//===- CIRTransformUtils.h - Shared helpers for CIR transforms -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_CIR_DIALECT_TRANSFORMS_CIRTRANSFORMUTILS_H
#define LLVM_CLANG_CIR_DIALECT_TRANSFORMS_CIRTRANSFORMUTILS_H

#include "mlir/IR/Location.h"
#include "mlir/IR/PatternMatch.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"

#include "llvm/ADT/SmallVector.h"

namespace cir {

/// Replace a `cir::CallOp` with a `cir::TryCallOp` whose unwind destination
/// is \p unwindDest. The call's parent block is split immediately after the
/// call; the resulting suffix block becomes the try_call's normal
/// destination and is returned to the caller.
///
/// All attributes of the original call other than the callee and operand
/// segment sizes (which `TryCallOp::create` sets itself) are copied onto
/// the new try_call. Uses of the original call's result, if any, are
/// redirected to the try_call's result, and the original call is erased.
///
/// The call must not already be marked nothrow.
mlir::Block *replaceCallWithTryCall(cir::CallOp callOp, mlir::Block *unwindDest,
                                    mlir::Location loc,
                                    mlir::RewriterBase &rewriter);

/// Replace a `cir::ThrowOp` with a `cir::TryThrowOp` whose unwind
/// destination is \p unwindDest. The throw's parent block is split
/// immediately after the throw; the resulting suffix block (which should
/// contain the `cir.unreachable` that follows every throw) becomes the
/// try_throw's normal destination and is returned to the caller.
///
/// All attributes of the original throw other than the operand segment
/// sizes (which `TryThrowOp::create` sets itself) are copied onto the new
/// try_throw, and the original throw is erased.
mlir::Block *replaceThrowWithTryThrow(cir::ThrowOp throwOp,
                                      mlir::Block *unwindDest,
                                      mlir::Location loc,
                                      mlir::RewriterBase &rewriter);

/// Collect ops in blocks that are unreachable from their region's entry,
/// appending them to \p ops. Used by CIR passes that drive
/// `applyPartialConversion` and need to feed it operations the conversion
/// driver's dominance-order traversal would otherwise skip.
void collectUnreachable(mlir::Operation *parent,
                        llvm::SmallVectorImpl<mlir::Operation *> &ops);

//===----------------------------------------------------------------------===//
// CIR Value Tracer — shared backward-walk utility for offload passes
//===----------------------------------------------------------------------===//

/// Result of tracing a CIR value backward to its origin.
struct ValueTraceResult {
  enum Kind {
    Constant,     ///< Compile-time integer constant
    Alloca,       ///< Local alloca (Value is the alloca result)
    BlockArg,     ///< Function/region block argument
    Dim3CtorArg,  ///< Argument of a dim3 constructor call
    Unknown       ///< Could not determine origin
  };

  Kind kind = Unknown;
  mlir::Value terminal;                  ///< The origin value
  std::optional<int64_t> constantValue;  ///< Valid when kind == Constant
  unsigned dim3FieldIndex = 0;           ///< x=0, y=1, z=2 for Dim3CtorArg
};

/// Trace a CIR value backward through casts, loads, alloca unique-store
/// forwarding, and dim3 struct paths (get_member → copy → constructor).
///
/// This is the shared core of tryResolveIndexToConstant (TightenLaunchBounds),
/// tryTracePointerToAllocation (PropagatePointerFacts), and the planned
/// GridCoverage pass.  Each pass interprets the terminal differently:
///   - TightenLaunchBounds: checks kind == Constant
///   - PropagatePointerFacts: checks kind == Alloca, then hipMalloc
///   - GridCoverage: checks the Dim3CtorArg for a division pattern
ValueTraceResult traceValueOrigin(mlir::Value v, unsigned maxDepth = 16);

/// Try to resolve a CIR value to a compile-time integer constant.
/// Convenience wrapper around traceValueOrigin.
std::optional<int64_t> tryResolveToConstant(mlir::Value v);

/// Given a dim3 constructor argument value, check if it is of the form
/// `(expr + C-1) / C` (ceiling division by C).  If so, return C and set
/// `dividend` to the `expr` value.  Returns std::nullopt if the pattern
/// doesn't match.
std::optional<int64_t> matchCeilDiv(mlir::Value v, mlir::Value &dividend);

/// Trace a launch argument backward to find which kernel argument index
/// it corresponds to.  Returns std::nullopt if the value doesn't trace
/// to a kernel launch argument.
std::optional<unsigned> traceToKernelArgIndex(
    mlir::Value launchArg, cir::OffloadKernelLaunchOp launch);

/// How one launch grid dimension is derived from a kernel argument:
///
///   gridDim = min(ceilDiv(arg, divisor), cap)
///
/// `divisor` is 1 for the identity form `gridDim = arg`, and `cap` is unset
/// when the expression is not clamped.
///
/// The distinction matters to callers because only the uncapped form supports
/// the unconditional conclusion `gridDim * divisor >= arg`.  When a cap is
/// present that holds solely on the launches where the clamp did not bite,
/// which is a runtime property of the individual launch.
struct GridDimRelation {
  unsigned argIndex = 0;
  int64_t divisor = 1;
  std::optional<int64_t> cap;

  bool isCapped() const { return cap.has_value(); }
};

/// Match a launch grid dimension operand against the shape above.
///
/// Accepts the dim3 member-load chain, an optional `min` clamp (which CIRGen
/// emits as a cir.ternary over a cir.cmp), an optional ceiling division by a
/// constant, and finally a value tracing back to one of \p launch's kernel
/// arguments.  Returns std::nullopt when any step does not match.
///
/// Strict by design: callers use the result to assert a fact unconditionally,
/// so anything not positively recognised must be rejected.  Note this only
/// sees the clamp in its cir.ternary form, which means before CIRFlattenCFG.
std::optional<GridDimRelation>
matchGridDimRelation(mlir::Value gridDim, cir::OffloadKernelLaunchOp launch);

/// Find a kernel argument that a grid dimension plausibly bounds, without
/// proving the relation.
///
/// Unlike matchGridDimRelation this looks through block arguments, so it still
/// finds the argument once a clamp has been flattened into a diamond.  The
/// returned divisor is the one recovered along the way, or 1 when none was
/// found -- the latter is merely a weaker claim, not a wrong one.
///
/// Only for callers that test the relation at run time.  Nothing here
/// establishes that the relation holds, so using the result as an unguarded
/// assumption would be unsound.
std::optional<GridDimRelation>
findGridDimCandidate(mlir::Value gridDim, cir::OffloadKernelLaunchOp launch);

} // namespace cir

#endif // LLVM_CLANG_CIR_DIALECT_TRANSFORMS_CIRTRANSFORMUTILS_H
