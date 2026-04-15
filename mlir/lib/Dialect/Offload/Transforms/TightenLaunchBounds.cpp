//===- TightenLaunchBounds.cpp - Tighten kernel launch bounds -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// For each offload.func with exec_space=global, this pass inspects all
// offload.kernel_launch ops that reference it and attempts to fold the product
// blockX * blockY * blockZ to a compile-time constant.
//
// If the static block total fits within a smaller candidate bound from
// {warpSize, 256, 512}, a specialised clone of the kernel is created with that
// tighter launch_bounds and the launch op is redirected to the clone.  The
// original kernel is always preserved because it may be called by name through
// the HIP runtime API (hipModuleGetFunction) in ways the IR cannot observe.
//
// The warp size is inferred from the offload.target attribute on the module:
//   - AMD gfxN, N < 1000  (CDNA / GFX6–9): wavefront size 64
//   - AMD gfxN, N >= 1000 (RDNA / GFX10+): wavefront size 32
//   - NVIDIA sm_*:                          warp size 32
// When multiple architectures are listed and their warp sizes differ, the
// largest warp size is used — this is the most conservative safe choice, as it
// avoids creating a bound smaller than any target's wavefront size.
// If no target attribute is present, the warp-size candidate is omitted and
// only the fixed candidates {256, 512} are considered.
//
// Cloned kernels are named @original$maxN (e.g. @vecAdd$max256).  The '$'
// character is valid in MLIR bare identifiers but not in standard C++
// identifiers, guaranteeing no collision with user-defined symbols.
//
// This pass must run before SplitSingleSourcePass, which is the only point
// where both the offload.func callee and its launch sites are co-visible.
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/Offload/Transforms/Passes.h"

#include "mlir/Dialect/Offload/IR/OffloadDialect.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"

namespace mlir {
namespace offload {

#define GEN_PASS_DEF_OFFLOADTIGHTENLAUNCHBOUNDSPASS
#include "mlir/Dialect/Offload/Transforms/Passes.h.inc"

} // namespace offload
} // namespace mlir

using namespace mlir;
using namespace mlir::offload;

namespace {

//===----------------------------------------------------------------------===//
// Target warp-size inference
//===----------------------------------------------------------------------===//

/// Return the wavefront/warp size for a single architecture string, or
/// std::nullopt if the string is not recognised.
///
/// AMD gfxN: the numeric part determines the generation.
///   N < 1000  → GFX6–9 / CDNA, wavefront size 64
///   N >= 1000 → GFX10+ / RDNA, wavefront size 32
///
/// NVIDIA sm_*: warp size is always 32.
static std::optional<int64_t> getArchWarpSize(StringRef arch) {
  if (arch.starts_with("gfx")) {
    uint64_t num = 0;
    // consumeInteger returns true on failure (no digits found).
    if (arch.drop_front(3).consumeInteger(10, num))
      return std::nullopt;
    return num >= 1000 ? 32 : 64;
  }
  if (arch.starts_with("sm_"))
    return 32;
  return std::nullopt;
}

/// Infer the effective warp size from the offload.target attribute.
///
/// When architectures have differing warp sizes, we take the maximum.  This
/// is the most conservative choice: it avoids choosing a bound smaller than
/// any target's wavefront size, which would be suboptimal for that target
/// (the hardware still executes a full wavefront regardless).
///
/// Returns std::nullopt when the attribute is absent or no architecture in
/// the list is recognised.
static std::optional<int64_t> inferWarpSize(ModuleOp module) {
  auto target =
      module->getAttrOfType<offload::TargetAttr>("offload.target");
  if (!target)
    return std::nullopt;

  std::optional<int64_t> result;
  for (Attribute archAttr : target.getArchitectures()) {
    auto arch = cast<StringAttr>(archAttr).getValue();
    if (auto ws = getArchWarpSize(arch))
      result = result ? std::max(*result, *ws) : *ws;
  }
  return result;
}

//===----------------------------------------------------------------------===//
// Bucket selection
//===----------------------------------------------------------------------===//

/// Return the smallest candidate bound >= blockTotal, or -1 if no candidate
/// improves on the implicit 1024.
///
/// Candidates are {warpSize, 256, 512} when warpSize is known, or {256, 512}
/// when it is not.  The warp-size candidate captures the common pattern of
/// launching exactly one wavefront per block.
///
/// Precondition: if warpSize is present, it must be < 256.  Real GPU warp
/// sizes are 32 (RDNA/NVIDIA) or 64 (CDNA/GFX9); values >= 256 are asserted.
static int64_t pickBucket(int64_t blockTotal,
                          std::optional<int64_t> warpSize) {
  if (warpSize)
    assert(*warpSize < 256 && "warpSize must be less than 256");

  // Build the candidate list in ascending order.  The fixed entries 256 and
  // 512 always appear; warpSize is prepended when known (and it is < 256 by
  // the precondition, so the list stays sorted).
  SmallVector<int64_t, 3> cands;
  if (warpSize)
    cands.push_back(*warpSize);
  cands.push_back(256);
  cands.push_back(512);

  for (int64_t c : cands)
    if (c >= blockTotal)
      return c;
  return -1; // blockTotal > 512 — no improvement possible
}

//===----------------------------------------------------------------------===//
// Constant folding through CIR and MLIR ops
//===----------------------------------------------------------------------===//

/// Extract a compile-time integer from an Attribute, handling both the
/// standard mlir::IntegerAttr and the CIR-specific cir::IntAttr.
///
/// CIR does not register mlir::IntegerAttr for its integer types, so
/// m_ConstantInt / matchPattern fail on cir.const results.  We fall back to
/// printing the attribute and parsing the integer out of the "#cir.int<N>"
/// representation.  This is called only for block-dim constants (once per
/// launch site per dimension), so the string round-trip overhead is negligible.
///
/// Returns the integer value, or std::nullopt if extraction fails.
static std::optional<int64_t> tryExtractAttrInt(Attribute attr) {
  // Standard case: mlir::IntegerAttr (covers arith constants, etc.).
  if (auto ia = dyn_cast<IntegerAttr>(attr))
    return ia.getValue().getZExtValue();

  // CIR case: cir::IntAttr.  The attribute prints as "#cir.int<N>".
  // We detect it by its dialect namespace and extract N via string parsing.
  if (attr.getDialect().getNamespace() == "cir") {
    std::string buf;
    llvm::raw_string_ostream os(buf);
    attr.print(os);
    os.flush();
    StringRef s(buf);
    auto ltPos = s.find('<');
    auto gtPos = s.rfind('>');
    if (ltPos != StringRef::npos && gtPos != StringRef::npos &&
        gtPos > ltPos) {
      StringRef numStr = s.slice(ltPos + 1, gtPos).trim();
      int64_t val = 0;
      if (!numStr.consumeInteger(10, val))
        return val;
      if (numStr.starts_with("-")) {
        numStr = numStr.drop_front(1);
        if (!numStr.consumeInteger(10, val))
          return -val;
      }
    }
  }
  return std::nullopt;
}

/// Try to resolve a Value to a compile-time integer constant.
///
/// Handles the direct case (m_ConstantInt) and the multi-step chain that CIR
/// emits when a kernel is launched with a dim3 block argument.
///
/// The HIP/CIR lowering for `kernel<<<grid, block>>>();` with
/// `dim3 block(BDIM, BDIM, 1)` produces (in the host function body):
///
///   %block    = cir.alloca !rec_dim3, ..., ["block", init]
///   %dim3.tmp = cir.alloca !rec_dim3, ..., ["dim3.tmp"]
///   cir.call @_ZN4dim3C2Ejjj(%block, %c16, %c16, %c1)  ← constants here
///   cir.copy %block to %dim3.tmp                        ← dst=operand(0)
///   %ptr = cir.get_member %dim3.tmp[fieldIndex]
///   %val = cir.load %ptr
///   %i32 = unrealized_conversion_cast %val : !u32i to i32
///   %idx = arith.index_castui %i32 : i32 to index       ← launch arg
///
/// We trace backwards through:
///   arith.index_castui → unrealized_conversion_cast → cir.load →
///   cir.get_member (captures fieldIndex) →
///   cir.alloca (dim3.tmp) → uses search → cir.copy → src = block alloca →
///   search the enclosing function region for the cir.call that initialises
///   the block alloca → pick constructor argument at fieldIndex+1.
///
/// Constructor args are cir.const ops whose value is a cir::IntAttr,
/// handled by tryExtractAttrInt since cir::IntAttr ≠ mlir::IntegerAttr.
static std::optional<int64_t> tryResolveIndexToConstant(Value v) {
  // Fast path: value is already a compile-time integer constant (arith case).
  APInt directConst;
  if (matchPattern(v, m_ConstantInt(&directConst)))
    return directConst.getZExtValue();

  Operation *defOp = v.getDefiningOp();
  if (!defOp)
    return std::nullopt;

  // CIR constant: cir.const #cir.int<N> — not covered by m_ConstantInt since
  // cir::IntAttr is not mlir::IntegerAttr.
  if (defOp->getName().getStringRef() == "cir.const" &&
      defOp->getNumResults() == 1) {
    Attribute valAttr = defOp->getAttr("value");
    if (!valAttr) {
      // The "value" attribute may be stored positionally; scan to find it.
      for (NamedAttribute na : defOp->getAttrDictionary()) {
        valAttr = na.getValue();
        break;
      }
    }
    if (valAttr)
      return tryExtractAttrInt(valAttr);
  }

  // Strip type-cast wrappers that don't change the integer value.
  StringRef opName = defOp->getName().getStringRef();

  // arith.index_castui: index ← i32 / i64.
  if (opName == "arith.index_castui") {
    if (defOp->getNumOperands() == 1)
      return tryResolveIndexToConstant(defOp->getOperand(0));
    return std::nullopt;
  }

  // builtin.unrealized_conversion_cast: !u32i ← i32, etc.
  if (opName == "builtin.unrealized_conversion_cast") {
    if (defOp->getNumOperands() == 1)
      return tryResolveIndexToConstant(defOp->getOperand(0));
    return std::nullopt;
  }

  // cir.cast: integral widening / narrowing.
  if (opName.starts_with("cir.cast") || opName == "cir.unary") {
    if (defOp->getNumOperands() == 1)
      return tryResolveIndexToConstant(defOp->getOperand(0));
    return std::nullopt;
  }

  // cir.load — the value comes from memory; only the dim3 struct pattern
  // is handled below.
  if (opName != "cir.load" || defOp->getNumOperands() < 1)
    return std::nullopt;

  // The loaded pointer must come from a cir.get_member op.
  Value ptrVal = defOp->getOperand(0);
  Operation *getMemberOp = ptrVal.getDefiningOp();
  if (!getMemberOp ||
      getMemberOp->getName().getStringRef() != "cir.get_member")
    return std::nullopt;

  // Retrieve the field index (x=0, y=1, z=2).
  IntegerAttr idxAttr =
      getMemberOp->getAttrOfType<IntegerAttr>("index_attr");
  if (!idxAttr)
    return std::nullopt;
  int64_t fieldIndex = idxAttr.getInt();

  // The get_member base is always a cir.alloca — either the named "block"
  // alloca (when FlattenCFG elided the copy) or a "dim3.tmp" alloca (when
  // the per-field copy is still present).
  if (getMemberOp->getNumOperands() < 1)
    return std::nullopt;
  Value basePtr = getMemberOp->getOperand(0); // addr operand
  Operation *baseDefOp = basePtr.getDefiningOp();
  if (!baseDefOp || baseDefOp->getName().getStringRef() != "cir.alloca")
    return std::nullopt;

  // Default: basePtr IS the actual dim3 alloca (elided-copy path).
  Value dim3Alloca = basePtr;

  // If basePtr is a "dim3.tmp" temporary, there will be a cir.copy op with
  // basePtr as its dst (operand 0).  The src (operand 1) is the real alloca.
  // CopyOp TableGen: arguments = (ins $dst, $src, ...).
  // Assembly format: "cir.copy %src to %dst" → operand(0)=dst, operand(1)=src.
  for (OpOperand &use : basePtr.getUses()) {
    Operation *userOp = use.getOwner();
    if (userOp->getName().getStringRef() != "cir.copy" ||
        userOp->getNumOperands() < 2 ||
        userOp->getOperand(0) != basePtr) // must be the dst
      continue;
    dim3Alloca = userOp->getOperand(1); // src of copy
    break;
  }

  // Search the entire enclosing function region for the dim3 constructor call
  // that initialises dim3Alloca.  After HoistAllocasPass, allocas live in the
  // entry block while the call may be in a later block.
  //
  // We match:
  //   cir.call @_ZN4dim3C1Ejjj(%dim3Alloca, x, y, z)
  // or the delegating constructor @_ZN4dim3C2Ejjj.
  // Constructor signature: (this, x, y, z) → fieldIndex maps to argIdx+1.
  Operation *allocaOp = dim3Alloca.getDefiningOp();
  if (!allocaOp)
    return std::nullopt;
  Region *funcRegion = allocaOp->getParentRegion();
  if (!funcRegion)
    return std::nullopt;

  for (Block &b : *funcRegion) {
    for (Operation &op : b) {
      if (op.getName().getStringRef() != "cir.call")
        continue;

      // Locate the callee symbol.  The "callee" attribute on cir.call is
      // stored at a positional index (not a fixed string key), so we scan
      // all attributes for the first FlatSymbolRefAttr.
      FlatSymbolRefAttr calleeAttr;
      for (NamedAttribute na : op.getAttrDictionary()) {
        if (auto sym = dyn_cast<FlatSymbolRefAttr>(na.getValue())) {
          calleeAttr = sym;
          break;
        }
      }
      if (!calleeAttr)
        continue;

      StringRef callee = calleeAttr.getValue();
      if (!callee.contains("dim3") ||
          (!callee.contains("C1Ejjj") && !callee.contains("C2Ejjj")))
        continue;

      // The first operand ("this") must be the block/grid dim3 alloca.
      if (op.getNumOperands() < 4 || op.getOperand(0) != dim3Alloca)
        continue;

      // Found the constructor.  Pick the argument for this field:
      //   field 0 (x) → arg 1,  field 1 (y) → arg 2,  field 2 (z) → arg 3.
      unsigned argIdx = static_cast<unsigned>(fieldIndex) + 1;
      if (argIdx >= op.getNumOperands())
        return std::nullopt;

      return tryResolveIndexToConstant(op.getOperand(argIdx));
    }
  }

  return std::nullopt;
}

/// Try to fold blockX * blockY * blockZ to a compile-time integer constant.
///
/// Handles both the trivial case (direct constants) and the CIR dim3-struct
/// pattern emitted by the HIP/CIR lowering for kernel launch configurations.
/// Returns the product or std::nullopt if any dimension is non-constant.
static std::optional<int64_t>
tryFoldBlockTotal(offload::KernelLaunchOp launch) {
  auto bx = tryResolveIndexToConstant(launch.getBlockX());
  auto by = tryResolveIndexToConstant(launch.getBlockY());
  auto bz = tryResolveIndexToConstant(launch.getBlockZ());
  if (bx && by && bz)
    return *bx * *by * *bz;
  return std::nullopt;
}

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

struct TightenLaunchBoundsPass
    : offload::impl::OffloadTightenLaunchBoundsPassBase<
          TightenLaunchBoundsPass> {

  using OffloadTightenLaunchBoundsPassBase::OffloadTightenLaunchBoundsPassBase;

  void runOnOperation() override {
    ModuleOp module = getOperation();
    MLIRContext *ctx = module.getContext();
    SymbolTable symTable(module);

    // Infer the warp size from the target attribute once for the whole module.
    std::optional<int64_t> warpSize = inferWarpSize(module);

    // Collect all kernel (exec_space=global) offload.func ops up front to
    // avoid iterator invalidation when clones are inserted.
    SmallVector<offload::FuncOp> kernels;
    module.walk([&](offload::FuncOp fn) {
      if (fn.isKernel())
        kernels.push_back(fn);
    });

    for (offload::FuncOp kernel : kernels)
      processKernel(ctx, module, symTable, warpSize, kernel);
  }

  void processKernel(MLIRContext *ctx, ModuleOp module, SymbolTable &symTable,
                     std::optional<int64_t> warpSize,
                     offload::FuncOp kernel) {
    // Current maxThreadsPerBlock — treat missing launch_bounds as 1024.
    int64_t currentMax = 1024;
    std::optional<int64_t> minBlocks;
    if (auto lb = kernel.getLaunchBoundsAttr()) {
      currentMax = lb.getMaxThreadsPerBlock();
      minBlocks = lb.getMinBlocksPerSM();
    }

    // Group launch ops by the candidate bucket they benefit from.
    // Launches that are dynamic or whose static total offers no improvement
    // over currentMax are left alone — they continue calling the original.
    llvm::DenseMap<int64_t, SmallVector<offload::KernelLaunchOp>> staticGroups;

    auto uses = SymbolTable::getSymbolUses(kernel, module);
    if (!uses)
      return;
    for (auto &use : *uses) {
      auto launch = dyn_cast<offload::KernelLaunchOp>(use.getUser());
      if (!launch)
        continue;

      auto total = tryFoldBlockTotal(launch);
      if (!total)
        continue; // dynamic block dims — leave pointing at original

      int64_t bucket = pickBucket(*total, warpSize);
      if (bucket < 0 || bucket >= currentMax)
        continue; // no improvement available for this launch

      staticGroups[bucket].push_back(launch);
    }

    if (staticGroups.empty())
      return; // Nothing to improve for this kernel.

    // Create one specialised clone per bucket and redirect its launch ops.
    for (auto &[bucket, launches] : staticGroups) {
      // '@original$maxN' — '$' is legal in MLIR bare identifiers but not in
      // standard C++ identifiers, so these names cannot collide with any
      // user-defined symbol.
      std::string cloneName =
          llvm::formatv("{0}$max{1}", kernel.getName(), bucket).str();

      // Clone the kernel and apply the tighter launch bound.
      //
      // When the warp size is known we also set minBlocksPerSM to the number
      // Preserve an explicit user-supplied minBlocksPerSM if present.
      // Do NOT auto-compute one from wavesPerBlock: setting waves_per_eu to
      // ceil(bucket/warpSize) pins the VGPR budget to exactly the same limit
      // that the default occupancy heuristic already enforces, providing no
      // register-pressure benefit.  The max_flat_workgroup_size hint alone
      // is sufficient — the backend lowers its occupancy target automatically
      // when it sees a smaller max block size, freeing more VGPRs without any
      // explicit waves_per_eu constraint.
      std::optional<int64_t> cloneMinBlocks = minBlocks;
      auto clone = cast<offload::FuncOp>(kernel->clone());
      SymbolTable::setSymbolName(clone, cloneName);
      clone.setLaunchBoundsAttr(
          LaunchBoundsAttr::get(ctx, bucket, cloneMinBlocks));

      // Insert the clone into the module body (at end) and register it in the
      // symbol table, then move it to sit immediately after the original so
      // that related kernels appear together in IR dumps.
      symTable.insert(clone);
      clone->moveAfter(kernel);

      // Redirect every launch in this bucket to the specialised clone.
      for (auto launch : launches)
        launch.setCalleeAttr(FlatSymbolRefAttr::get(ctx, cloneName));
    }

    // The original kernel is always kept — it may be retrieved by name at
    // runtime via hipModuleGetFunction and launched through paths the IR
    // cannot observe.
  }
};

} // namespace
