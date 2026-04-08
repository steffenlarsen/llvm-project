//===- OffloadTightenLaunchBounds.cpp - Tighten kernel launch bounds -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// For each cir.offload.func with the kernel attribute, this pass inspects all
// cir.offload.kernel_launch ops that reference it and attempts to fold the
// product blockX * blockY * blockZ to a compile-time constant.
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
// This pass must run before the CIR Offload → GPU lowering pass, which is the
// only point where both the cir.offload.func callee and its launch sites are
// co-visible.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIROpsEnums.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/Dialect/Transforms/CIRTransformUtils.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "cir-offload-tighten-launch-bounds"

using namespace mlir;

namespace {

//===----------------------------------------------------------------------===//
// Target warp-size inference
//===----------------------------------------------------------------------===//

/// Return the wavefront/warp size for a single architecture string, or
/// std::nullopt if the string is not recognised.
///
/// AMD gfxN: the numeric part determines the generation.
///   N < 1000  -> GFX6-9 / CDNA, wavefront size 64
///   N >= 1000 -> GFX10+ / RDNA, wavefront size 32
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

static int64_t computeStaticLDSBytes(cir::OffloadFuncOp fn) {
  // OffloadFuncOp does not have workgroup attribution args like gpu::GPUFuncOp.
  // Static LDS is tracked via the "gpu.workgroup_memory" attribute if present.
  // For now, return 0 — the occupancy analysis will be conservative.
  (void)fn;
  return 0;
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

static bool classifyComputeBound(cir::OffloadFuncOp fn) {
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
                          << " -> " << (result ? "compute" : "memory")
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
  return -1; // blockTotal > 512 -- no improvement possible
}

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

struct OffloadTightenLaunchBoundsPass
    : public PassWrapper<OffloadTightenLaunchBoundsPass,
                         OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(OffloadTightenLaunchBoundsPass)

  OffloadTightenLaunchBoundsPass() = default;
  OffloadTightenLaunchBoundsPass(bool enabled) : passEnabled(enabled) {}

  // Occupancy-based waves-per-eu control.
  bool wavesPerEuEnabled = true;
  bool aggressiveRegisterMin = true;
  double aggressiveMinFraction = 0.5;

  StringRef getArgument() const override {
    return "cir-offload-tighten-launch-bounds";
  }
  StringRef getDescription() const override {
    return "Tighten kernel launch bounds based on static block dimensions";
  }

  void runOnOperation() override {
    if (!passEnabled)
      return;

    ModuleOp module = getOperation();
    MLIRContext *ctx = module.getContext();

    // Infer the warp size from the target attribute once for the whole module.
    std::optional<int64_t> warpSize = inferWarpSize(module);
    std::optional<ArchConstants> archConsts = inferArchConstants(module);

    // Process all cir.offload.modules.
    module.walk([&](cir::OffloadModuleOp offloadMod) {
      if (offloadMod.getBody().empty())
        return;
      SymbolTable symTable(offloadMod);
      SmallVector<cir::OffloadFuncOp> kernels;
      offloadMod.walk([&](cir::OffloadFuncOp fn) {
        if (fn.isKernel() && !fn.isExternal())
          kernels.push_back(fn);
      });

      for (cir::OffloadFuncOp kernel : kernels)
        processKernel(ctx, module, symTable, warpSize, archConsts, kernel);
    });
  }

  void processKernel(MLIRContext *ctx, ModuleOp module, SymbolTable &symTable,
                     std::optional<int64_t> warpSize,
                     const std::optional<ArchConstants> &archConsts,
                     cir::OffloadFuncOp kernel) {
    // Read the current max threads -- treat missing known_block_size as 1024.
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
    // over currentMax are left alone -- they continue calling the original.
    // Store the actual block dims (bx, by, bz) from the first launch seen so
    // that known_block_size preserves the multi-dimensional layout.
    struct BucketInfo {
      SmallVector<cir::OffloadKernelLaunchOp> launches;
      int64_t bx = 1, by = 1, bz = 1;
    };
    llvm::DenseMap<int64_t, BucketInfo> staticGroups;

    // cir.offload.kernel_launch callee is a nested symbol ref:
    // @offloadModule::@kernelName.
    // Walk the outer module for launch ops targeting this kernel.
    SmallVector<cir::OffloadKernelLaunchOp> launchOps;
    module.walk([&](cir::OffloadKernelLaunchOp op) {
      if (op.getKernel().getLeafReference() == kernel.getSymName())
        launchOps.push_back(op);
    });

    for (auto launch : launchOps) {
      auto bx = cir::tryResolveToConstant(launch.getBlockSizeX());
      auto by = cir::tryResolveToConstant(launch.getBlockSizeY());
      auto bz = cir::tryResolveToConstant(launch.getBlockSizeZ());
      if (!bx || !by || !bz)
        continue; // dynamic block dims -- leave pointing at original

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
          llvm::formatv("{0}$max{1}", kernel.getSymName(), bucket).str();

      // Clone the kernel and apply the tighter launch bound attributes.
      auto *cloneOp = kernel->clone();
      auto clone = cast<cir::OffloadFuncOp>(cloneOp);
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
            if (auto resolved = cir::tryResolveToConstant(dynShmem))
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
                   << "  " << kernel.getSymName() << "$max" << bucket
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

      // Insert the clone into the cir.offload.module body and move it after
      // the original so that related kernels appear together in IR dumps.
      symTable.insert(clone);
      clone->moveAfter(kernel);

      // Redirect every launch in this bucket to the specialised clone.
      // The callee is a nested symbol ref @offloadModule::@kernelName; update
      // only the leaf (kernel name) while keeping the module part unchanged.
      // Use the module name from each launch op's existing reference, NOT the
      // kernel's parent -- in two-pass mode, launches may reference a different
      // offload.module than the one containing the kernel body.
      StringRef kernelModName =
          cast<cir::OffloadModuleOp>(kernel->getParentOp()).getSymName();
      for (auto launch : launches) {
        StringRef launchModName =
            launch.getKernel().getRootReference().getValue();
        // In two-pass mode, the launch references a different offload.module
        // (declarations only) than the one containing the kernel body.
        // Add a declaration of the clone to the launch's target module.
        if (launchModName != kernelModName) {
          auto launchMod =
              module.lookupSymbol<cir::OffloadModuleOp>(launchModName);
          if (launchMod && !launchMod.lookupSymbol(cloneName)) {
            // Find the existing declaration of the original kernel in the
            // launch's module and clone it with the new name.
            auto origDecl =
                launchMod.lookupSymbol<cir::OffloadFuncOp>(kernel.getSymName());
            if (origDecl) {
              auto *declClone = origDecl->clone();
              SymbolTable::setSymbolName(declClone, cloneName);
              OpBuilder builder(ctx);
              builder.setInsertionPointToEnd(&launchMod.getBody().back());
              builder.insert(declClone);
            }
          }
        }
        launch.setKernelAttr(SymbolRefAttr::get(
            ctx, launchModName, {FlatSymbolRefAttr::get(ctx, cloneName)}));
      }
    }

    // The original kernel is always kept -- it may be retrieved by name at
    // runtime via hipModuleGetFunction and launched through paths the IR
    // cannot observe.
  }

  bool passEnabled = true;
};

} // namespace

std::unique_ptr<mlir::Pass>
mlir::createOffloadTightenLaunchBoundsPass(bool enabled) {
  return std::make_unique<OffloadTightenLaunchBoundsPass>(enabled);
}
