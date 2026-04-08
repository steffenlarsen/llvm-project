//===- OffloadMultiversionDivisibility.cpp - Multiversion for divisibility ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// When a scalar runtime argument (typically a problem size `n`) is used as a
// grid-stride loop bound in a kernel, creates a fast clone valid under
// `n % VF == 0` alongside the generic original, with all visible launches
// redirected to the fast clone.
//
// The fast clone has a divisibility attribute so the LLVM vectorizer can
// eliminate remainder/tail handling.
//
// Run after SpecializeScalarArgs and before the offload->GPU lowering pass.
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
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "cir-offload-multiversion-divisibility"

using namespace mlir;

namespace {

//===----------------------------------------------------------------------===//
// Grid-stride loop detection
//===----------------------------------------------------------------------===//

/// Check if a block argument reaches a comparison (heuristic for loop bound
/// usage).
///
/// CIRGen spills every kernel parameter into an alloca and reloads it, so the
/// block argument's only direct use is a cir.store; the cir.cmp is on a value
/// loaded back out.  Following casts alone therefore never reaches the
/// comparison in real code -- it only works on hand-written IR where the
/// argument feeds cir.cmp directly, which is why this used to match in the lit
/// test and nowhere else.  The backward equivalent of the spill hop lives in
/// OffloadSpecializeLaunchWrappers::collectParamsReaching.
static bool isUsedAsLoopBound(BlockArgument arg) {
  SmallVector<Value, 8> worklist;
  llvm::SmallPtrSet<void *, 16> visited;
  worklist.push_back(arg);

  while (!worklist.empty()) {
    Value current = worklist.pop_back_val();
    if (!visited.insert(current.getAsOpaquePointer()).second)
      continue;

    for (OpOperand &use : current.getUses()) {
      Operation *user = use.getOwner();

      if (isa<cir::CmpOp>(user))
        return true;

      // A parameter spill: hop from the stored value to every reload of the
      // slot it was stored into.
      if (auto store = dyn_cast<cir::StoreOp>(user)) {
        if (store.getValue() != current)
          continue;
        auto alloca = store.getAddr().getDefiningOp<cir::AllocaOp>();
        if (!alloca)
          continue;
        for (Operation *slotUser : alloca.getResult().getUsers())
          if (auto load = dyn_cast<cir::LoadOp>(slotUser))
            worklist.push_back(load.getResult());
        continue;
      }

      // The bound is often scaled before the comparison (k*sizeof/QK8_0 in
      // ggml's dequantize kernels), so follow ordinary value-producing ops.
      // Being permissive here is fine: the real evidence is the grid relation
      // derived at the launch site, and this only gates on the fact being
      // usable at all.
      if (isa<cir::CallOp>(user))
        continue;
      for (Value result : user->getResults())
        worklist.push_back(result);
    }
  }
  return false;
}

/// Check if a type is a pointer type.
static bool isPointerType(Type ty) {
  return isa<cir::PointerType>(ty);
}

/// Build `v % divisor == 0` at the builder's current insertion point.
///
/// Used twice from the same (arg, divisor) pair: once on the device to state
/// the fact, once on the host to test it.  Deriving both from one place is
/// what keeps the guard and the assumption in agreement -- if they could drift
/// the clone would be entered under conditions it was not compiled for.
static Value buildDivisibilityTest(OpBuilder &builder, Location loc, Value v,
                                   int64_t divisor) {
  auto intTy = mlir::dyn_cast<cir::IntType>(v.getType());
  if (!intTy)
    return {};

  // The divisor comes from the grid expression, whose type need not be the
  // bound's -- a 2048 recovered from an int64 grid computation does not fit an
  // i16 bound.  IntAttr::get asserts rather than truncating, so require the
  // round trip to be exact and decline otherwise.
  const unsigned width = intTy.getWidth();
  if (width == 0 || width > 64)
    return {};
  llvm::APInt wide(64, static_cast<uint64_t>(divisor), /*isSigned=*/true);
  llvm::APInt divisorVal = wide.trunc(width);
  const bool exact = intTy.isSigned() ? (divisorVal.sext(64) == wide)
                                      : (divisorVal.zext(64) == wide);
  if (!exact)
    return {};

  auto divisorCst = cir::ConstantOp::create(
      builder, loc, cir::IntAttr::get(intTy, divisorVal));
  auto rem =
      cir::RemOp::create(builder, loc, intTy, v, divisorCst.getResult());

  llvm::APInt zeroVal(width, 0, intTy.isSigned());
  auto zeroCst =
      cir::ConstantOp::create(builder, loc, cir::IntAttr::get(intTy, zeroVal));

  auto boolTy = cir::BoolType::get(builder.getContext());
  return cir::CmpOp::create(builder, loc, boolTy, cir::CmpOpKind::eq,
                            rem.getResult(), zeroCst.getResult())
      .getResult();
}

/// Check if a type is an integer type suitable for divisibility checks.
static bool isIntegerLikeType(Type ty) {
  if (isa<IntegerType, IndexType>(ty))
    return true;
  if (isa<cir::IntType>(ty))
    return true;
  return false;
}

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

struct OffloadMultiversionDivisibilityPass
    : public PassWrapper<OffloadMultiversionDivisibilityPass,
                         OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(
      OffloadMultiversionDivisibilityPass)

  OffloadMultiversionDivisibilityPass() = default;
  OffloadMultiversionDivisibilityPass(bool enabled) : passEnabled(enabled) {}

  StringRef getArgument() const override {
    return "cir-offload-multiversion-divisibility";
  }
  StringRef getDescription() const override {
    return "Create fast-path kernel clones valid when a scalar argument is "
           "divisible by a vectorization factor";
  }

  void runOnOperation() override {
    if (!passEnabled)
      return;

    ModuleOp module = getOperation();

    module.walk([&](cir::OffloadModuleOp offloadMod) {
      if (offloadMod.getBody().empty())
        return;
      SymbolTable symTable(offloadMod);
      SmallVector<cir::OffloadFuncOp> kernels;
      for (auto func : offloadMod.getOps<cir::OffloadFuncOp>()) {
        if (func.isKernel() && !func.isExternal())
          kernels.push_back(func);
      }
      for (auto kernel : kernels)
        processKernel(module.getContext(), module, symTable, offloadMod,
                      kernel);
    });
  }

  void processKernel(MLIRContext *ctx, ModuleOp module, SymbolTable &symTable,
                     cir::OffloadModuleOp offloadMod,
                     cir::OffloadFuncOp kernel) {
    // A clone of a clone gains nothing and would recurse on a later run.
    if (kernel.getSymName().contains("$div"))
      return;

    // EliminateCoveredGuards already built a dispatch on `n % d == 0` for this
    // kernel and actually removed the guards; a second one on the same
    // condition would only duplicate code.
    if (kernel->hasAttr("covered_guards_handled") ||
        kernel.getSymName().contains("$guards"))
      return;

    // Gather all launch ops targeting this kernel.
    SmallVector<cir::OffloadKernelLaunchOp> launchOps;
    module.walk([&](cir::OffloadKernelLaunchOp op) {
      if (op.getKernelLeafName() == kernel.getSymName())
        launchOps.push_back(op);
    });

    if (launchOps.empty()) {
      LLVM_DEBUG(llvm::dbgs() << "DIVDBG " << kernel.getSymName()
                              << ": no launches\n");
      return;
    }
    LLVM_DEBUG(llvm::dbgs() << "DIVDBG " << kernel.getSymName() << ": "
                            << launchOps.size() << " launches\n");

    unsigned numArgs = kernel.getNumArguments();

    // Find scalar integer args used as loop bounds.
    SmallVector<unsigned> eligibleParams;
    for (unsigned i = 0; i < numArgs; ++i) {
      Type argTy = kernel.getArgumentTypes()[i];
      if (isPointerType(argTy))
        continue;
      if (!isIntegerLikeType(argTy))
        continue;

      BlockArgument arg = kernel.getArgument(i);
      if (!isUsedAsLoopBound(arg))
        continue;

      // Check that the param is NOT already a compile-time constant at all
      // sites -- that case is handled by SpecializeScalarArgs.
      bool allConstant = true;
      for (auto launch : launchOps) {
        auto kernelOperands = launch.getKernelOperands();
        if (i >= kernelOperands.size()) {
          allConstant = false;
          break;
        }
        APInt dummy;
        if (!matchPattern(kernelOperands[i], m_ConstantInt(&dummy))) {
          // Also check CIR constants.
          Operation *defOp = kernelOperands[i].getDefiningOp();
          if (!defOp || !isa<cir::ConstantOp>(defOp)) {
            allConstant = false;
            break;
          }
        }
      }
      if (allConstant)
        continue;

      eligibleParams.push_back(i);
    }

    if (eligibleParams.empty())
      return;

    // Derive which argument to multiversion on, and by what factor, from the
    // host's own grid computation rather than assuming one.
    //
    // A kernel that covers `n` elements in blocks of `d` is launched with
    // `gridDim = ceil(n/d)`; when `n % d == 0` the final block is exactly full
    // and the kernel's tail guards are dead.  `d` is therefore the only factor
    // worth cloning for, and it is written down at the launch site -- ggml's
    // `(k + CUDA_Q8_0_NE_ALIGN - 1) / CUDA_Q8_0_NE_ALIGN` is precisely this
    // shape.  A hardcoded vectorisation factor would name a divisibility the
    // kernel does not care about.
    //
    // findGridDimCandidate rather than matchGridDimRelation: this pass runs
    // after CIRFlattenCFG, and we test the relation at run time below, which
    // is exactly the contract the former documents.
    std::optional<cir::GridDimRelation> rel;
    for (auto launch : launchOps) {
      LLVM_DEBUG({
        cir::ValueTraceResult t = cir::traceValueOrigin(launch.getGridSizeX());
        llvm::dbgs() << "DIVDBG   gridX trace kind=" << (int)t.kind
                     << " terminal="
                     << (t.terminal ? (t.terminal.getDefiningOp()
                             ? t.terminal.getDefiningOp()->getName().getStringRef()
                             : llvm::StringRef("<blockarg>"))
                                    : llvm::StringRef("<none>")) << "\n";
        mlir::Value g = launch.getGridSizeX();
        llvm::dbgs() << "DIVDBG   gridX defop="
                     << (g.getDefiningOp()
                             ? g.getDefiningOp()->getName().getStringRef()
                             : llvm::StringRef("<blockarg>"))
                     << "\n";
      });
      std::optional<cir::GridDimRelation> r =
          cir::findGridDimCandidate(launch.getGridSizeX(), launch);
      if (!r || r->divisor <= 1) {
        LLVM_DEBUG(llvm::dbgs() << "DIVDBG   no grid relation (r="
                                << (r ? (int)r->divisor : -1) << ")\n");
        return;
      }
      if (!rel) {
        rel = *r;
        continue;
      }
      // One clone has to serve every site, so the sites must agree.
      if (rel->argIndex != r->argIndex || rel->divisor != r->divisor)
        return;
    }
    if (!rel)
      return;

    const unsigned paramIdx = rel->argIndex;
    const int64_t vectorFactor = rel->divisor;

    LLVM_DEBUG(llvm::dbgs() << "DIVDBG   rel arg=" << paramIdx << " vf="
                            << vectorFactor << " eligible={";
               for (unsigned e : eligibleParams) llvm::dbgs() << e << ",";
               llvm::dbgs() << "}\n");
    if (!llvm::is_contained(eligibleParams, paramIdx))
      return;

    {
      LLVM_DEBUG(llvm::dbgs()
                 << "OffloadMultiversionDivisibility: " << kernel.getSymName()
                 << " -- multiversioning on arg " << paramIdx
                 << " with VF=" << vectorFactor << "\n");

      // Create the fast clone.
      std::string cloneName =
          llvm::formatv("{0}$div{1}", kernel.getSymName(), vectorFactor).str();

      // If the clone already exists (idempotence), skip.
      if (symTable.lookup(cloneName))
        return;

      auto *cloneOp = kernel->clone();
      auto clone = cast<cir::OffloadFuncOp>(cloneOp);
      SymbolTable::setSymbolName(clone, cloneName);

      // State the divisibility in the clone body.
      //
      // An attribute alone does nothing: nothing in CIR, MLIR or LLVM reads
      // `divisibility.argN`, so the clone used to be byte-identical to the
      // original.  A cir.assume lowers to llvm.assume, which is what actually
      // lets the tail guards fold.  The attribute is kept alongside it purely
      // as a marker for tests and debugging.
      if (!clone.getBody().empty() && paramIdx < clone.getNumArguments()) {
        OpBuilder builder(ctx);
        Block &entry = clone.getBody().front();
        builder.setInsertionPointToStart(&entry);
        Location loc = clone.getLoc();

        if (Value test = buildDivisibilityTest(
                builder, loc, entry.getArgument(paramIdx), vectorFactor)) {
          cir::AssumeOp::create(builder, loc, test,
                                cir::AssumeBundleKind::None,
                                mlir::ValueRange{});
          clone->setAttr(llvm::formatv("divisibility.arg{0}", paramIdx).str(),
                         builder.getI64IntegerAttr(vectorFactor));
        } else {
          // Without the assumption the clone is just a duplicate; drop it
          // rather than redirect launches to a kernel that gained nothing.
          cloneOp->erase();
          return;
        }
      }

      symTable.insert(clone);
      clone->moveAfter(kernel);

      // Redirect each launch site to the fast clone.
      StringRef offloadModName = offloadMod.getSymName();
      for (auto launch : launchOps) {
        auto kernelOperands = launch.getKernelOperands();
        if (paramIdx >= kernelOperands.size())
          continue;

        StringRef launchModName =
            launch.getKernelAttr().getRootReference().getValue();

        // Ensure clone declaration exists in launch module.
        if (launchModName != offloadModName) {
          auto launchMod =
              module.lookupSymbol<cir::OffloadModuleOp>(launchModName);
          if (launchMod && !launchMod.lookupSymbol(cloneName)) {
            auto origDecl = launchMod.lookupSymbol<cir::OffloadFuncOp>(
                kernel.getSymName());
            if (origDecl) {
              auto *declClone = origDecl->clone();
              SymbolTable::setSymbolName(declClone, cloneName);
              OpBuilder declBuilder(ctx);
              declBuilder.setInsertionPointToEnd(&launchMod.getBody().front());
              declBuilder.insert(declClone);
            }
          }
        }

        // Dispatch on the divisibility instead of redirecting unconditionally.
        //
        // The clone now carries a real cir.assume, so entering it when
        // `n % VF != 0` would be entering a kernel under a false assumption --
        // the unconditional redirect that used to live here was only harmless
        // because the assumption was inert.
        //
        //     if (n % VF == 0) fast_clone<<<...>>> else original<<<...>>>
        //
        // Built with blocks and cir.brcond rather than a structured cir.if:
        // this pass runs after CIRFlattenCFG, which would leave a cir.if
        // unlegalised.  Same construction as
        // OffloadPropagateGridCoverage::multiversionCappedDims.
        OpBuilder builder(ctx);
        Location loc = launch.getLoc();
        builder.setInsertionPoint(launch);
        Value cond = buildDivisibilityTest(builder, loc,
                                           kernelOperands[paramIdx],
                                           vectorFactor);
        if (!cond)
          continue;

        Block *entryBlk = launch->getBlock();
        Block *tail = entryBlk->splitBlock(std::next(launch->getIterator()));
        Block *fast = builder.createBlock(tail);
        Block *slow = builder.createBlock(tail);

        {
          OpBuilder::InsertionGuard guard(builder);
          builder.setInsertionPointToEnd(fast);
          auto *fastLaunch = builder.clone(*launch.getOperation());
          cast<cir::OffloadKernelLaunchOp>(fastLaunch).setKernelAttr(
              SymbolRefAttr::get(ctx, launchModName,
                                 {FlatSymbolRefAttr::get(ctx, cloneName)}));
          cir::BrOp::create(builder, loc, tail);
        }
        {
          OpBuilder::InsertionGuard guard(builder);
          launch->moveBefore(slow, slow->end());
          builder.setInsertionPointToEnd(slow);
          cir::BrOp::create(builder, loc, tail);
        }
        {
          OpBuilder::InsertionGuard guard(builder);
          builder.setInsertionPointToEnd(entryBlk);
          cir::BrCondOp::create(builder, loc, cond, fast, slow);
        }
      }
    }
  }

  bool passEnabled = true;
};

} // namespace

std::unique_ptr<mlir::Pass>
mlir::createOffloadMultiversionDivisibilityPass(bool enabled) {
  return std::make_unique<OffloadMultiversionDivisibilityPass>(enabled);
}
