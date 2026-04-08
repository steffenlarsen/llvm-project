//===- TightenLaunchBounds.cpp - Tighten kernel launch bounds -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// For each gpu.func with the kernel attribute, this pass inspects all
// gpu.launch_func ops that reference it and attempts to fold the product
// blockX * blockY * blockZ to a compile-time constant.
//
// If the static block total fits within a smaller candidate bound from
// {warpSize, 256, 512}, a specialised clone of the kernel is created with that
// tighter known_block_size and rocdl.flat_work_group_size attrs, and the launch
// op is redirected to the clone.  The original kernel is always preserved
// because it may be called by name through the HIP runtime API
// (hipModuleGetFunction) in ways the IR cannot observe.
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
// This pass must run before GpuSplitSingleSourcePass, which is the only point
// where both the gpu.func callee and its launch sites are co-visible.
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/GPU/Transforms/Passes.h"

#include "mlir/Dialect/GPU/IR/GPUDialect.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"
#include "mlir/IR/BuiltinTypeInterfaces.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "gpu-tighten-launch-bounds"

namespace mlir {

#define GEN_PASS_DEF_GPUTIGHTENLAUNCHBOUNDSPASS
#include "mlir/Dialect/GPU/Transforms/Passes.h.inc"

} // namespace mlir

using namespace mlir;
using namespace mlir::gpu;

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
/// "offload.target" is stored as an ArrayAttr of StringAttr arch names.
/// When architectures have differing warp sizes, we take the maximum.
///
/// Returns std::nullopt when the attribute is absent or no architecture in
/// the list is recognised.
static std::optional<int64_t> inferWarpSize(ModuleOp module) {
  auto archs = module->getAttrOfType<ArrayAttr>("offload.target");
  if (!archs)
    return std::nullopt;

  std::optional<int64_t> result;
  for (Attribute archAttr : archs) {
    auto arch = cast<StringAttr>(archAttr).getValue();
    if (auto ws = getArchWarpSize(arch))
      result = result ? std::max(*result, *ws) : *ws;
  }
  return result;
}

//===----------------------------------------------------------------------===//
// Architecture constants for occupancy analysis
//===----------------------------------------------------------------------===//

struct ArchConstants {
  int64_t waveSize;
  int64_t simdsPerCU;
  int64_t maxWavesPerSIMD;
  int64_t ldsPerCU;
  int64_t maxBlocksPerCU;
};

static std::optional<ArchConstants> getArchConstants(StringRef arch) {
  if (!arch.starts_with("gfx"))
    return std::nullopt;

  uint64_t num = 0;
  if (arch.drop_front(3).consumeInteger(10, num))
    return std::nullopt;

  // CDNA / GFX9 (gfx900, gfx906, gfx908, gfx90a, gfx940, gfx941, gfx942)
  if (num < 1000)
    return ArchConstants{64, 4, 8, 65536, 16};

  // RDNA / GFX10+ (gfx1030, gfx1100, gfx1101, gfx1102, ...)
  // CU-mode values to stay conservative; WGP mode would double LDS.
  return ArchConstants{32, 4, 16, 131072, 16};
}

static std::optional<ArchConstants> inferArchConstants(ModuleOp module) {
  auto archs = module->getAttrOfType<ArrayAttr>("offload.target");
  if (!archs)
    return std::nullopt;

  // For multi-arch, use the most conservative (lowest maxWavesPerSIMD).
  std::optional<ArchConstants> result;
  for (Attribute archAttr : archs) {
    auto arch = cast<StringAttr>(archAttr).getValue();
    if (auto ac = getArchConstants(arch)) {
      if (!result || ac->maxWavesPerSIMD < result->maxWavesPerSIMD)
        result = ac;
    }
  }
  return result;
}

//===----------------------------------------------------------------------===//
// K_ceiling computation
//===----------------------------------------------------------------------===//

static int64_t computeWavesPerBlock(int64_t blockTotal, int64_t waveSize) {
  return (blockTotal + waveSize - 1) / waveSize;
}

static int64_t computeStaticLDSBytes(gpu::GPUFuncOp fn) {
  int64_t totalBytes = 0;
  for (BlockArgument arg : fn.getWorkgroupAttributionBBArgs()) {
    auto memrefType = dyn_cast<MemRefType>(arg.getType());
    if (!memrefType || !memrefType.hasStaticShape())
      continue;
    int64_t numElements = 1;
    for (int64_t dim : memrefType.getShape())
      numElements *= dim;
    int64_t elemBits = memrefType.getElementTypeBitWidth();
    totalBytes += (numElements * elemBits + 7) / 8;
  }
  return totalBytes;
}

static int64_t computeKCeiling(int64_t blockTotal, const ArchConstants &ac,
                                int64_t staticLDS, int64_t dynamicLDS) {
  int64_t wpb = computeWavesPerBlock(blockTotal, ac.waveSize);

  int64_t sBlock = staticLDS + dynamicLDS;
  int64_t kLds = ac.maxWavesPerSIMD;
  if (sBlock > 0) {
    int64_t blocksPerCULds = ac.ldsPerCU / sBlock;
    kLds = (blocksPerCULds * wpb) / ac.simdsPerCU;
  }

  int64_t kSlots =
      std::min(ac.maxWavesPerSIMD, (ac.maxBlocksPerCU * wpb) / ac.simdsPerCU);

  int64_t kCeiling = std::min({kLds, kSlots, ac.maxWavesPerSIMD});
  return std::max(kCeiling, int64_t(1));
}

//===----------------------------------------------------------------------===//
// Compute-bound classifier
//===----------------------------------------------------------------------===//

static bool classifyComputeBound(gpu::GPUFuncOp fn) {
  int64_t arithCount = 0;
  int64_t memCount = 0;

  fn.walk([&](Operation *op) {
    StringRef name = op->getName().getStringRef();
    if (name.starts_with("arith.") || name.starts_with("math.") ||
        name == "cir.binop" || name == "cir.unary")
      ++arithCount;
    else if (name == "cir.load" || name == "cir.store" ||
             name == "memref.load" || name == "memref.store")
      ++memCount;
  });

  bool result = memCount > 0 && arithCount > 4 * memCount;
  LLVM_DEBUG(llvm::dbgs() << "  classifier: arith=" << arithCount
                          << " mem=" << memCount
                          << " → " << (result ? "compute" : "memory")
                          << "-bound\n");
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
static int64_t pickBucket(int64_t blockTotal, std::optional<int64_t> warpSize) {
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
    if (ltPos != StringRef::npos && gtPos != StringRef::npos && gtPos > ltPos) {
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

  // cir.load — the value comes from memory.
  if (opName != "cir.load" || defOp->getNumOperands() < 1)
    return std::nullopt;

  Value ptrVal = defOp->getOperand(0);
  Operation *ptrDefOp = ptrVal.getDefiningOp();
  if (!ptrDefOp)
    return std::nullopt;

  // Simple alloca path: cir.load(cir.alloca) — find the unique cir.store to
  // the alloca and resolve its stored value.
  if (ptrDefOp->getName().getStringRef() == "cir.alloca") {
    Operation *uniqueStore = nullptr;
    for (OpOperand &use : ptrVal.getUses()) {
      Operation *userOp = use.getOwner();
      if (userOp->getName().getStringRef() != "cir.store" ||
          userOp->getNumOperands() < 2 || userOp->getOperand(1) != ptrVal)
        continue;
      if (uniqueStore)
        return std::nullopt; // multiple stores — give up
      uniqueStore = userOp;
    }
    if (uniqueStore)
      return tryResolveIndexToConstant(uniqueStore->getOperand(0));
    return std::nullopt;
  }

  // dim3 struct path: cir.load(cir.get_member(...))
  Operation *getMemberOp = ptrDefOp;
  if (getMemberOp->getName().getStringRef() != "cir.get_member")
    return std::nullopt;

  // Retrieve the field index (x=0, y=1, z=2).
  IntegerAttr idxAttr = getMemberOp->getAttrOfType<IntegerAttr>("index_attr");
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

  // Check for explicit stores to the same field of the source dim3 alloca.
  // This handles the pattern:
  //   dim3 dimBlock;       // constructor: dim3(1, 1, 1)
  //   dimBlock.x = 128;    // field assignment AFTER construction
  // The field store overrides the constructor argument.
  // We check dim3Alloca (which follows through cir.copy if present) so this
  // works regardless of whether a temporary copy exists.
  {
    Operation *fieldStore = nullptr;
    bool ambiguous = false;
    for (OpOperand &use : dim3Alloca.getUses()) {
      Operation *userOp = use.getOwner();
      if (userOp->getName().getStringRef() != "cir.get_member")
        continue;
      IntegerAttr userIdx =
          userOp->getAttrOfType<IntegerAttr>("index_attr");
      if (!userIdx || userIdx.getInt() != fieldIndex)
        continue;
      for (OpOperand &gmUse : userOp->getResult(0).getUses()) {
        Operation *gmUser = gmUse.getOwner();
        if (gmUser->getName().getStringRef() != "cir.store" ||
            gmUser->getNumOperands() < 2 ||
            gmUser->getOperand(1) != userOp->getResult(0))
          continue;
        if (fieldStore) {
          ambiguous = true;
          break;
        }
        fieldStore = gmUser;
      }
      if (ambiguous)
        break;
    }
    if (ambiguous)
      return std::nullopt;
    if (fieldStore)
      return tryResolveIndexToConstant(fieldStore->getOperand(0));
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


//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

struct TightenLaunchBoundsPass
    : impl::GpuTightenLaunchBoundsPassBase<TightenLaunchBoundsPass> {

  using GpuTightenLaunchBoundsPassBase::GpuTightenLaunchBoundsPassBase;

  void runOnOperation() override {
    if (!enabled)
      return;

    ModuleOp module = getOperation();
    MLIRContext *ctx = module.getContext();

    // Infer the warp size from the target attribute once for the whole module.
    std::optional<int64_t> warpSize = inferWarpSize(module);
    std::optional<ArchConstants> archConsts = inferArchConstants(module);

    // Process all gpu.modules (the merge pass may produce arch-suffixed names
    // like "offload_device_module_gfx90a").
    module.walk([&](gpu::GPUModuleOp gpuMod) {
      SymbolTable symTable(gpuMod);
      SmallVector<gpu::GPUFuncOp> kernels;
      gpuMod.walk([&](gpu::GPUFuncOp fn) {
        if (fn.isKernel() && !fn.isExternal())
          kernels.push_back(fn);
      });

      for (gpu::GPUFuncOp kernel : kernels)
        processKernel(ctx, module, symTable, warpSize, archConsts, kernel);
    });
  }

  void processKernel(MLIRContext *ctx, ModuleOp module, SymbolTable &symTable,
                     std::optional<int64_t> warpSize,
                     const std::optional<ArchConstants> &archConsts,
                     gpu::GPUFuncOp kernel) {
    // Read the current max threads — treat missing known_block_size as 1024.
    int64_t currentMax = 1024;
    if (auto knownSize =
            kernel->getAttrOfType<DenseI32ArrayAttr>("known_block_size")) {
      auto elems = knownSize.asArrayRef();
      if (!elems.empty()) {
        int64_t product = 1;
        for (int32_t d : elems)
          product *= d;
        currentMax = product;
      }
    }

    // Group launch ops by the candidate bucket they benefit from.
    // Launches that are dynamic or whose static total offers no improvement
    // over currentMax are left alone — they continue calling the original.
    // Store the actual block dims (bx, by, bz) from the first launch seen so
    // that known_block_size preserves the multi-dimensional layout.
    struct BucketInfo {
      SmallVector<gpu::LaunchFuncOp> launches;
      int64_t bx = 1, by = 1, bz = 1;
    };
    llvm::DenseMap<int64_t, BucketInfo> staticGroups;

    // gpu.launch_func callee is a nested symbol ref: @gpuModule::@kernelName.
    // Walk the outer module for launch_func ops targeting this kernel.
    SmallVector<gpu::LaunchFuncOp> launchOps;
    module.walk([&](gpu::LaunchFuncOp op) {
      if (op.getKernel().getLeafReference() == kernel.getName())
        launchOps.push_back(op);
    });

    for (auto launch : launchOps) {
      auto bx = tryResolveIndexToConstant(launch.getBlockSizeX());
      auto by = tryResolveIndexToConstant(launch.getBlockSizeY());
      auto bz = tryResolveIndexToConstant(launch.getBlockSizeZ());
      if (!bx || !by || !bz)
        continue; // dynamic block dims — leave pointing at original

      int64_t total = *bx * *by * *bz;
      int64_t bucket = pickBucket(total, warpSize);
      if (bucket < 0 || bucket >= currentMax)
        continue; // no improvement available for this launch

      auto &info = staticGroups[bucket];
      info.launches.push_back(launch);
      // Record dims from the first launch; all launches in the bucket share
      // the same total, so the kernel sees the same flat thread count.
      if (info.launches.size() == 1) {
        info.bx = *bx;
        info.by = *by;
        info.bz = *bz;
      }
    }

    if (staticGroups.empty())
      return; // Nothing to improve for this kernel.

    // Create one specialised clone per bucket and redirect its launch ops.
    for (auto &[bucket, info] : staticGroups) {
      auto &launches = info.launches;
      std::string cloneName =
          llvm::formatv("{0}$max{1}", kernel.getName(), bucket).str();

      // Clone the kernel and apply the tighter launch bound attributes.
      auto *cloneOp = kernel->clone();
      auto clone = cast<gpu::GPUFuncOp>(cloneOp);
      SymbolTable::setSymbolName(clone, cloneName);

      // known_block_size: preserve the actual XYZ block dimensions from the
      // launch so that threadIdx.x/y/z retain their original semantics.
      // Collapsing to {bucket, 1, 1} would incorrectly make threadIdx.y == 0
      // for all threads in multi-dimensional blocks.
      clone->setAttr("known_block_size",
                     DenseI32ArrayAttr::get(
                         ctx, {static_cast<int32_t>(info.bx),
                               static_cast<int32_t>(info.by),
                               static_cast<int32_t>(info.bz)}));

      // Occupancy attributes: compute K_ceiling if arch info is available.
      if (wavesPerEuEnabled && archConsts) {
        int64_t blockTotal = info.bx * info.by * info.bz;
        int64_t staticLDS = computeStaticLDSBytes(kernel);

        // Resolve dynamic LDS from each launch in this bucket. Use the max
        // across launches; if any is unresolvable, use 0 (conservative).
        int64_t dynamicLDS = 0;
        for (auto launch : launches) {
          Value dynShmem = launch.getDynamicSharedMemorySize();
          if (dynShmem) {
            if (auto resolved = tryResolveIndexToConstant(dynShmem))
              dynamicLDS = std::max(dynamicLDS, *resolved);
          }
        }

        int64_t kCeiling =
            computeKCeiling(blockTotal, *archConsts, staticLDS, dynamicLDS);

        int64_t wavesPerEu = kCeiling;
        if (aggressiveRegisterMin && classifyComputeBound(kernel)) {
          wavesPerEu = std::max(
              int64_t(1),
              static_cast<int64_t>(kCeiling * aggressiveMinFraction));
        }

        LLVM_DEBUG(llvm::dbgs()
                   << "  " << kernel.getName() << "$max" << bucket
                   << ": blockTotal=" << blockTotal
                   << " staticLDS=" << staticLDS
                   << " dynamicLDS=" << dynamicLDS
                   << " kCeiling=" << kCeiling
                   << " wavesPerEu=" << wavesPerEu << "\n");

        clone->setAttr(
            "rocdl.flat_work_group_size",
            StringAttr::get(ctx, llvm::formatv("1,{0}", bucket).str()));
        clone->setAttr("rocdl.waves_per_eu",
                       IntegerAttr::get(IntegerType::get(ctx, 32), wavesPerEu));
      } else {
        // Fallback: no arch info or waves-per-eu disabled.
        clone->setAttr(
            "rocdl.flat_work_group_size",
            StringAttr::get(ctx, llvm::formatv("1,{0}", bucket).str()));
      }

      // Insert the clone into the gpu.module body and move it after the
      // original so that related kernels appear together in IR dumps.
      symTable.insert(clone);
      clone->moveAfter(kernel);

      // Redirect every launch in this bucket to the specialised clone.
      // The callee is a nested symbol ref @gpuModule::@kernelName; update
      // only the leaf (kernel name) while keeping the module part unchanged.
      // Use the module name from each launch op's existing reference, NOT the
      // kernel's parent — in two-pass mode, launches may reference a different
      // gpu.module than the one containing the kernel body.
      StringRef kernelModName =
          cast<gpu::GPUModuleOp>(kernel->getParentOp()).getName();
      for (auto launch : launches) {
        StringRef launchModName =
            launch.getKernel().getRootReference().getValue();
        // In two-pass mode, the launch references a different gpu.module
        // (declarations only) than the one containing the kernel body.
        // Add a declaration of the clone to the launch's target module.
        if (launchModName != kernelModName) {
          auto launchMod =
              module.lookupSymbol<gpu::GPUModuleOp>(launchModName);
          if (launchMod && !launchMod.lookupSymbol(cloneName)) {
            // Find the existing declaration of the original kernel in the
            // launch's module and clone it with the new name.
            auto origDecl =
                launchMod.lookupSymbol<gpu::GPUFuncOp>(kernel.getName());
            if (origDecl) {
              auto *declClone = origDecl->clone();
              SymbolTable::setSymbolName(declClone, cloneName);
              OpBuilder builder(ctx);
              builder.setInsertionPointToEnd(launchMod.getBody());
              builder.insert(declClone);
            }
          }
        }
        launch.setKernelAttr(SymbolRefAttr::get(
            ctx, launchModName, {FlatSymbolRefAttr::get(ctx, cloneName)}));
      }
    }

    // The original kernel is always kept — it may be retrieved by name at
    // runtime via hipModuleGetFunction and launched through paths the IR
    // cannot observe.
  }
};

} // namespace
