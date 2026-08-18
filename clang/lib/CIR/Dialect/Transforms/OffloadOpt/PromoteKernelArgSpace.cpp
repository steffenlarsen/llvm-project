//===- PromoteKernelArgSpace.cpp - Kernel pointer args are global --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A HIP kernel's pointer arguments always point to global memory -- the runtime
// can only hand a kernel device allocations -- so the ABI states them in the
// global address space. OGCG does this in
// `AMDGPUABIInfo::classifyKernelArgumentType`, which runs
// `coerceKernelArgumentType` for HIP; without it a kernel parameter reaches the
// backend as a generic pointer and every access through it is `flat_` rather
// than `global_` unless InferAddressSpaces happens to recover it.
//
// This restates the parameter type and immediately casts back to generic at the
// top of the entry block, so the body is untouched: the signature carries the
// fact, and InferAddressSpaces propagates it forward through the cast the same
// way it does for OGCG.
//
// Doing this in CIRGen instead is a trap. Coercing the parameter type there
// leaves `cir.copy` with a global-address-space destination that actually
// points at a stack slot, and the copy is then silently dropped -- the same
// failure staging records in `coerceKernelPtrArgsToGlobal`, which it defines
// and never calls for exactly this reason. Running here, after the offload
// passes and with the body left generic, avoids it: nothing in the body ever
// sees the promoted type.
//
// TODO: work out why that CIRGen-level coercion drops the copy, rather than
// only routing around it. A `cir.copy` whose destination is stated to be in
// one address space while it actually points at a stack slot is a
// mis-typed IR that some pass then acts on; the copy disappearing silently
// suggests something is drawing an aliasing or reachability conclusion from
// the address space without the type having been established. If that is
// right, the same reasoning can fire wherever else an address space is
// restated -- so it is worth understanding rather than avoiding, and fixing
// it would also let this move to CIRGen where OGCG does it
// (`AMDGPUABIInfo::classifyKernelArgumentType`), covering non-kernel paths
// this pass does not reach.
//
// Only the device module is touched. Arguments travel in the kernarg segment,
// so the host stub's signature is unrelated and stays as it is.
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cir-offload-promote-kernel-arg-space"

namespace mlir {
#define GEN_PASS_DEF_OFFLOADPROMOTEKERNELARGSPACE
#include "clang/CIR/Dialect/Passes.h.inc"
} // namespace mlir

using namespace mlir;
using namespace cir;

namespace {

// AMDGPUAS::GLOBAL_ADDRESS. Spelled here rather than pulled from the AMDGPU
// backend headers, which the CIR dialect does not depend on.
constexpr unsigned kGlobalAddressSpace = 1;

/// Restate `kernel`'s generic pointer parameters as global ones, casting back
/// to generic at entry. Returns the number of parameters promoted.
static unsigned promoteKernel(cir::FuncOp kernel) {
  Block &entry = kernel.getBody().front();
  MLIRContext *ctx = kernel.getContext();
  auto globalSpace = cir::TargetAddressSpaceAttr::get(ctx, kGlobalAddressSpace);

  llvm::SmallVector<Type> inputs(kernel.getFunctionType().getInputs());
  unsigned promoted = 0;

  OpBuilder builder(&entry, entry.begin());
  for (auto [i, argTy] : llvm::enumerate(inputs)) {
    auto ptrTy = mlir::dyn_cast<cir::PointerType>(argTy);
    // A parameter that already carries an address space was put there
    // deliberately -- by `__shared__`, say -- and is not ours to restate.
    if (!ptrTy || ptrTy.getAddrSpace())
      continue;
    if (i >= entry.getNumArguments())
      continue;

    auto globalTy = cir::PointerType::get(ptrTy.getPointee(), globalSpace);
    BlockArgument arg = entry.getArgument(i);
    arg.setType(globalTy);
    inputs[i] = globalTy;

    // The body keeps working in the generic space; only the signature changes.
    auto back = cir::CastOp::create(builder, kernel.getLoc(), ptrTy,
                                    cir::CastKind::address_space, arg);
    arg.replaceAllUsesExcept(back.getResult(), back);
    ++promoted;
  }

  if (promoted)
    kernel.setFunctionType(
        cir::FuncType::get(inputs, kernel.getFunctionType().getReturnType(),
                           kernel.getFunctionType().isVarArg()));
  return promoted;
}

struct OffloadPromoteKernelArgSpacePass
    : public impl::OffloadPromoteKernelArgSpaceBase<
          OffloadPromoteKernelArgSpacePass> {
  void runOnOperation() override;
};

void OffloadPromoteKernelArgSpacePass::runOnOperation() {
  cir::OffloadContainerOp container = getOperation();
  unsigned kernels = 0, args = 0;

  for (mlir::ModuleOp device : container.getDeviceModules()) {
    device.walk([&](cir::FuncOp fn) {
      // Only AMDGPU kernels: the global address space number and the ABI rule
      // are both target specific, and the calling convention is what says the
      // arguments came from a HIP launch rather than from other device code.
      if (fn.isDeclaration() || fn.getBody().empty())
        return;
      if (fn.getCallingConv() != cir::CallingConv::AMDGPUKernel)
        return;
      if (unsigned n = promoteKernel(fn)) {
        ++kernels;
        args += n;
      }
    });
  }

  LLVM_DEBUG(llvm::dbgs() << "promoted " << args << " pointer arguments across "
                          << kernels << " kernels\n");
  if (!kernels)
    markAllAnalysesPreserved();
}

} // namespace

std::unique_ptr<Pass> mlir::createOffloadPromoteKernelArgSpacePass() {
  return std::make_unique<OffloadPromoteKernelArgSpacePass>();
}
