//===- OffloadFuseKernels.cpp - Fuse consecutive kernel launches -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// When two consecutive cir.offload.kernel_launch ops operate on disjoint
// or safely-shared buffers with no intervening side effects, this pass
// fuses them into a single kernel launch.
//
// For shared buffers, the pass analyzes index expressions in the kernel
// bodies to determine the synchronization requirement:
//   - Per-thread: both kernels access buf[f(tid)] with the same f → no sync.
//   - Per-workgroup: accesses stay within workgroup bounds → barrier inserted.
//   - Unknown: cannot prove safety → fusion rejected.
//
// Fusion criteria (conservative):
//   1. No side-effecting ops between launches (calls are only allowed if
//      they are known-safe launch setup such as dim3 constructors).
//   2. Pointer-typed kernel args are either disjoint or safely shared.
//   3. Block dimensions are identical (same SSA value).
//   4. Both kernels are defined in the offload module.
//   5. Same stream (or both default).
//   6. For shared buffers, grid dimensions must also be identical.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/Dialect/Transforms/CIRTransformUtils.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cir-offload-fuse-kernels"

using namespace mlir;

namespace {

//===----------------------------------------------------------------------===//
// Index Expression Decomposition
//===----------------------------------------------------------------------===//

/// Linear decomposition of a buffer index into GPU coordinate terms.
/// Represents:
///   tidCoeff[d] * thread_id.d + bidBdimCoeff[d] * (block_id.d * block_dim.d)
///   + constOffset + argCoeff * argTerm
/// for each dimension d in {X, Y, Z}.
struct IndexExpr {
  bool valid = false;
  int64_t tidCoeff[3] = {};      // [X, Y, Z]
  int64_t bidBdimCoeff[3] = {};  // [X, Y, Z]
  int64_t constOffset = 0;
  mlir::Value argTerm;
  int64_t argCoeff = 0;

  bool hasThreadDep() const {
    return tidCoeff[0] != 0 || tidCoeff[1] != 0 || tidCoeff[2] != 0 ||
           bidBdimCoeff[0] != 0 || bidBdimCoeff[1] != 0 ||
           bidBdimCoeff[2] != 0;
  }

  bool operator==(const IndexExpr &o) const {
    return valid && o.valid &&
           tidCoeff[0] == o.tidCoeff[0] && tidCoeff[1] == o.tidCoeff[1] &&
           tidCoeff[2] == o.tidCoeff[2] &&
           bidBdimCoeff[0] == o.bidBdimCoeff[0] &&
           bidBdimCoeff[1] == o.bidBdimCoeff[1] &&
           bidBdimCoeff[2] == o.bidBdimCoeff[2] &&
           constOffset == o.constOffset &&
           argTerm == o.argTerm && argCoeff == o.argCoeff;
  }
};

/// Sentinel values for raw block_id / block_dim before they combine
/// in a multiply. Tracks which dimension they belong to.
enum class RawDimKind {
  None,
  BlockIdX, BlockIdY, BlockIdZ,
  BlockDimX, BlockDimY, BlockDimZ
};

struct DecomposeResult {
  IndexExpr expr;
  RawDimKind rawDim = RawDimKind::None;
};

static DecomposeResult decomposeIndexImpl(mlir::Value v, unsigned depth) {
  if (depth > 16)
    return {};

  mlir::Operation *defOp = v.getDefiningOp();

  // Block argument — kernel function parameter.
  if (!defOp) {
    if (mlir::isa<mlir::BlockArgument>(v)) {
      // Only treat entry block args as kernel args.
      auto *block = mlir::cast<mlir::BlockArgument>(v).getOwner();
      if (block->isEntryBlock()) {
        DecomposeResult r;
        r.expr.valid = true;
        r.expr.argTerm = v;
        r.expr.argCoeff = 1;
        return r;
      }
    }
    return {};
  }

  // Constants.
  auto constVal = cir::tryResolveToConstant(v);
  if (constVal) {
    DecomposeResult r;
    r.expr.valid = true;
    r.expr.constOffset = *constVal;
    return r;
  }

  // thread_id.{x,y,z}
  if (auto tid = dyn_cast<cir::OffloadThreadIdOp>(defOp)) {
    int dim = static_cast<int>(tid.getDimension());
    DecomposeResult r;
    r.expr.valid = true;
    r.expr.tidCoeff[dim] = 1;
    return r;
  }

  // block_id.{x,y,z} — raw, only valid in a multiply with block_dim
  if (auto bid = dyn_cast<cir::OffloadBlockIdOp>(defOp)) {
    int dim = static_cast<int>(bid.getDimension());
    DecomposeResult r;
    r.rawDim = static_cast<RawDimKind>(
        static_cast<int>(RawDimKind::BlockIdX) + dim);
    return r;
  }

  // block_dim.{x,y,z} — raw, only valid in a multiply with block_id
  if (auto bdim = dyn_cast<cir::OffloadBlockDimOp>(defOp)) {
    int dim = static_cast<int>(bdim.getDimension());
    DecomposeResult r;
    r.rawDim = static_cast<RawDimKind>(
        static_cast<int>(RawDimKind::BlockDimX) + dim);
    return r;
  }

  // Transparent casts — peel and recurse.
  if ((isa<cir::CastOp>(defOp) || isa<UnrealizedConversionCastOp>(defOp)) &&
      defOp->getNumOperands() == 1)
    return decomposeIndexImpl(defOp->getOperand(0), depth + 1);

  // cir.add
  if (auto addOp = dyn_cast<cir::AddOp>(defOp)) {
    auto lhs = decomposeIndexImpl(addOp.getLhs(), depth + 1);
    auto rhs = decomposeIndexImpl(addOp.getRhs(), depth + 1);
    if (!lhs.expr.valid || !rhs.expr.valid)
      return {};
    if (lhs.rawDim != RawDimKind::None || rhs.rawDim != RawDimKind::None)
      return {};
    // Both operands may have argTerms — if both have one, they must be the
    // same value or we can't combine.
    if (lhs.expr.argTerm && rhs.expr.argTerm &&
        lhs.expr.argTerm != rhs.expr.argTerm)
      return {};
    DecomposeResult r;
    r.expr.valid = true;
    for (int d = 0; d < 3; ++d) {
      r.expr.tidCoeff[d] = lhs.expr.tidCoeff[d] + rhs.expr.tidCoeff[d];
      r.expr.bidBdimCoeff[d] =
          lhs.expr.bidBdimCoeff[d] + rhs.expr.bidBdimCoeff[d];
    }
    r.expr.constOffset = lhs.expr.constOffset + rhs.expr.constOffset;
    r.expr.argTerm = lhs.expr.argTerm ? lhs.expr.argTerm : rhs.expr.argTerm;
    r.expr.argCoeff = lhs.expr.argCoeff + rhs.expr.argCoeff;
    return r;
  }

  // cir.sub
  if (auto subOp = dyn_cast<cir::SubOp>(defOp)) {
    auto lhs = decomposeIndexImpl(subOp.getLhs(), depth + 1);
    auto rhs = decomposeIndexImpl(subOp.getRhs(), depth + 1);
    if (!lhs.expr.valid || !rhs.expr.valid)
      return {};
    if (lhs.rawDim != RawDimKind::None || rhs.rawDim != RawDimKind::None)
      return {};
    if (lhs.expr.argTerm && rhs.expr.argTerm &&
        lhs.expr.argTerm != rhs.expr.argTerm)
      return {};
    DecomposeResult r;
    r.expr.valid = true;
    for (int d = 0; d < 3; ++d) {
      r.expr.tidCoeff[d] = lhs.expr.tidCoeff[d] - rhs.expr.tidCoeff[d];
      r.expr.bidBdimCoeff[d] =
          lhs.expr.bidBdimCoeff[d] - rhs.expr.bidBdimCoeff[d];
    }
    r.expr.constOffset = lhs.expr.constOffset - rhs.expr.constOffset;
    r.expr.argTerm = lhs.expr.argTerm ? lhs.expr.argTerm : rhs.expr.argTerm;
    r.expr.argCoeff = lhs.expr.argCoeff - rhs.expr.argCoeff;
    return r;
  }

  // cir.mul
  if (auto mulOp = dyn_cast<cir::MulOp>(defOp)) {
    auto lhs = decomposeIndexImpl(mulOp.getLhs(), depth + 1);
    auto rhs = decomposeIndexImpl(mulOp.getRhs(), depth + 1);

    // block_id.d * block_dim.d (either order, same dimension)
    auto tryBidBdimPair = [](RawDimKind a, RawDimKind b) -> int {
      // Returns the dimension index (0,1,2) if a and b form a
      // block_id.d * block_dim.d pair, or -1 otherwise.
      for (int d = 0; d < 3; ++d) {
        auto bidD = static_cast<RawDimKind>(
            static_cast<int>(RawDimKind::BlockIdX) + d);
        auto bdimD = static_cast<RawDimKind>(
            static_cast<int>(RawDimKind::BlockDimX) + d);
        if ((a == bidD && b == bdimD) || (a == bdimD && b == bidD))
          return d;
      }
      return -1;
    };
    int pairedDim = tryBidBdimPair(lhs.rawDim, rhs.rawDim);
    if (pairedDim >= 0) {
      DecomposeResult r;
      r.expr.valid = true;
      r.expr.bidBdimCoeff[pairedDim] = 1;
      return r;
    }

    // One side is a constant, other is a valid expression.
    if (lhs.expr.valid && rhs.expr.valid) {
      auto tryScale = [](const DecomposeResult &constSide,
                         const DecomposeResult &exprSide) -> DecomposeResult {
        if (constSide.rawDim != RawDimKind::None ||
            exprSide.rawDim != RawDimKind::None)
          return {};
        // constSide must be pure constant (no tid/bid/arg terms).
        if (constSide.expr.hasThreadDep() || constSide.expr.argCoeff != 0)
          return {};
        int64_t c = constSide.expr.constOffset;
        DecomposeResult r;
        r.expr.valid = true;
        for (int d = 0; d < 3; ++d) {
          r.expr.tidCoeff[d] = exprSide.expr.tidCoeff[d] * c;
          r.expr.bidBdimCoeff[d] = exprSide.expr.bidBdimCoeff[d] * c;
        }
        r.expr.constOffset = exprSide.expr.constOffset * c;
        r.expr.argTerm = exprSide.expr.argTerm;
        r.expr.argCoeff = exprSide.expr.argCoeff * c;
        return r;
      };
      auto r = tryScale(lhs, rhs);
      if (r.expr.valid)
        return r;
      r = tryScale(rhs, lhs);
      if (r.expr.valid)
        return r;
    }

    return {};
  }

  // cir.load — follow through unique store to alloca.
  if (auto loadOp = dyn_cast<cir::LoadOp>(defOp)) {
    mlir::Value addr = loadOp.getAddr();
    mlir::Operation *addrDef = addr.getDefiningOp();
    if (addrDef && isa<cir::AllocaOp>(addrDef)) {
      mlir::Operation *uniqueStore = nullptr;
      for (mlir::OpOperand &use : addr.getUses()) {
        auto *user = use.getOwner();
        if (auto store = dyn_cast<cir::StoreOp>(user)) {
          if (store.getAddr() == addr) {
            if (uniqueStore)
              return {};
            uniqueStore = user;
          }
        }
      }
      if (uniqueStore)
        return decomposeIndexImpl(
            cast<cir::StoreOp>(uniqueStore).getValue(), depth + 1);
    }
    return {};
  }

  return {};
}

static IndexExpr decomposeIndex(mlir::Value v) {
  auto result = decomposeIndexImpl(v, 0);
  if (result.rawDim != RawDimKind::None)
    return {};
  return result.expr;
}

//===----------------------------------------------------------------------===//
// Structural Index Fingerprinting
//===----------------------------------------------------------------------===//
//
// When decomposeIndex fails (e.g., `j * Lx + i` where Lx is a kernel arg),
// we fall back to structural comparison: two index computations are equivalent
// if they perform the same operations on equivalent inputs.
//
// A "fingerprint" is a string encoding of the computation DAG:
//   - thread_id.<dim> → "T<dim>"
//   - block_id.<dim> → "B<dim>"
//   - block_dim.<dim> → "D<dim>"
//   - kernel block arg at position N → "A<N>"
//   - constant C → "C<C>"
//   - add(L, R) → "(L+R)"
//   - mul(L, R) → "(L*R)" (operands sorted for commutativity)
//   - sub(L, R) → "(L-R)"
//   - cast(X) → fingerprint(X)
//   - load(alloca with unique store) → fingerprint(stored value)

static std::string fingerprintIndex(mlir::Value v, unsigned depth = 0) {
  if (depth > 16)
    return "?";

  mlir::Operation *defOp = v.getDefiningOp();

  if (!defOp) {
    if (auto ba = mlir::dyn_cast<mlir::BlockArgument>(v)) {
      if (ba.getOwner()->isEntryBlock())
        return "A" + std::to_string(ba.getArgNumber());
    }
    return "?";
  }

  if (auto c = cir::tryResolveToConstant(v))
    return "C" + std::to_string(*c);

  if (auto tid = dyn_cast<cir::OffloadThreadIdOp>(defOp))
    return "T" + std::to_string(static_cast<int>(tid.getDimension()));
  if (auto bid = dyn_cast<cir::OffloadBlockIdOp>(defOp))
    return "B" + std::to_string(static_cast<int>(bid.getDimension()));
  if (auto bdim = dyn_cast<cir::OffloadBlockDimOp>(defOp))
    return "D" + std::to_string(static_cast<int>(bdim.getDimension()));

  if ((isa<cir::CastOp>(defOp) || isa<UnrealizedConversionCastOp>(defOp)) &&
      defOp->getNumOperands() == 1)
    return fingerprintIndex(defOp->getOperand(0), depth + 1);

  if (auto addOp = dyn_cast<cir::AddOp>(defOp)) {
    auto l = fingerprintIndex(addOp.getLhs(), depth + 1);
    auto r = fingerprintIndex(addOp.getRhs(), depth + 1);
    return "(" + l + "+" + r + ")";
  }
  if (auto subOp = dyn_cast<cir::SubOp>(defOp)) {
    auto l = fingerprintIndex(subOp.getLhs(), depth + 1);
    auto r = fingerprintIndex(subOp.getRhs(), depth + 1);
    return "(" + l + "-" + r + ")";
  }
  if (auto mulOp = dyn_cast<cir::MulOp>(defOp)) {
    auto l = fingerprintIndex(mulOp.getLhs(), depth + 1);
    auto r = fingerprintIndex(mulOp.getRhs(), depth + 1);
    // Sort for commutativity.
    if (l > r) std::swap(l, r);
    return "(" + l + "*" + r + ")";
  }

  if (auto loadOp = dyn_cast<cir::LoadOp>(defOp)) {
    mlir::Value addr = loadOp.getAddr();
    mlir::Operation *addrDef = addr.getDefiningOp();
    if (addrDef && isa<cir::AllocaOp>(addrDef)) {
      mlir::Operation *uniqueStore = nullptr;
      for (mlir::OpOperand &use : addr.getUses()) {
        auto *user = use.getOwner();
        if (auto store = dyn_cast<cir::StoreOp>(user)) {
          if (store.getAddr() == addr) {
            if (uniqueStore) return "?";
            uniqueStore = user;
          }
        }
      }
      if (uniqueStore)
        return fingerprintIndex(
            cast<cir::StoreOp>(uniqueStore).getValue(), depth + 1);
    }
    return "?";
  }

  return "?";
}

/// Check if a fingerprint contains any thread/block coordinate references.
static bool fingerprintHasThreadDep(const std::string &fp) {
  return fp.find('T') != std::string::npos ||
         fp.find('B') != std::string::npos;
}

//===----------------------------------------------------------------------===//
// Buffer Access Pattern Analysis
//===----------------------------------------------------------------------===//

enum class BufferAccessPattern { PerThread, PerWorkgroup, Unknown };

struct BufferAccessInfo {
  BufferAccessPattern pattern = BufferAccessPattern::Unknown;
  IndexExpr uniformIndex;
  std::string fingerprint;
  bool hasRead = false;
  bool hasWrite = false;
};

static BufferAccessInfo analyzeBufferInKernel(cir::OffloadFuncOp kernel,
                                              unsigned argIdx) {
  BufferAccessInfo info;
  if (argIdx >= kernel.getNumArguments())
    return info;

  BlockArgument bufArg = kernel.getArgument(argIdx);
  if (!isa<cir::PointerType>(bufArg.getType()))
    return info;

  SmallVector<IndexExpr> indices;
  SmallVector<std::string> fingerprints;
  SmallVector<mlir::Value> worklist;
  llvm::SmallPtrSet<mlir::Value, 16> visited;
  worklist.push_back(bufArg);

  while (!worklist.empty()) {
    mlir::Value current = worklist.pop_back_val();
    if (!visited.insert(current).second)
      continue;

    for (mlir::OpOperand &use : current.getUses()) {
      mlir::Operation *user = use.getOwner();

      // ptr_stride — extract and decompose the index.
      if (auto stride = dyn_cast<cir::PtrStrideOp>(user)) {
        if (stride.getBase() == current) {
          IndexExpr idx = decomposeIndex(stride.getStride());
          if (idx.valid) {
            indices.push_back(idx);
          } else {
            // Fallback: compute structural fingerprint.
            std::string fp = fingerprintIndex(stride.getStride());
            if (fp.find('?') != std::string::npos) {
              LLVM_DEBUG(llvm::dbgs()
                         << "  analyzeBuffer: unfingerprintable index\n");
              return info;
            }
            fingerprints.push_back(fp);
          }
          worklist.push_back(stride.getResult());
          continue;
        }
      }

      // Load from the pointer (direct or after stride).
      if (auto load = dyn_cast<cir::LoadOp>(user)) {
        if (load.getAddr() == current) {
          info.hasRead = true;
          continue;
        }
      }

      // Store to the pointer (writing through the buffer).
      if (auto store = dyn_cast<cir::StoreOp>(user)) {
        if (store.getAddr() == current) {
          info.hasWrite = true;
          continue;
        }
        // Store of the pointer VALUE to an alloca — follow loads from
        // that alloca to track the buffer through local variables.
        if (store.getValue() == current) {
          mlir::Value addr = store.getAddr();
          if (addr.getDefiningOp() &&
              isa<cir::AllocaOp>(addr.getDefiningOp())) {
            for (mlir::OpOperand &allocaUse : addr.getUses()) {
              if (auto load = dyn_cast<cir::LoadOp>(allocaUse.getOwner())) {
                if (load.getAddr() == addr)
                  worklist.push_back(load.getResult());
              }
            }
            continue;
          }
        }
      }

      // Casts — follow through.
      if (isa<cir::CastOp>(user) && user->getNumResults() == 1) {
        worklist.push_back(user->getResult(0));
        continue;
      }

      // Any other use (call, return, etc.) — cannot analyze.
      LLVM_DEBUG(llvm::dbgs()
                 << "  analyzeBuffer: unknown use: " << *user << "\n");
      return info;
    }
  }

  // If we have fingerprints (from decomposition fallback), use those.
  if (!fingerprints.empty()) {
    // All fingerprints must be identical and thread-dependent.
    bool allFpSame = true;
    for (unsigned i = 1; i < fingerprints.size(); ++i) {
      if (fingerprints[i] != fingerprints[0]) {
        allFpSame = false;
        break;
      }
    }
    // Also verify no decomposed indices conflict.
    for (auto &idx : indices) {
      if (idx.valid) {
        // Can't mix fingerprinted and decomposed indices reliably.
        return info;
      }
    }
    if (allFpSame && fingerprintHasThreadDep(fingerprints[0])) {
      info.pattern = BufferAccessPattern::PerThread;
      info.fingerprint = fingerprints[0];
    }
    return info;
  }

  if (indices.empty()) {
    // Buffer is accessed at index 0 only (direct load/store).
    IndexExpr zeroIdx;
    zeroIdx.valid = true;
    indices.push_back(zeroIdx);
  }

  // All indices must be identical for PerThread classification.
  bool allSame = true;
  for (unsigned i = 1; i < indices.size(); ++i) {
    if (!(indices[i] == indices[0])) {
      allSame = false;
      break;
    }
  }

  if (allSame && indices[0].hasThreadDep()) {
    info.pattern = BufferAccessPattern::PerThread;
    info.uniformIndex = indices[0];
  } else if (allSame && !indices[0].hasThreadDep() &&
             indices[0].argCoeff == 0) {
    // Uniform access (same element by all threads) — per-workgroup at best.
    info.pattern = BufferAccessPattern::PerWorkgroup;
    info.uniformIndex = indices[0];
  } else {
    // Check if all indices stay within workgroup bounds (no bidBdim term
    // in any dimension, tid-only with different coefficients).
    bool allWorkgroupLocal = true;
    for (auto &idx : indices) {
      if (idx.bidBdimCoeff[0] != 0 || idx.bidBdimCoeff[1] != 0 ||
          idx.bidBdimCoeff[2] != 0) {
        allWorkgroupLocal = false;
        break;
      }
    }
    if (allWorkgroupLocal)
      info.pattern = BufferAccessPattern::PerWorkgroup;
  }

  return info;
}

//===----------------------------------------------------------------------===//
// Sync Requirement
//===----------------------------------------------------------------------===//

enum class SyncRequirement { None, WorkgroupBarrier, Unsafe };

static SyncRequirement determineSyncRequirement(const BufferAccessInfo &infoA,
                                                const BufferAccessInfo &infoB) {
  // If either side is Unknown, we can't prove safety.
  if (infoA.pattern == BufferAccessPattern::Unknown ||
      infoB.pattern == BufferAccessPattern::Unknown)
    return SyncRequirement::Unsafe;

  // Read-read is always safe (only checked after pattern is known).
  if (!infoA.hasWrite && !infoB.hasWrite)
    return SyncRequirement::None;

  // Both PerThread with the same index — no sync needed.
  if (infoA.pattern == BufferAccessPattern::PerThread &&
      infoB.pattern == BufferAccessPattern::PerThread) {
    // Compare via IndexExpr if both have valid decompositions.
    if (infoA.uniformIndex.valid && infoB.uniformIndex.valid) {
      if (infoA.uniformIndex == infoB.uniformIndex)
        return SyncRequirement::None;
      return SyncRequirement::Unsafe;
    }
    // Compare via structural fingerprint if both have one.
    if (!infoA.fingerprint.empty() && !infoB.fingerprint.empty()) {
      if (infoA.fingerprint == infoB.fingerprint)
        return SyncRequirement::None;
      return SyncRequirement::Unsafe;
    }
    // Mixed (one decomposed, one fingerprinted) — can't compare safely.
    return SyncRequirement::Unsafe;
  }

  // Any workgroup-level pattern — insert a barrier.
  if ((infoA.pattern == BufferAccessPattern::PerWorkgroup ||
       infoA.pattern == BufferAccessPattern::PerThread) &&
      (infoB.pattern == BufferAccessPattern::PerWorkgroup ||
       infoB.pattern == BufferAccessPattern::PerThread))
    return SyncRequirement::WorkgroupBarrier;

  return SyncRequirement::Unsafe;
}

/// Analyze shared buffers between two launches and determine the sync
/// requirement for safe fusion. Sets hasSharedBuffers to true if any
/// pointer args trace to the same origin.
static SyncRequirement
analyzeSharedBuffers(cir::OffloadKernelLaunchOp launchA,
                     cir::OffloadKernelLaunchOp launchB,
                     cir::OffloadFuncOp kernelA,
                     cir::OffloadFuncOp kernelB,
                     bool &hasSharedBuffers) {
  SyncRequirement maxReq = SyncRequirement::None;
  hasSharedBuffers = false;

  // Build origin map for A's pointer operands.
  struct PtrInfo {
    unsigned launchArgIdx;
    mlir::Value origin;
  };
  SmallVector<PtrInfo> ptrsA, ptrsB;

  for (auto [i, arg] : llvm::enumerate(launchA.getKernelOperands())) {
    if (isa<cir::PointerType>(arg.getType())) {
      auto result = cir::traceValueOrigin(arg);
      ptrsA.push_back({(unsigned)i, result.terminal});
    }
  }
  for (auto [i, arg] : llvm::enumerate(launchB.getKernelOperands())) {
    if (isa<cir::PointerType>(arg.getType())) {
      auto result = cir::traceValueOrigin(arg);
      ptrsB.push_back({(unsigned)i, result.terminal});
    }
  }

  // Check each pair for shared origins.
  for (auto &pA : ptrsA) {
    for (auto &pB : ptrsB) {
      if (pA.origin != pB.origin)
        continue;

      hasSharedBuffers = true;

      LLVM_DEBUG(llvm::dbgs()
                 << "  Shared buffer: A arg " << pA.launchArgIdx << ", B arg "
                 << pB.launchArgIdx << "\n");

      auto infoA = analyzeBufferInKernel(kernelA, pA.launchArgIdx);
      auto infoB = analyzeBufferInKernel(kernelB, pB.launchArgIdx);

      LLVM_DEBUG(llvm::dbgs()
                 << "    A pattern=" << (int)infoA.pattern
                 << " read=" << infoA.hasRead << " write=" << infoA.hasWrite
                 << "\n");
      LLVM_DEBUG(llvm::dbgs()
                 << "    B pattern=" << (int)infoB.pattern
                 << " read=" << infoB.hasRead << " write=" << infoB.hasWrite
                 << "\n");

      auto req = determineSyncRequirement(infoA, infoB);
      if (req == SyncRequirement::Unsafe)
        return SyncRequirement::Unsafe;
      if (req > maxReq)
        maxReq = req;
    }
  }

  return maxReq;
}

//===----------------------------------------------------------------------===//
// Fusion Safety Checks
//===----------------------------------------------------------------------===//

/// Check if an op between two launches is safe for fusion.
/// Safe ops: loads, stores (stack-local), consts, casts, get_member, binop,
/// alloca.  Calls are safe only if they're dim3 constructors or other
/// known-safe launch setup.
static bool isSafeInterveningOp(Operation *op) {
  // Launch ops themselves are not "intervening".
  if (isa<cir::OffloadKernelLaunchOp>(op))
    return true;

  // Pure value-producing ops, stack-local stores, and unconditional branches.
  if (isa<cir::LoadOp, cir::StoreOp, cir::ConstantOp, cir::CastOp,
          cir::GetMemberOp, cir::CmpOp, cir::AllocaOp, cir::SelectOp,
          cir::AddOp, cir::SubOp, cir::MulOp, cir::NotOp, cir::FNegOp,
          cir::PtrStrideOp, cir::CopyOp, cir::DivOp, cir::BrOp>(op))
    return true;

  // dim3 constructor calls are launch setup — safe.
  if (auto call = dyn_cast<cir::CallOp>(op)) {
    if (auto callee = call.getCalleeAttr()) {
      StringRef name = callee.getValue();
      // dim3::dim3(unsigned, unsigned, unsigned)
      if (name.starts_with("_ZN4dim3C"))
        return true;
    }
  }

  return false;
}

/// Compare two values: either same SSA value, both resolve to the
/// same constant, or both trace to the same origin.
static bool valuesMatch(mlir::Value a, mlir::Value b) {
  if (a == b)
    return true;
  auto ca = cir::tryResolveToConstant(a);
  auto cb = cir::tryResolveToConstant(b);
  if (ca && cb)
    return *ca == *cb;
  auto oa = cir::traceValueOrigin(a);
  auto ob = cir::traceValueOrigin(b);
  return oa.terminal && ob.terminal && oa.terminal == ob.terminal;
}

/// Check if two launches have identical block dimensions.
static bool blockDimsMatch(cir::OffloadKernelLaunchOp a,
                           cir::OffloadKernelLaunchOp b) {
  return valuesMatch(a.getBlockSizeX(), b.getBlockSizeX()) &&
         valuesMatch(a.getBlockSizeY(), b.getBlockSizeY()) &&
         valuesMatch(a.getBlockSizeZ(), b.getBlockSizeZ());
}

/// Check if two launches have identical grid dimensions.
static bool gridDimsMatch(cir::OffloadKernelLaunchOp a,
                          cir::OffloadKernelLaunchOp b) {
  return valuesMatch(a.getGridSizeX(), b.getGridSizeX()) &&
         valuesMatch(a.getGridSizeY(), b.getGridSizeY()) &&
         valuesMatch(a.getGridSizeZ(), b.getGridSizeZ());
}

/// Check if two launches have the same stream (or both default).
static bool streamsMatch(cir::OffloadKernelLaunchOp a,
                         cir::OffloadKernelLaunchOp b) {
  auto sA = a.getStream();
  auto sB = b.getStream();
  if (!sA && !sB)
    return true; // both default
  if (sA && sB)
    return sA == sB; // same SSA value
  return false;
}

struct OffloadFuseKernelsPass
    : public PassWrapper<OffloadFuseKernelsPass, OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(OffloadFuseKernelsPass)

  OffloadFuseKernelsPass() = default;
  OffloadFuseKernelsPass(bool enabled) : passEnabled(enabled) {}

  StringRef getArgument() const override {
    return "cir-offload-fuse-kernels";
  }
  StringRef getDescription() const override {
    return "Fuse consecutive kernel launches on disjoint or safely-shared "
           "buffers";
  }

  void runOnOperation() override {
    if (!passEnabled)
      return;

    ModuleOp module = getOperation();
    MLIRContext *ctx = module.getContext();

    // Count launches to verify pass is running.

    // Single pass — no chaining for now (fusing A+B then (A+B)+C
    // requires grid guard support for different grid dims).
    {

      // Collect all offload modules.
      SmallVector<cir::OffloadModuleOp> offloadMods;
      module.walk([&](cir::OffloadModuleOp mod) {
        if (!mod.getBody().empty())
          offloadMods.push_back(mod);
      });

      if (offloadMods.empty())
        return;

      // Find fusible pairs by walking all blocks for consecutive launches.
      struct FusiblePair {
        cir::OffloadKernelLaunchOp launchA, launchB;
        cir::OffloadModuleOp kernelMod;
        SyncRequirement syncReq;
      };
      SmallVector<FusiblePair> fusiblePairs;

      // Helper: walk forward from an op through safe ops and unconditional
      // branches to find the next kernel_launch.
      auto findNextLaunch =
          [](cir::OffloadKernelLaunchOp launchA)
          -> std::optional<cir::OffloadKernelLaunchOp> {
        // First check remaining ops in the same block.
        for (auto it = std::next(launchA->getIterator()),
                  end = launchA->getBlock()->end();
             it != end; ++it) {
          if (auto launch = dyn_cast<cir::OffloadKernelLaunchOp>(&*it))
            return launch;
          if (!isSafeInterveningOp(&*it))
            return std::nullopt;
        }
        // Follow unconditional branches (cir.br) to successor blocks.
        Block *block = launchA->getBlock();
        for (unsigned depth = 0; depth < 8; ++depth) {
          auto *term = block->getTerminator();
          if (!term)
            return std::nullopt;
          auto br = dyn_cast<cir::BrOp>(term);
          if (!br)
            return std::nullopt;
          block = br->getSuccessor(0);
          for (auto &op : *block) {
            if (auto launch = dyn_cast<cir::OffloadKernelLaunchOp>(&op))
              return launch;
            if (!isSafeInterveningOp(&op))
              return std::nullopt;
          }
        }
        return std::nullopt;
      };

      // Collect all kernel launches in the module.
      SmallVector<cir::OffloadKernelLaunchOp> allLaunches;
      module.walk([&](cir::OffloadKernelLaunchOp op) {
        allLaunches.push_back(op);
      });

      // Check consecutive pairs (same block) AND cross-block pairs
      // (connected by unconditional branches through safe ops).
      llvm::SmallPtrSet<Operation *, 8> visitedLaunches;
      for (auto launchA : allLaunches) {
        if (visitedLaunches.count(launchA))
          continue;

        auto nextLaunchOpt = findNextLaunch(launchA);
        if (!nextLaunchOpt)
          continue;
        auto launchB = *nextLaunchOpt;
        visitedLaunches.insert(launchB);


          // Check block dims match.
          if (!blockDimsMatch(launchA, launchB))
            continue;

          // Check streams match.
          if (!streamsMatch(launchA, launchB)) {
            continue;
          }

          // Check both kernels are in the same offload module and defined.
          StringRef modNameA =
              launchA.getKernel().getRootReference().getValue();
          StringRef modNameB =
              launchB.getKernel().getRootReference().getValue();
          if (modNameA != modNameB)
            continue;

          StringRef kernelNameA =
              launchA.getKernel().getLeafReference().getValue();
          StringRef kernelNameB =
              launchB.getKernel().getLeafReference().getValue();

          // In two-pass mode, kernels may be in @offload_device_module_<arch>
          // while launches reference @offload_device_module.  Search all
          // offload modules for the kernel definitions.
          cir::OffloadModuleOp kernelMod;
          for (auto mod : offloadMods) {
            if (mod.lookupSymbol<cir::OffloadFuncOp>(kernelNameA) &&
                mod.lookupSymbol<cir::OffloadFuncOp>(kernelNameB)) {
              kernelMod = mod;
              break;
            }
          }
          if (!kernelMod) {
            auto offloadMod =
                module.lookupSymbol<cir::OffloadModuleOp>(modNameA);
            if (offloadMod &&
                offloadMod.lookupSymbol<cir::OffloadFuncOp>(kernelNameA))
              kernelMod = offloadMod;
          }
          if (!kernelMod)
            continue;

          auto kernelA =
              kernelMod.lookupSymbol<cir::OffloadFuncOp>(kernelNameA);
          auto kernelB =
              kernelMod.lookupSymbol<cir::OffloadFuncOp>(kernelNameB);

          if (!kernelA || !kernelB || kernelA.isExternal() ||
              kernelB.isExternal())
            continue;

          // Analyze shared buffers and determine sync requirement.
          bool hasSharedBuffers = false;
          auto syncReq = analyzeSharedBuffers(launchA, launchB, kernelA,
                                              kernelB, hasSharedBuffers);
          if (syncReq == SyncRequirement::Unsafe)
            continue;

          // Shared-buffer fusion requires matching grid dims too.
          if (hasSharedBuffers && !gridDimsMatch(launchA, launchB))
            continue;

          fusiblePairs.push_back({launchA, launchB, kernelMod, syncReq});
      }

      // Apply fusions (one pass, no chaining).
      // Fusing erases the original launch ops, so stop after the first
      // successful fusion to avoid using dangling references.
      for (auto &pair : fusiblePairs) {
        if (fuseKernelPair(ctx, module, pair.kernelMod,
                           pair.launchA, pair.launchB, pair.syncReq))
          break;
      }
    }
  }

  bool fuseKernelPair(MLIRContext *ctx, ModuleOp module,
                      cir::OffloadModuleOp kernelMod,
                      cir::OffloadKernelLaunchOp launchA,
                      cir::OffloadKernelLaunchOp launchB,
                      SyncRequirement syncReq) {
    StringRef modName = launchA.getKernel().getRootReference().getValue();
    StringRef kernelNameA = launchA.getKernel().getLeafReference().getValue();
    StringRef kernelNameB = launchB.getKernel().getLeafReference().getValue();


    SymbolTable symTable(kernelMod);

    auto kernelA = symTable.lookup<cir::OffloadFuncOp>(kernelNameA);
    auto kernelB = symTable.lookup<cir::OffloadFuncOp>(kernelNameB);
    if (!kernelA || !kernelB) {
      return false;
    }

    // Build fused kernel name.
    std::string fusedName =
        (kernelNameA + "$fused$" + kernelNameB).str();
    if (symTable.lookup(fusedName)) {
      return false;
    }

    // Build merged arg list. Deduplicate pointer args that are the same
    // SSA value at the launch site.
    SmallVector<Value> fusedLaunchArgs;
    SmallVector<Type> fusedArgTypes;
    llvm::DenseMap<Value, unsigned> argDedup; // launchArg → fusedArgIdx

    // Add all of A's args.
    for (auto arg : launchA.getKernelOperands()) {
      argDedup[arg] = fusedLaunchArgs.size();
      fusedLaunchArgs.push_back(arg);
      fusedArgTypes.push_back(arg.getType());
    }

    // Add B's args, deduplicating pointers.
    SmallVector<unsigned> bArgToFusedIdx;
    for (auto arg : launchB.getKernelOperands()) {
      auto it = argDedup.find(arg);
      if (it != argDedup.end() && isa<cir::PointerType>(arg.getType())) {
        bArgToFusedIdx.push_back(it->second);
      } else {
        bArgToFusedIdx.push_back(fusedLaunchArgs.size());
        fusedLaunchArgs.push_back(arg);
        fusedArgTypes.push_back(arg.getType());
      }
    }

    // Create the fused kernel function (void return, like original kernels).
    auto fusedFnTy = cir::FuncType::get(fusedArgTypes,
                                        kernelA.getFunctionType().getReturnType(),
                                        /*isVarArg=*/false);
    // Insert the fused kernel definition into kernelMod (offload_device_module_gfx90a)
    // where the original kernels live. SplitSingleSource will move it to
    // offload_device_module along with the other primary kernels.
    OpBuilder builder(ctx);
    builder.setInsertionPointToEnd(&kernelMod.getBody().front());

    auto fusedKernel = cir::OffloadFuncOp::create(builder, kernelA.getLoc(),
                                                   fusedName, fusedFnTy,
                                                   /*isKernel=*/true);
    fusedKernel.getBody().emplaceBlock();
    Block &fusedEntry = fusedKernel.getBody().front();

    // Add block arguments matching fusedArgTypes.
    for (auto ty : fusedArgTypes)
      fusedEntry.addArgument(ty, kernelA.getLoc());

    // Set arg_attrs: mark all args as {llvm.noundef} to prevent
    // AMDGPUAttributor from inferring readnone.
    SmallVector<Attribute> fusedArgAttrs;
    auto noundefAttr = builder.getNamedAttr("llvm.noundef",
                                             UnitAttr::get(ctx));
    for (unsigned i = 0; i < fusedArgTypes.size(); ++i)
      fusedArgAttrs.push_back(
          DictionaryAttr::get(ctx, {noundefAttr}));
    fusedKernel.setArgAttrsAttr(ArrayAttr::get(ctx, fusedArgAttrs));

    // Clone A's entire body (all blocks) into the fused kernel.
    // After FlattenCFG, kernel bodies have multiple blocks with
    // branches/conditional branches — we must clone ALL blocks.
    //
    // Strategy: erase the empty fusedEntry, clone A's region into the fused
    // body, then clone B's region. Fix up entry block arguments to use
    // the fused kernel's combined argument list.

    // Remove the placeholder entry block.
    fusedEntry.erase();

    // Clone A's body — map A's block args to themselves initially;
    // cloneInto will create new block args in the cloned blocks.
    // Collect A's returns across scopes — they'll be replaced with branches
    // to B's entry after B's body is cloned.
    SmallVector<cir::ReturnOp> aReturns;

    {
      IRMapping mapA;
      kernelA.getBody().cloneInto(&fusedKernel.getBody(), mapA);

      // The cloned entry block has A's argument types.
      Block &newEntry = fusedKernel.getBody().front();

      // Add B's extra args to the entry block.
      for (unsigned i = kernelA.getNumArguments();
           i < fusedArgTypes.size(); ++i)
        newEntry.addArgument(fusedArgTypes[i], kernelA.getLoc());

      // Collect ALL of A's cir.return terminators.
      for (Block &blk : fusedKernel.getBody()) {
        if (auto ret = dyn_cast_or_null<cir::ReturnOp>(blk.getTerminator()))
          aReturns.push_back(ret);
      }
    }

    // Clone B's body, appending blocks after A's.
    {
      IRMapping mapB;
      Block &bEntry = kernelB.getBody().front();
      // Map B's entry args to the fused kernel's corresponding args.
      Block &fusedEntryBlock = fusedKernel.getBody().front();
      for (unsigned i = 0; i < bEntry.getNumArguments(); ++i)
        mapB.map(bEntry.getArgument(i),
                 fusedEntryBlock.getArgument(bArgToFusedIdx[i]));

      // Remember where A's blocks end.
      Block *lastABlock = &fusedKernel.getBody().back();

      kernelB.getBody().cloneInto(&fusedKernel.getBody(), mapB);

      // The first new block (after lastABlock) is B's cloned entry.
      Block *bClonedEntry = lastABlock->getNextNode();

      // Replace ALL of A's cir.return ops with branches to B's entry.
      // This handles early-return branches (e.g., bounds checks) so that
      // every path through A's body proceeds to B's body.
      for (auto ret : aReturns) {
        OpBuilder retBuilder(ctx);
        retBuilder.setInsertionPoint(ret);
        if (syncReq == SyncRequirement::WorkgroupBarrier)
          cir::OffloadBarrierOp::create(retBuilder, ret.getLoc());
        cir::BrOp::create(retBuilder, ret.getLoc(), bClonedEntry);
        ret.erase();
      }

      // B's args were already mapped via IRMapping, so the cloned ops
      // reference the fused entry args directly. Remove B's cloned entry
      // block args (they're unused — IRMapping resolved them).
      while (bClonedEntry->getNumArguments() > 0)
        bClonedEntry->eraseArgument(0);
    }

    // Also add a declaration in the launch module so SplitSingleSource
    // can discover the kernel from gpu.launch_func callees.
    StringRef kernelModName = kernelMod.getSymName();
    if (modName != kernelModName) {
      auto launchMod = module.lookupSymbol<cir::OffloadModuleOp>(modName);
      if (launchMod) {
        if (launchMod.getBody().empty())
          launchMod.getBody().emplaceBlock();
        if (!launchMod.lookupSymbol(fusedName)) {
          OpBuilder declBuilder(ctx);
          declBuilder.setInsertionPointToEnd(&launchMod.getBody().front());
          cir::OffloadFuncOp::create(
              declBuilder, fusedKernel.getLoc(), fusedName, fusedFnTy,
              /*isKernel=*/true);
        }
      }
    }

    // Insert at launchB's position — all operands from both launches
    // are defined by this point.
    builder.setInsertionPoint(launchB);
    cir::OffloadKernelLaunchOp::create(
        builder, launchA.getLoc(),
        SymbolRefAttr::get(ctx, modName,
                           {FlatSymbolRefAttr::get(ctx, fusedName)}),
        launchA.getGridSizeX(), launchA.getGridSizeY(),
        launchA.getGridSizeZ(), launchA.getBlockSizeX(),
        launchA.getBlockSizeY(), launchA.getBlockSizeZ(),
        launchA.getDynamicSharedMemorySize(), fusedLaunchArgs,
        launchA.getStream());

    // Erase the original launches.
    launchB->erase();
    launchA->erase();


    return true;
  }

  bool passEnabled = true;
};

} // namespace

std::unique_ptr<Pass> mlir::createOffloadFuseKernelsPass(bool enabled) {
  return std::make_unique<OffloadFuseKernelsPass>(enabled);
}
