//===- Target.cpp - MLIR LLVM ROCDL target compilation ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This files defines ROCDL target related functions including registration
// calls for the `#rocdl.target` compilation attribute.
//
//===----------------------------------------------------------------------===//

#include "mlir/Target/LLVM/ROCDL/Target.h"

#include "mlir/Dialect/GPU/IR/GPUDialect.h"
#include "mlir/Dialect/LLVMIR/ROCDLDialect.h"
#include "mlir/Support/FileUtilities.h"
#include "mlir/Target/LLVM/ROCDL/Utils.h"
#include "mlir/Target/LLVMIR/Export.h"

#include "llvm/Config/Targets.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Transforms/IPO/AlwaysInliner.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar/InferAddressSpaces.h"
#include "llvm/Transforms/Scalar/InstSimplifyPass.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"
#include "llvm/Transforms/Scalar/SROA.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/Support/ModRef.h"
#include "llvm/IR/PassInstrumentation.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/TargetParser/AMDGPUTargetParser.h"
#include "llvm/Transforms/IPO/GlobalDCE.h"
#include "llvm/Transforms/IPO/Internalize.h"

#include <cstdlib>
#include <optional>

using namespace mlir;
using namespace mlir::ROCDL;

#ifndef __DEFAULT_ROCM_PATH__
#define __DEFAULT_ROCM_PATH__ ""
#endif

namespace {
// Implementation of the `TargetAttrInterface` model.
class ROCDLTargetAttrImpl
    : public gpu::TargetAttrInterface::FallbackModel<ROCDLTargetAttrImpl> {
public:
  std::optional<mlir::gpu::SerializedObject>
  serializeToObject(Attribute attribute, Operation *module,
                    const gpu::TargetOptions &options) const;

  Attribute createObject(Attribute attribute, Operation *module,
                         const mlir::gpu::SerializedObject &object,
                         const gpu::TargetOptions &options) const;
};
} // namespace

// Register the ROCDL dialect, the ROCDL translation and the target interface.
void mlir::ROCDL::registerROCDLTargetInterfaceExternalModels(
    DialectRegistry &registry) {
  registry.addExtension(+[](MLIRContext *ctx, ROCDL::ROCDLDialect *dialect) {
    ROCDLTargetAttr::attachInterface<ROCDLTargetAttrImpl>(*ctx);
  });
}

void mlir::ROCDL::registerROCDLTargetInterfaceExternalModels(
    MLIRContext &context) {
  DialectRegistry registry;
  registerROCDLTargetInterfaceExternalModels(registry);
  context.appendDialectRegistry(registry);
}

// Search for the ROCM path.
StringRef mlir::ROCDL::getROCMPath() {
  if (const char *var = std::getenv("ROCM_PATH"))
    return var;
  if (const char *var = std::getenv("ROCM_ROOT"))
    return var;
  if (const char *var = std::getenv("ROCM_HOME"))
    return var;
  return __DEFAULT_ROCM_PATH__;
}

SerializeGPUModuleBase::SerializeGPUModuleBase(
    Operation &module, ROCDLTargetAttr target,
    const gpu::TargetOptions &targetOptions)
    : ModuleToObject(module, target.getTriple(), target.getChip(),
                     target.getFeatures(), target.getO()),
      target(target), toolkitPath(targetOptions.getToolkitPath()),
      librariesToLink(targetOptions.getLibrariesToLink()) {

  // If `targetOptions` has an empty toolkitPath use `getROCMPath`
  if (toolkitPath.empty())
    toolkitPath = getROCMPath();

  // Append the files in the target attribute.
  if (target.getLink())
    librariesToLink.append(target.getLink().begin(), target.getLink().end());
}

void SerializeGPUModuleBase::init() {
  static llvm::once_flag initializeBackendOnce;
  llvm::call_once(initializeBackendOnce, []() {
  // If the `AMDGPU` LLVM target was built, initialize it.
#if LLVM_HAS_AMDGPU_TARGET
    LLVMInitializeAMDGPUTarget();
    LLVMInitializeAMDGPUTargetInfo();
    LLVMInitializeAMDGPUTargetMC();
    LLVMInitializeAMDGPUAsmParser();
    LLVMInitializeAMDGPUAsmPrinter();
#endif
  });
}

ROCDLTargetAttr SerializeGPUModuleBase::getTarget() const { return target; }

StringRef SerializeGPUModuleBase::getToolkitPath() const { return toolkitPath; }

ArrayRef<Attribute> SerializeGPUModuleBase::getLibrariesToLink() const {
  return librariesToLink;
}

LogicalResult SerializeGPUModuleBase::appendStandardLibs(AMDGCNLibraries libs) {
  if (libs == AMDGCNLibraries::None)
    return success();
  StringRef pathRef = getToolkitPath();

  // Get the path for the device libraries
  SmallString<256> path;
  path.insert(path.begin(), pathRef.begin(), pathRef.end());
  llvm::sys::path::append(path, "amdgcn", "bitcode");
  pathRef = StringRef(path.data(), path.size());

  // Fail if the path is invalid.
  if (!llvm::sys::fs::is_directory(pathRef)) {
    getOperation().emitError() << "ROCm amdgcn bitcode path: " << pathRef
                               << " does not exist or is not a directory";
    return failure();
  }

  // Helper function for adding a library.
  auto addLib = [&](const Twine &lib) -> bool {
    auto baseSize = path.size();
    llvm::sys::path::append(path, lib);
    StringRef pathRef(path.data(), path.size());
    if (!llvm::sys::fs::is_regular_file(pathRef)) {
      getOperation().emitRemark() << "bitcode library path: " << pathRef
                                  << " does not exist or is not a file";
      return true;
    }
    librariesToLink.push_back(StringAttr::get(target.getContext(), pathRef));
    path.truncate(baseSize);
    return false;
  };

  // Add ROCm device libraries. Fail if any of the libraries is not found, ie.
  // if any of the `addLib` failed.
  if ((any(libs & AMDGCNLibraries::Ocml) && addLib("ocml.bc")) ||
      (any(libs & AMDGCNLibraries::Ockl) && addLib("ockl.bc")) ||
      (any(libs & AMDGCNLibraries::Hip) && addLib("hip.bc")) ||
      (any(libs & AMDGCNLibraries::OpenCL) && addLib("opencl.bc")))
    return failure();
  return success();
}

std::optional<SmallVector<std::unique_ptr<llvm::Module>>>
SerializeGPUModuleBase::loadBitcodeFiles(llvm::Module &module) {
  // Return if there are no libs to load.
  if (deviceLibs == AMDGCNLibraries::None && librariesToLink.empty())
    return SmallVector<std::unique_ptr<llvm::Module>>();
  if (failed(appendStandardLibs(deviceLibs)))
    return std::nullopt;
  SmallVector<std::unique_ptr<llvm::Module>> bcFiles;
  if (failed(loadBitcodeFilesFromList(module.getContext(), librariesToLink,
                                      bcFiles, true)))
    return std::nullopt;
  return std::move(bcFiles);
}

LogicalResult SerializeGPUModuleBase::handleBitcodeFile(llvm::Module &module) {
  // Some ROCM builds don't strip this like they should
  if (auto *openclVersion = module.getNamedMetadata("opencl.ocl.version"))
    module.eraseNamedMetadata(openclVersion);
  // Stop spamming us with clang version numbers
  if (auto *ident = module.getNamedMetadata("llvm.ident"))
    module.eraseNamedMetadata(ident);
  // Override the libModules datalayout and target triple with the compiler's
  // data layout should there be a discrepency.
  setDataLayoutAndTriple(module);
  return success();
}

void SerializeGPUModuleBase::handleModulePreLink(llvm::Module &module) {
  // If all libraries are not set, traverse the module to determine which
  // libraries are required.
  if (deviceLibs != AMDGCNLibraries::All) {
    for (llvm::Function &f : module.functions()) {
      if (f.hasExternalLinkage() && f.hasName() && !f.hasExactDefinition()) {
        StringRef funcName = f.getName();
        if ("printf" == funcName)
          deviceLibs |= AMDGCNLibraries::OpenCL | AMDGCNLibraries::Ockl |
                        AMDGCNLibraries::Ocml;
        if (funcName.starts_with("__ockl_"))
          deviceLibs |= AMDGCNLibraries::Ockl;
        if (funcName.starts_with("__ocml_"))
          deviceLibs |= AMDGCNLibraries::Ocml;
        if (funcName == "__atomic_work_item_fence")
          deviceLibs |= AMDGCNLibraries::Hip;
      }
    }
  }
  addControlVariables(module, deviceLibs, target.hasWave64(), target.hasDaz(),
                      target.hasFiniteOnly(), target.hasUnsafeMath(),
                      target.hasFastMath(), target.hasCorrectSqrt(),
                      target.getAbi());
}

void SerializeGPUModuleBase::handleModulePostLink(llvm::Module &module) {
  // Mirror the effect of `-amdgpu-internalize-symbols` from ROCm device cc1.
  // After ockl.bc and other bitcode libraries are linked in, symbols like
  // `__assert_fail` and `__ockl_fprintf_*` are defined as `weak hidden` and
  // are unreachable from the kernel entry points.  Without internalization +
  // GlobalDCE these dead definitions survive into the optimizer, causing
  // `AMDGPUAttributorPass` to conservatively mark the kernel `convergent` and
  // preventing inference of `amdgpu-no-*` attributes.  Eliminating them before
  // optimization matches what ROCm's clang device cc1 does and restores VOPD
  // code generation and optimal VGPR usage.

  // Move shared memory (LDS) globals from the default address space (0) to
  // addrspace(3).  CIR currently lowers __shared__ variables as plain
  // addrspace-0 globals, but AMDGPU requires them in addrspace(3) for correct
  // codegen and for InferAddressSpaces to promote pointers into LDS.
  // Without this, all LDS accesses go through flat pointers, preventing the
  // optimizer from distinguishing shared-memory stores from dead private
  // stores, which causes it to eliminate MFMA computation as dead code.
  {
    llvm::SmallVector<llvm::GlobalVariable *> toMove;
    for (llvm::GlobalVariable &GV : module.globals()) {
      if (GV.getAddressSpace() != 0)
        continue;
      if (!GV.hasLocalLinkage())
        continue;
      if (!llvm::isa<llvm::UndefValue>(GV.getInitializer()))
        continue;
      toMove.push_back(&GV);
    }
    for (llvm::GlobalVariable *OldGV : toMove) {
      auto *NewGV = new llvm::GlobalVariable(
          module, OldGV->getValueType(), OldGV->isConstant(),
          OldGV->getLinkage(), OldGV->getInitializer(), "", nullptr,
          OldGV->getThreadLocalMode(), /*AddressSpace=*/3);
      NewGV->copyAttributesFrom(OldGV);
      NewGV->takeName(OldGV);

      auto *Cast = llvm::ConstantExpr::getAddrSpaceCast(
          NewGV, OldGV->getType());
      OldGV->replaceAllUsesWith(Cast);
      OldGV->eraseFromParent();
    }
  }

  // Match the AMDGPU kernel ABI produced by OGCG
  // (AMDGPUABIInfo::classifyKernelArgumentType):
  //
  // 1. Flat pointer args (ptr addrspace(0)) → global (ptr addrspace(1)).
  //    This matches coerceKernelArgumentType for HIP.
  //
  // 2. By-value aggregate args (%StructType) → byref pointer in constant
  //    address space (ptr addrspace(4) byref(%StructType)).
  //    This matches getIndirectAliased with opencl_constant for struct args.
  //
  // Without (2), the struct is stored to a private alloca and pointers are
  // loaded from private memory.  The optimizer cannot trace these pointers
  // back to kernel arguments and may infer readnone, eliminating all
  // computation (including MFMA intrinsics) as dead code.
  llvm::SmallVector<llvm::Function *> kernelsToFix;
  for (llvm::Function &F : module) {
    if (F.isDeclaration() ||
        F.getCallingConv() != llvm::CallingConv::AMDGPU_KERNEL)
      continue;
    for (auto &Arg : F.args()) {
      bool isFlatPtr = Arg.getType()->isPointerTy() &&
                       Arg.getType()->getPointerAddressSpace() == 0;
      bool isByValStruct = !Arg.getType()->isPointerTy() &&
                           Arg.getType()->isAggregateType();
      if (isFlatPtr || isByValStruct) {
        kernelsToFix.push_back(&F);
        break;
      }
    }
  }

  for (llvm::Function *OldF : kernelsToFix) {
    llvm::LLVMContext &Ctx = OldF->getContext();
    llvm::SmallVector<llvm::Type *> newArgTypes;
    bool changed = false;

    for (auto &Arg : OldF->args()) {
      if (Arg.getType()->isPointerTy() &&
          Arg.getType()->getPointerAddressSpace() == 0) {
        newArgTypes.push_back(llvm::PointerType::get(Ctx, 1));
        changed = true;
      } else if (!Arg.getType()->isPointerTy() &&
                 Arg.getType()->isAggregateType()) {
        newArgTypes.push_back(llvm::PointerType::get(Ctx, 4));
        changed = true;
      } else {
        newArgTypes.push_back(Arg.getType());
      }
    }
    if (!changed)
      continue;

    auto *NewFT = llvm::FunctionType::get(OldF->getReturnType(), newArgTypes,
                                          OldF->isVarArg());
    auto *NewF = llvm::Function::Create(NewFT, OldF->getLinkage(),
                                        OldF->getAddressSpace(), "", &module);
    NewF->copyAttributesFrom(OldF);
    NewF->setCallingConv(OldF->getCallingConv());
    NewF->setComdat(OldF->getComdat());

    llvm::SmallVector<std::pair<unsigned, llvm::MDNode *>> MDs;
    OldF->getAllMetadata(MDs);
    for (auto &[ID, MD] : MDs)
      NewF->setMetadata(ID, MD);

    NewF->splice(NewF->begin(), OldF);

    llvm::IRBuilder<> B(&NewF->getEntryBlock(),
                        NewF->getEntryBlock().begin());
    for (unsigned i = 0; i < OldF->arg_size(); i++) {
      llvm::Argument *OldArg = OldF->getArg(i);
      llvm::Argument *NewArg = NewF->getArg(i);
      NewArg->setName(OldArg->getName());

      llvm::Type *OldTy = OldArg->getType();
      llvm::Type *NewTy = NewArg->getType();

      if (OldTy == NewTy) {
        OldArg->replaceAllUsesWith(NewArg);
      } else if (OldTy->isPointerTy() && NewTy->isPointerTy()) {
        auto *Cast = B.CreateAddrSpaceCast(NewArg, OldTy);
        OldArg->replaceAllUsesWith(Cast);
      } else {
        // By-value struct → byref ptr addrspace(4).
        // Match OGCG: use memcpy from AS4 kernarg to AS5 alloca instead
        // of whole-struct load + store.  The load/store path generates
        // insertvalue/extractvalue chains with flat pointers that
        // InferAddressSpaces cannot trace through aggregate operations.
        llvm::Align srcAlign =
            OldF->getParamAlign(i).value_or(
                module.getDataLayout().getABITypeAlign(OldTy));
        llvm::AttrBuilder AB(Ctx);
        AB.addByRefAttr(OldTy);
        AB.addAttribute(llvm::Attribute::ReadOnly);
        AB.addAttribute(llvm::Attribute::NoUndef);
        AB.addAttribute(llvm::Attribute::getWithAlignment(Ctx, srcAlign));
        AB.addCapturesAttr(llvm::CaptureInfo::none());
        NewF->addParamAttrs(i, AB);

        uint64_t size = module.getDataLayout().getTypeAllocSize(OldTy);
        llvm::SmallVector<llvm::StoreInst *, 4> storesToReplace;
        for (auto &use : OldArg->uses()) {
          if (auto *SI = llvm::dyn_cast<llvm::StoreInst>(use.getUser()))
            if (SI->getValueOperand() == OldArg)
              storesToReplace.push_back(SI);
        }
        for (auto *SI : storesToReplace) {
          llvm::IRBuilder<> SB(SI);
          SB.CreateMemCpy(SI->getPointerOperand(), SI->getAlign(),
                          NewArg, srcAlign, size);
          SI->eraseFromParent();
        }
        if (!OldArg->use_empty()) {
          auto *Loaded = B.CreateLoad(OldTy, NewArg);
          OldArg->replaceAllUsesWith(Loaded);
        }
      }
    }

    NewF->takeName(OldF);
    OldF->eraseFromParent();
  }

  // Convert by-value aggregate returns to sret (struct return by pointer).
  //
  // CIR generates functions that return structs by value:
  //   %StructType @func(ptr, ptr)
  // OGCG instead uses sret (passing the destination as a pointer param):
  //   void @func(ptr sret(%StructType), ptr, ptr)
  //
  // By-value returns create insertvalue/extractvalue chains with flat pointers
  // embedded in aggregate values.  InferAddressSpaces cannot trace AS5 pointers
  // through these aggregate operations, causing the optimizer to lose track of
  // address-space provenance at CK scale (14K+ unresolvable addrspacecasts).
  //
  // Converting to sret eliminates aggregate value passing — the callee writes
  // directly to the caller's alloca via the sret pointer.  After inlining, the
  // writes go to the caller's AS5 alloca without any flat-pointer indirection.
  //
  // Also handles the reverse mismatch: call sites that already use sret
  // (call void @func(ptr sret, ...)) calling a callee that returns by value.
  if (std::getenv("CIR_DUMP_GPU_LLVM")) {
    std::error_code ec;
    llvm::raw_fd_ostream os("/tmp/cir_gpu_pre_sret.ll", ec);
    if (!ec)
      module.print(os, nullptr);
  }

  {
    llvm::LLVMContext &Ctx = module.getContext();
    llvm::SmallVector<llvm::Function *> funcsToConvert;
    for (llvm::Function &F : module) {
      if (F.isDeclaration())
        continue;
      if (F.getCallingConv() == llvm::CallingConv::AMDGPU_KERNEL)
        continue;
      if (!F.getReturnType()->isAggregateType())
        continue;
      funcsToConvert.push_back(&F);
    }

    for (llvm::Function *OldF : funcsToConvert) {
      llvm::Type *RetTy = OldF->getReturnType();
      llvm::Align RetAlign =
          module.getDataLayout().getABITypeAlign(RetTy);

      // Build new function type: void (ptr sret, original_params...)
      llvm::SmallVector<llvm::Type *> newParamTypes;
      newParamTypes.push_back(llvm::PointerType::get(Ctx, 5));
      for (auto &Arg : OldF->args())
        newParamTypes.push_back(Arg.getType());

      auto *NewFT =
          llvm::FunctionType::get(llvm::Type::getVoidTy(Ctx), newParamTypes,
                                  OldF->isVarArg());
      auto *NewF = llvm::Function::Create(NewFT, OldF->getLinkage(),
                                          OldF->getAddressSpace(), "", &module);
      NewF->setCallingConv(OldF->getCallingConv());
      NewF->setComdat(OldF->getComdat());

      // Copy function-level attributes only (not return or param attrs, since
      // param indices are shifted by the new sret param).
      llvm::AttrBuilder FnAB(Ctx, OldF->getAttributes().getFnAttrs());
      if (FnAB.hasAttributes())
        NewF->addFnAttrs(FnAB);

      // Copy param attrs at shifted indices (old param i → new param i+1).
      for (unsigned i = 0; i < OldF->arg_size(); i++) {
        llvm::AttrBuilder ParamAB(Ctx, OldF->getAttributes().getParamAttrs(i));
        if (ParamAB.hasAttributes())
          NewF->addParamAttrs(i + 1, ParamAB);
      }

      llvm::SmallVector<std::pair<unsigned, llvm::MDNode *>> MDs;
      OldF->getAllMetadata(MDs);
      for (auto &[ID, MD] : MDs)
        NewF->setMetadata(ID, MD);

      // Add sret attribute to the new first parameter.
      llvm::AttrBuilder AB(Ctx);
      AB.addStructRetAttr(RetTy);
      AB.addAttribute(llvm::Attribute::getWithAlignment(Ctx, RetAlign));
      AB.addAttribute(llvm::Attribute::NoAlias);
      NewF->addParamAttrs(0, AB);

      NewF->splice(NewF->begin(), OldF);

      // Set up argument mapping: new arg 0 = sret, new arg i+1 = old arg i.
      llvm::Argument *SRetArg = NewF->getArg(0);
      SRetArg->setName("sret");
      for (unsigned i = 0; i < OldF->arg_size(); i++) {
        llvm::Argument *OldArg = OldF->getArg(i);
        llvm::Argument *NewArg = NewF->getArg(i + 1);
        NewArg->setName(OldArg->getName());
        OldArg->replaceAllUsesWith(NewArg);
      }

      // Replace ret instructions: store return value to sret, then ret void.
      for (llvm::BasicBlock &BB : *NewF) {
        auto *RI = llvm::dyn_cast<llvm::ReturnInst>(BB.getTerminator());
        if (!RI || !RI->getReturnValue())
          continue;
        llvm::IRBuilder<> B(RI);
        B.CreateStore(RI->getReturnValue(), SRetArg);
        B.CreateRetVoid();
        RI->eraseFromParent();
      }

      NewF->takeName(OldF);

      // Update all call sites.
      llvm::SmallVector<llvm::CallBase *> callers;
      for (auto *U : OldF->users()) {
        if (auto *CB = llvm::dyn_cast<llvm::CallBase>(U))
          callers.push_back(CB);
      }

      for (llvm::CallBase *CB : callers) {
        llvm::IRBuilder<> B(CB);

        if (CB->getType()->isVoidTy() &&
            CB->arg_size() == OldF->arg_size() + 1) {
          // Call site already uses sret pattern:
          //   call void @func(ptr sret %dest, args...)
          // Just update to call the new function directly.
          llvm::SmallVector<llvm::Value *> NewArgs;
          llvm::Value *SRetPtr = CB->getArgOperand(0);
          // Cast sret ptr to AS5 if needed.
          if (SRetPtr->getType() != llvm::PointerType::get(Ctx, 5))
            SRetPtr = B.CreateAddrSpaceCast(
                SRetPtr, llvm::PointerType::get(Ctx, 5));
          NewArgs.push_back(SRetPtr);
          for (unsigned i = 1; i < CB->arg_size(); i++)
            NewArgs.push_back(CB->getArgOperand(i));

          auto *NewCall = B.CreateCall(NewF, NewArgs);
          NewCall->setCallingConv(CB->getCallingConv());
          CB->eraseFromParent();
        } else {
          // Call site uses by-value return:
          //   %result = call %StructType @func(args...)
          //   store %StructType %result, ptr %dest  (typical pattern)
          // Allocate AS5 temp, pass as sret, load result if needed.
          llvm::SmallVector<llvm::Value *> NewArgs;
          auto *Alloca = B.CreateAlloca(RetTy, module.getDataLayout().
              getAllocaAddrSpace());
          NewArgs.push_back(Alloca);
          for (unsigned i = 0; i < CB->arg_size(); i++)
            NewArgs.push_back(CB->getArgOperand(i));

          auto *NewCall = B.CreateCall(NewF, NewArgs);
          NewCall->setCallingConv(CB->getCallingConv());

          if (!CB->use_empty()) {
            auto *Loaded = B.CreateLoad(RetTy, Alloca);
            CB->replaceAllUsesWith(Loaded);
          }
          CB->eraseFromParent();
        }
      }

      // Replace any remaining non-CallBase users (e.g., ConstantExpr refs,
      // stored function pointers) before erasing.  With opaque pointers both
      // functions are just `ptr`, so RAUW is valid.
      if (!OldF->use_empty())
        OldF->replaceAllUsesWith(NewF);
      OldF->eraseFromParent();
    }
  }

  // Replace `store %EmptyStruct undef` with `store %EmptyStruct zeroinitializer`.
  //
  // CIR generates `store %EmptyStruct undef` for empty struct types like
  // ck::integral_constant that have no runtime data.  After inlining + SROA,
  // these undef bytes can contaminate insertvalue chains that build containing
  // structs, causing real data fields to become undef.  Using zeroinitializer
  // prevents poison propagation through aggregate operations.
  for (llvm::Function &F : module) {
    for (llvm::BasicBlock &BB : F) {
      for (llvm::Instruction &I : BB) {
        auto *SI = llvm::dyn_cast<llvm::StoreInst>(&I);
        if (!SI)
          continue;
        if (!llvm::isa<llvm::UndefValue>(SI->getValueOperand()))
          continue;
        llvm::Type *Ty = SI->getValueOperand()->getType();
        if (!Ty->isAggregateType())
          continue;
        SI->setOperand(0, llvm::Constant::getNullValue(Ty));
      }
    }
  }

  // Mark non-kernel functions as inlinehint (not alwaysinline) so that
  // O3's CGSCC inliner runs per-function optimization before inlining.
  // OGCG emits 7000+ small functions with inlinehint; O3 folds template-
  // metaprogramming branches in each small function, then inlines clean
  // code into kernels.  Using alwaysinline + AlwaysInlinerPass forces
  // inlining before any optimization, producing monster kernel functions
  // with 1700+ unfoldable branches that cause barrier deadlocks on GPU.
  //
  // Also fix CIR's incorrect memory(argmem: readwrite) annotation on
  // device functions and strip noalias from sret parameters.
  for (llvm::Function &F : module) {
    if (F.isDeclaration())
      continue;
    if (F.getCallingConv() != llvm::CallingConv::AMDGPU_KERNEL) {
      F.addFnAttr(llvm::Attribute::InlineHint);
      F.setMemoryEffects(llvm::MemoryEffects::unknown());
      F.removeFnAttr(llvm::Attribute::WillReturn);
      // Strip noalias from non-kernel args (sret params etc.) — these are
      // inlined helpers where CIR's noalias annotations may be incorrect.
      for (llvm::Argument &A : F.args())
        A.removeAttr(llvm::Attribute::NoAlias);
    }
  }

  auto mustPreserve = [](const llvm::GlobalValue &GV) -> bool {
    if (const auto *F = llvm::dyn_cast<llvm::Function>(&GV))
      return F->isDeclaration() ||
             F->getCallingConv() == llvm::CallingConv::AMDGPU_KERNEL;
    GV.removeDeadConstantUsers();
    return !GV.use_empty();
  };

  if (std::getenv("CIR_DUMP_GPU_LLVM")) {
    std::error_code ec;
    llvm::raw_fd_ostream os("/tmp/cir_gpu_pre_dce.ll", ec);
    if (!ec)
      module.print(os, nullptr);
  }

  llvm::ModuleAnalysisManager mam;
  mam.registerPass([] { return llvm::PassInstrumentationAnalysis(); });
  llvm::ModulePassManager mpm;
  mpm.addPass(llvm::InternalizePass(mustPreserve));
  mpm.addPass(llvm::GlobalDCEPass());
  mpm.run(module, mam);

  if (std::getenv("CIR_DUMP_GPU_LLVM")) {
    std::error_code ec;
    llvm::raw_fd_ostream os("/tmp/cir_gpu_post_dce.ll", ec);
    if (!ec)
      module.print(os, nullptr);
  }
}

LogicalResult SerializeGPUModuleBase::optimizeModule(llvm::Module &module,
                                                     int optLevel) {
  // On AMDGPU targets with packed FP32 ops (e.g. gfx90a/gfx94x), the LLVM
  // SLP cost model slightly underestimates the benefit of vectorizing
  // independent float accumulator chains into <2 x float> operations.  ROCm
  // clang's LLVM 20 SLP finds these trees with cost -2; LLVM 23 evaluates them
  // at cost 0 and skips them.  Lowering the threshold to -5 recovers the
  // vectorization (v_pk_add_f32 / v_pk_mul_f32), reducing VGPR pressure from
  // ~165 to ~148 and matching ROCm clang's output.
  //
  // The adjustment is guarded by getNumOccurrences()==0 so that an explicit
  // -slp-threshold from the user or tests always takes precedence.
  auto &opts = llvm::cl::getRegisteredOptions();

  // Do NOT set amdgpu-function-calls=false or amdgpu-early-inline-all=true.
  // OGCG's standard O3 pipeline inlines all device functions via the CGSCC
  // inliner after per-function optimization. Forcing early inlining with
  // these flags (or AlwaysInliner) prevents per-function branch folding,
  // leaving 1700+ unfoldable branches in the kernel.

  llvm::cl::Option *slpOpt = opts.lookup("slp-threshold");

  // Packed FP32 ops (v_pk_add_f32 / v_pk_mul_f32, ISA feature +mai-insts) are
  // available on gfx90a and gfx94x (MI200 / MI300 series).
  StringRef chip = target.getChip();
  bool hasPackedFP32 =
      chip.starts_with("gfx90a") || chip.starts_with("gfx940") ||
      chip.starts_with("gfx941") || chip.starts_with("gfx942");

  bool adjustedThreshold = false;
  if (slpOpt && hasPackedFP32 && slpOpt->getNumOccurrences() == 0) {
    // Set threshold to -5 via the option's own occurrence handler.  This is
    // safe because we guard on getNumOccurrences()==0 (no user override) and
    // restore via setDefault() after optimization.
    std::string val = "-5";
    if (!slpOpt->addOccurrence(/*pos=*/0, "slp-threshold", val,
                               /*MultiArg=*/false))
      adjustedThreshold = true;
  }

  {
    FailureOr<llvm::TargetMachine *> tm = getOrCreateTargetMachine();
    if (succeeded(tm)) {
      llvm::LoopAnalysisManager lam;
      llvm::FunctionAnalysisManager fam;
      llvm::CGSCCAnalysisManager cgam;
      llvm::ModuleAnalysisManager mam2;

      fam.registerPass(
          [&] { return (*tm)->getTargetIRAnalysis(); });

      llvm::PassBuilder pb(*tm);
      pb.registerModuleAnalyses(mam2);
      pb.registerCGSCCAnalyses(cgam);
      pb.registerFunctionAnalyses(fam);
      pb.registerLoopAnalyses(lam);
      pb.crossRegisterProxies(lam, fam, cgam, mam2);

      // CIR lowers __builtin_memcpy to a C library call `memcpy(dst, src, n)`
      // instead of the LLVM intrinsic `llvm.memcpy`.  On AMDGPU there is no C
      // library, so the symbol stays unresolved and the code object fails to
      // load.  Rewrite all calls to the external `memcpy` into
      // `llvm.memcpy.p0.p0.i64` intrinsic calls.
      if (auto *memcpyFn = module.getFunction("memcpy")) {
        auto &ctx = module.getContext();
        auto *i1Ty = llvm::Type::getInt1Ty(ctx);
        auto *i64Ty = llvm::Type::getInt64Ty(ctx);
        auto *ptrTy = llvm::PointerType::getUnqual(ctx);
        auto *intrinsic = llvm::Intrinsic::getOrInsertDeclaration(
            &module, llvm::Intrinsic::memcpy,
            {ptrTy, ptrTy, i64Ty});
        llvm::SmallVector<llvm::CallInst *, 16> callsToReplace;
        for (auto *U : memcpyFn->users())
          if (auto *CI = llvm::dyn_cast<llvm::CallInst>(U))
            callsToReplace.push_back(CI);
        for (auto *CI : callsToReplace) {
          llvm::IRBuilder<> B(CI);
          B.CreateCall(
              intrinsic,
              {CI->getArgOperand(0), CI->getArgOperand(1),
               CI->getArgOperand(2), llvm::ConstantInt::getFalse(i1Ty)});
          if (!CI->getType()->isVoidTy())
            CI->replaceAllUsesWith(CI->getArgOperand(0));
          CI->eraseFromParent();
        }
        if (memcpyFn->use_empty())
          memcpyFn->eraseFromParent();
      }

      // NOTE: Previously, this pass eliminated the kernarg struct copy
      // (memcpy AS4→AS5) and rewrote all accesses to load directly from
      // AS4 (constant memory).  However, the memcpy+alloca pattern is
      // actually REQUIRED for correct optimization: SROA can split the
      // alloca into individual fields, giving each field a direct SSA
      // value from the AS4 source via the memcpy.  Without it, the
      // Problem sub-struct pointer goes through AS4→flat addrspacecast,
      // and after inlining, the optimizer loses track of the integer
      // dimension fields (M,N,K,strides), folding buffer size
      // computations to zero (make.buffer.rsrc with num_records=0).
      // KEEP the memcpy+alloca pattern — it matches OGCG and gives
      // SROA the visibility it needs.

      // No AlwaysInlinerPass here — O3's CGSCC inliner handles inlining
      // after per-function optimization, which folds template-metaprogramming
      // branches in each small function before inlining clean code into
      // kernels.  See OGCG path: 7000+ functions → per-function opt →
      // inline → 5 functions with 1 branch.

      // CIR emits lifetime markers at CIR scope boundaries.  These are
      // incorrect: alloca addresses escape through flat pointers or pointer
      // stores to other allocas.  Strip all lifetime markers.
      {
        llvm::SmallVector<llvm::IntrinsicInst *, 64> toRemove;
        for (llvm::Function &F : module) {
          for (auto &BB : F) {
            for (auto &I : BB) {
              if (auto *II = llvm::dyn_cast<llvm::IntrinsicInst>(&I)) {
                if (II->getIntrinsicID() == llvm::Intrinsic::lifetime_start ||
                    II->getIntrinsicID() == llvm::Intrinsic::lifetime_end)
                  toRemove.push_back(II);
              }
            }
          }
        }
        for (auto *II : toRemove)
          II->eraseFromParent();
        if (std::getenv("CIR_DUMP_GPU_LLVM"))
          llvm::errs() << "[CIR] Removed " << toRemove.size()
                       << " lifetime markers\n";
      }

      if (std::getenv("CIR_DUMP_GPU_LLVM")) {
        unsigned fnCount = 0;
        for (auto &F : module)
          if (!F.isDeclaration())
            ++fnCount;
        llvm::errs() << "[CIR] Post-inline+DCE: " << fnCount
                     << " function definitions\n";
      }

      // After inlining, the kernarg sub-struct pointer appears as:
      //   %gep = gep i8, ptr addrspace(4) %0, 16
      //   %flat = addrspacecast ptr addrspace(4) %gep to ptr
      //   %field = gep Problem, ptr %flat, 0, N
      //   %val = load i32, ptr %field
      //
      // IAS should promote these to AS4 loads, but at CK scale (2.8M flat
      // loads, 98+ kernels) it fails to do so.  Rewrite the chains manually:
      // trace each `addrspacecast AS4→flat` through GEP and load users and
      // replace them with AS4 equivalents.
      for (llvm::Function &F : module) {
        if (F.getCallingConv() != llvm::CallingConv::AMDGPU_KERNEL)
          continue;

        llvm::SmallVector<llvm::AddrSpaceCastInst *, 4> as4Casts;
        for (auto &BB : F) {
          for (auto &I : BB) {
            auto *ASC = llvm::dyn_cast<llvm::AddrSpaceCastInst>(&I);
            if (!ASC)
              continue;
            if (ASC->getSrcAddressSpace() == 4 &&
                ASC->getDestAddressSpace() == 0)
              as4Casts.push_back(ASC);
          }
        }

        if (std::getenv("CIR_DUMP_GPU_LLVM"))
          llvm::errs() << "[CIR] AS4 rewrite: " << F.getName()
                       << " has " << as4Casts.size()
                       << " AS4→flat addrspacecasts\n";

        unsigned gepCount = 0, loadCount = 0, otherCount = 0;
        for (auto *ASC : as4Casts) {
          llvm::Value *AS4Src = ASC->getOperand(0);

          // Worklist: replace flat pointer uses with AS4 equivalents.
          // IMPORTANT: We must NOT use replaceAllUsesWith on GEPs because
          // ptr (AS0) and ptr addrspace(4) are different types — RAUW
          // silently corrupts IR with assertions off.  Instead, we trace
          // each user individually and create AS4 equivalents.
          llvm::SmallVector<std::pair<llvm::Instruction *, llvm::Value *>>
              worklist;
          llvm::SmallVector<llvm::Instruction *, 32> toErase;

          for (auto *U : ASC->users()) {
            if (auto *UI = llvm::dyn_cast<llvm::Instruction>(U))
              worklist.push_back({UI, AS4Src});
          }

          while (!worklist.empty()) {
            auto [Inst, AS4Base] = worklist.pop_back_val();
            llvm::IRBuilder<> B(Inst);

            if (auto *GEP =
                    llvm::dyn_cast<llvm::GetElementPtrInst>(Inst)) {
              if (GEP->getPointerOperand()->getType()
                      ->getPointerAddressSpace() != 0)
                continue;
              llvm::SmallVector<llvm::Value *> indices(GEP->idx_begin(),
                                                       GEP->idx_end());
              auto *NewGEP = B.CreateGEP(GEP->getSourceElementType(),
                                         AS4Base, indices, "",
                                         GEP->isInBounds());
              for (auto *U : GEP->users()) {
                if (auto *UI = llvm::dyn_cast<llvm::Instruction>(U))
                  worklist.push_back({UI, NewGEP});
              }
              toErase.push_back(GEP);
              ++gepCount;

            } else if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(Inst)) {
              if (LI->getPointerOperand()->getType()
                      ->getPointerAddressSpace() != 0)
                continue;
              auto *NewLoad = B.CreateAlignedLoad(
                  LI->getType(), AS4Base, LI->getAlign(),
                  LI->isVolatile());
              LI->replaceAllUsesWith(NewLoad);
              toErase.push_back(LI);
              ++loadCount;
            } else {
              ++otherCount;
            }
          }

          // Erase dead instructions leaf-first (reverse of discovery order).
          for (auto it = toErase.rbegin(); it != toErase.rend(); ++it) {
            if ((*it)->use_empty())
              (*it)->eraseFromParent();
          }
          if (ASC->use_empty())
            ASC->eraseFromParent();
        }
        if (std::getenv("CIR_DUMP_GPU_LLVM") && !as4Casts.empty())
          llvm::errs() << "[CIR]   → rewrote " << gepCount << " GEPs, "
                       << loadCount << " loads, " << otherCount
                       << " other uses\n";
      }

      if (std::getenv("CIR_DUMP_GPU_LLVM")) {
        unsigned as4i32 = 0;
        for (llvm::Function &F : module) {
          if (F.getCallingConv() != llvm::CallingConv::AMDGPU_KERNEL)
            continue;
          for (auto &BB : F)
            for (auto &I : BB)
              if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(&I))
                if (LI->getType()->isIntegerTy(32) &&
                    LI->getPointerOperand()
                        ->getType()
                        ->getPointerAddressSpace() == 4)
                  ++as4i32;
        }
        llvm::errs() << "[CIR] Post-inline AS4 rewrite: " << as4i32
                     << " i32 AS4 loads in kernels\n";
      }

      // After inlining, CIR-generated code has massive alloca+addrspacecast
      // chains (every local goes through addrspacecast AS5→flat).  Run
      // SROA+InstCombine+IAS to clean up before the full O3 pipeline, giving
      // IAS a chance to promote flat loads to AS4/AS5 before DSE/ADCE can
      // eliminate them as "dead".
      // Diagnose AS4 load user patterns before cleanup.
      if (std::getenv("CIR_DUMP_GPU_LLVM")) {
        unsigned storeUsers = 0, compUsers = 0, phiUsers = 0, otherUsers = 0;
        unsigned totalLoads = 0;
        for (llvm::Function &F : module) {
          if (F.getCallingConv() != llvm::CallingConv::AMDGPU_KERNEL)
            continue;
          for (auto &BB : F)
            for (auto &I : BB) {
              auto *LI = llvm::dyn_cast<llvm::LoadInst>(&I);
              if (!LI || !LI->getType()->isIntegerTy(32) ||
                  LI->getPointerOperand()
                      ->getType()->getPointerAddressSpace() != 4)
                continue;
              ++totalLoads;
              for (auto *U : LI->users()) {
                if (llvm::isa<llvm::StoreInst>(U))
                  ++storeUsers;
                else if (llvm::isa<llvm::PHINode>(U))
                  ++phiUsers;
                else if (llvm::isa<llvm::BinaryOperator>(U) ||
                         llvm::isa<llvm::CmpInst>(U) ||
                         llvm::isa<llvm::CastInst>(U) ||
                         llvm::isa<llvm::SelectInst>(U))
                  ++compUsers;
                else
                  ++otherUsers;
              }
            }
        }
        llvm::errs() << "[CIR] AS4 i32 load users: " << totalLoads
                     << " loads → " << storeUsers << " stores, "
                     << compUsers << " comp, " << phiUsers << " phi, "
                     << otherUsers << " other\n";
      }

      if (std::getenv("CIR_DUMP_GPU_LLVM")) {
        std::error_code ec;
        llvm::raw_fd_ostream os("/tmp/cir_gpu_post_as4.ll", ec);
        if (!ec)
          module.print(os, nullptr);
      }

      // CIR generates alloca ptr (flat, 8-byte) in AS5, but stores a
      // ptr addrspace(5) (4-byte) and loads ptr (8-byte).  SROA can't
      // promote due to size mismatch.  This fixup shrinks the alloca to
      // ptr addrspace(5) and rewrites loads to the narrower type +
      // addrspacecast.  Must run AFTER SROA splits structs into scalar
      // fragments (the mismatch only appears in split .sroa.N allocas).
      auto runAS5PtrFixup = [&](llvm::Module &mod) -> unsigned {
        unsigned fixedAllocas = 0;
        unsigned totalAllocas = 0, ptrAllocas = 0;
        unsigned rejOther = 0, rejNoLoad = 0, rejNoStore = 0,
                 rejSameAS = 0;
        for (llvm::Function &F : mod) {
          for (auto &BB : F) {
            for (auto &I : BB) {
              auto *AI = llvm::dyn_cast<llvm::AllocaInst>(&I);
              if (!AI || AI->getType()->getPointerAddressSpace() != 5)
                continue;
              ++totalAllocas;
              auto *allocTy = AI->getAllocatedType();
              if (!allocTy->isPointerTy() ||
                  allocTy->getPointerAddressSpace() != 0)
                continue;
              ++ptrAllocas;

              unsigned storeAS = UINT_MAX;
              bool allStoresSameAS = true;
              bool hasLoad = false;
              bool hasOtherUser = false;

              auto checkUser = [&](llvm::User *U,
                                   llvm::Value *ptrOp) -> bool {
                if (auto *SI = llvm::dyn_cast<llvm::StoreInst>(U)) {
                  if (SI->getPointerOperand() == ptrOp) {
                    auto *valTy = SI->getValueOperand()->getType();
                    if (!valTy->isPointerTy())
                      return false;
                    unsigned as = valTy->getPointerAddressSpace();
                    if (storeAS == UINT_MAX)
                      storeAS = as;
                    else if (storeAS != as)
                      allStoresSameAS = false;
                  }
                  return true;
                }
                if (llvm::isa<llvm::LoadInst>(U)) {
                  hasLoad = true;
                  return true;
                }
                if (auto *II = llvm::dyn_cast<llvm::IntrinsicInst>(U)) {
                  return II->getIntrinsicID() ==
                             llvm::Intrinsic::lifetime_start ||
                         II->getIntrinsicID() ==
                             llvm::Intrinsic::lifetime_end;
                }
                return false;
              };

              for (auto *U : AI->users()) {
                if (auto *ASC =
                        llvm::dyn_cast<llvm::AddrSpaceCastInst>(U)) {
                  for (auto *AU : ASC->users()) {
                    if (!checkUser(AU, ASC)) {
                      hasOtherUser = true;
                      break;
                    }
                  }
                  if (hasOtherUser)
                    break;
                } else if (!checkUser(U, AI)) {
                  hasOtherUser = true;
                  break;
                }
              }

              if (hasOtherUser) { ++rejOther; continue; }
              if (!hasLoad) { ++rejNoLoad; continue; }
              if (!allStoresSameAS || storeAS == UINT_MAX) {
                ++rejNoStore;
                continue;
              }
              if (storeAS == 0) { ++rejSameAS; continue; }

              auto *narrowPtrTy = llvm::PointerType::get(
                  AI->getContext(), storeAS);

              AI->setAllocatedType(narrowPtrTy);

              llvm::SmallVector<llvm::LoadInst *, 8> loadsToFix;
              for (auto *U : AI->users()) {
                if (auto *ASC =
                        llvm::dyn_cast<llvm::AddrSpaceCastInst>(U)) {
                  for (auto *AU : ASC->users())
                    if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(AU))
                      if (LI->getType()->isPointerTy() &&
                          LI->getType()->getPointerAddressSpace() == 0)
                        loadsToFix.push_back(LI);
                } else if (auto *LI =
                               llvm::dyn_cast<llvm::LoadInst>(U)) {
                  if (LI->getType()->isPointerTy() &&
                      LI->getType()->getPointerAddressSpace() == 0)
                    loadsToFix.push_back(LI);
                }
              }

              for (auto *LI : loadsToFix) {
                llvm::IRBuilder<> B(LI->getNextNode());
                auto *NewLoad = new llvm::LoadInst(
                    narrowPtrTy, LI->getPointerOperand(), "",
                    LI->isVolatile(), LI->getAlign(), LI);
                auto *Cast =
                    B.CreateAddrSpaceCast(NewLoad, LI->getType());
                LI->replaceAllUsesWith(Cast);
                LI->eraseFromParent();
              }
              if (!loadsToFix.empty())
                ++fixedAllocas;
            }
          }
        }
        if (std::getenv("CIR_DUMP_GPU_LLVM"))
          llvm::errs() << "[CIR] AS5 ptr fixup: " << totalAllocas
                       << " total, " << ptrAllocas << " ptr-typed, "
                       << fixedAllocas << " fixed, rejected: "
                       << rejOther << " other-user, " << rejNoLoad
                       << " no-load, " << rejNoStore << " no-store, "
                       << rejSameAS << " same-AS\n";
        return fixedAllocas;
      };

      // No pre-cleanup passes needed: without AlwaysInliner, functions are
      // small and O3's per-function optimization handles IAS+SROA+SimplifyCFG
      // naturally (matching OGCG's pipeline).

      unsigned fixed = runAS5PtrFixup(module);

      if (std::getenv("CIR_DUMP_GPU_LLVM")) {
        unsigned a4 = 0, al = 0, ac = 0;
        for (auto &F : module)
          for (auto &BB : F)
            for (auto &I : BB) {
              if (llvm::isa<llvm::AllocaInst>(&I)) ++al;
              if (llvm::isa<llvm::AddrSpaceCastInst>(&I)) ++ac;
              if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(&I))
                if (LI->getType()->isIntegerTy(32) &&
                    LI->getPointerOperand()
                        ->getType()->getPointerAddressSpace() == 4)
                  ++a4;
            }
        llvm::errs() << "[CIR] Post-cleanup: " << a4 << " i32 AS4, "
                     << al << " allocas, " << ac << " ASC"
                     << ", fixed " << fixed << " ptr allocas\n";
      }

      // No AS5→flat rewriting needed: without pre-inlining, O3's AMDGPU IAS
      // handles address space resolution per-function before and after inlining.

      if (std::getenv("CIR_DUMP_GPU_LLVM")) {
        unsigned as4i32Post = 0, allocaCount = 0, ascCount = 0;
        unsigned kernelCount = 0;
        for (llvm::Function &F : module) {
          if (F.getCallingConv() == llvm::CallingConv::AMDGPU_KERNEL)
            ++kernelCount;
          for (auto &BB : F)
            for (auto &I : BB) {
              if (llvm::isa<llvm::AllocaInst>(&I))
                ++allocaCount;
              if (auto *ASC = llvm::dyn_cast<llvm::AddrSpaceCastInst>(&I))
                ++ascCount;
              if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(&I))
                if (LI->getType()->isIntegerTy(32) &&
                    LI->getPointerOperand()
                        ->getType()
                        ->getPointerAddressSpace() == 4)
                  ++as4i32Post;
            }
        }
        llvm::errs() << "[CIR] Pre-O3: " << as4i32Post
                     << " i32 AS4 loads, " << allocaCount << " allocas, "
                     << ascCount << " addrspacecasts, " << kernelCount
                     << " kernels\n";
      }
    }
  }

  // Strip noalias/alias.scope metadata that CIR's inliner generates.
  // CIR inlines all device functions via AlwaysInliner, which creates
  // noalias scope metadata at each inline site.  The scoped-noalias AA
  // in O3 then misidentifies aliasing stores to the same alloca accessed
  // through different address spaces (flat vs AS5), causing SROA/GVN/DSE
  // to eliminate tile-offset computations derived from workgroup.id.x.
  {
    unsigned noaliasID =
        module.getContext().getMDKindID("noalias");
    unsigned aliasScopeID =
        module.getContext().getMDKindID("alias.scope");
    unsigned scopeDeclCount = 0;
    for (auto &F : module) {
      for (auto &BB : F) {
        llvm::SmallVector<llvm::Instruction *, 8> toErase;
        for (auto &I : BB) {
          I.setMetadata(noaliasID, nullptr);
          I.setMetadata(aliasScopeID, nullptr);
          if (auto *II = llvm::dyn_cast<llvm::IntrinsicInst>(&I)) {
            if (II->getIntrinsicID() ==
                llvm::Intrinsic::experimental_noalias_scope_decl) {
              toErase.push_back(II);
              ++scopeDeclCount;
            }
          }
        }
        for (auto *I : toErase)
          I->eraseFromParent();
      }
    }
    if (std::getenv("CIR_DUMP_GPU_LLVM"))
      llvm::errs() << "[CIR] Stripped noalias metadata, removed "
                   << scopeDeclCount << " scope decls\n";
  }

  // Strip stacksave/stackrestore pairs that CIR's AlwaysInliner generates.
  // CIR wraps each inlined function body in stacksave/stackrestore to scope
  // dynamic allocas.  OGCG's inliner does NOT emit these for functions without
  // VLAs.  The pairs interact badly with O3: SROA and InstCombine treat
  // post-stackrestore loads from allocas created after the corresponding
  // stacksave as reading freed memory (UB), propagating poison through the
  // tile-offset computation chain and collapsing buffer-load voffsets to 0.
  {
    unsigned ssCount = 0;
    for (auto &F : module) {
      llvm::SmallVector<llvm::Instruction *, 64> toErase;
      for (auto &BB : F) {
        for (auto &I : BB) {
          if (auto *II = llvm::dyn_cast<llvm::IntrinsicInst>(&I)) {
            auto id = II->getIntrinsicID();
            if (id == llvm::Intrinsic::stacksave ||
                id == llvm::Intrinsic::stackrestore) {
              toErase.push_back(II);
              ++ssCount;
            }
          }
        }
      }
      for (auto *I : toErase) {
        if (!I->use_empty())
          I->replaceAllUsesWith(llvm::PoisonValue::get(I->getType()));
        I->eraseFromParent();
      }
    }
    if (std::getenv("CIR_DUMP_GPU_LLVM"))
      llvm::errs() << "[CIR] Removed " << ssCount
                   << " stacksave/stackrestore calls\n";
  }

  // CIR kernels access the byref kernel argument struct directly from
  // addrspace(4) and pass sub-pointers to callees via addrspacecast to
  // flat.  OGCG instead copies the struct to a local alloca (addrspace 5)
  // via memcpy, then accesses the alloca.  The alloca+memcpy pattern
  // gives SROA and IAS much better visibility into the data flow (SROA
  // can split the alloca into individual fields; loads become direct
  // SSA values from the AS4 source).  Without this, the optimizer loses
  // track of the Problem struct fields and folds buffer size computations
  // to zero, producing zero-size buffer resources (make.buffer.rsrc with
  // num_records=0) that silently discard all buffer loads/stores.
  {
    unsigned fixCount = 0;
    for (llvm::Function &F : module) {
      if (F.getCallingConv() != llvm::CallingConv::AMDGPU_KERNEL)
        continue;
      if (F.arg_size() != 1)
        continue;
      llvm::Argument *arg = F.getArg(0);
      if (!arg->hasByRefAttr())
        continue;
      llvm::Type *byrefTy = arg->getParamByRefType();
      if (!byrefTy)
        continue;
      auto &DL = module.getDataLayout();
      uint64_t structSize = DL.getTypeAllocSize(byrefTy);
      if (structSize == 0)
        continue;
      // Check if there's already a memcpy from the arg.
      llvm::MemCpyInst *existingMCI = nullptr;
      for (llvm::User *U : arg->users()) {
        if (auto *MCI = llvm::dyn_cast<llvm::MemCpyInst>(U)) {
          existingMCI = MCI;
          break;
        }
      }

      if (existingMCI) {
        // CIR lowering already generated a memcpy, but it may use the
        // wrong pattern: memcpy(AS5 alloca, AS4 arg) with all GEPs in
        // AS5.  OGCG uses memcpy(flat, AS4 arg) with all GEPs in flat
        // (AS0).  The flat pattern is critical: SROA can trace through
        // a single addrspacecast(alloca→flat) to decompose the struct,
        // but with AS5 GEPs + separate addrspacecasts for sub-structs,
        // SROA loses track of the fields and the optimizer folds
        // dimension values to zero.
        auto *destPtr = existingMCI->getDest();
        unsigned destAS =
            destPtr->getType()->getPointerAddressSpace();
        if (destAS == 0) // Already flat dest — nothing to do.
          continue;

        // Find the AS5 alloca backing the memcpy dest.
        llvm::AllocaInst *existingAlloca = nullptr;
        if (auto *AI = llvm::dyn_cast<llvm::AllocaInst>(destPtr))
          existingAlloca = AI;
        if (!existingAlloca)
          continue;

        // Rebuild the kernel entry to match OGCG's pattern:
        //   %alloca = alloca struct, addrspace(5)
        //   %flat = addrspacecast ptr addrspace(5) %alloca to ptr
        //   memcpy(ptr %flat, ptr addrspace(4) %arg, size)
        //   ... all GEPs on ptr %flat ...
        //
        // We must clone all instructions that use the alloca (GEPs,
        // loads, stores) with the flat pointer to get correct types,
        // because LLVM's opaque pointer types carry address space info.

        // 1. Insert addrspacecast after alloca.
        llvm::IRBuilder<> B(existingMCI);
        auto *flatPtr = B.CreateAddrSpaceCast(
            existingAlloca,
            llvm::PointerType::get(module.getContext(), 0));

        // 2. Create new memcpy with flat dest (correct intrinsic p0.p4).
        B.CreateMemCpy(
            flatPtr, existingAlloca->getAlign(), arg,
            arg->getParamAlign().valueOrOne(), structSize);

        // 3. Erase old memcpy (which used p5.p4).
        existingMCI->eraseFromParent();

        // 4. Replace all remaining uses of the AS5 alloca with flat,
        //    except the addrspacecast itself.  We must rebuild each
        //    user with the correct pointer type.
        llvm::SmallVector<llvm::Instruction *, 32> worklist;
        for (auto *U : existingAlloca->users()) {
          if (U == flatPtr)
            continue;
          if (auto *I = llvm::dyn_cast<llvm::Instruction>(U))
            worklist.push_back(I);
        }

        for (auto *I : worklist) {
          if (auto *GEP = llvm::dyn_cast<llvm::GetElementPtrInst>(I)) {
            // Clone the GEP with flat source.
            llvm::IRBuilder<> GB(GEP);
            llvm::SmallVector<llvm::Value *, 4> indices(
                GEP->idx_begin(), GEP->idx_end());
            auto *newGEP = GB.CreateGEP(GEP->getSourceElementType(),
                                        flatPtr, indices, "",
                                        GEP->isInBounds());
            if (auto *GEPI =
                    llvm::dyn_cast<llvm::GetElementPtrInst>(newGEP))
              GEPI->setNoWrapFlags(GEP->getNoWrapFlags());
            GEP->replaceAllUsesWith(newGEP);
            GEP->eraseFromParent();
          }
        }

        // 5. Clean up identity addrspacecasts (AS5→flat that became
        //    flat→flat after GEP source rewiring).
        llvm::SmallVector<llvm::AddrSpaceCastInst *, 16> redundant;
        for (auto &BB : F) {
          for (auto &I : BB) {
            if (auto *ASC = llvm::dyn_cast<llvm::AddrSpaceCastInst>(&I)) {
              if (ASC != flatPtr &&
                  ASC->getSrcAddressSpace() ==
                      ASC->getDestAddressSpace())
                redundant.push_back(ASC);
            }
          }
        }
        for (auto *ASC : redundant) {
          ASC->replaceAllUsesWith(ASC->getPointerOperand());
          ASC->eraseFromParent();
        }
        ++fixCount;
      } else {
        // No existing memcpy — create the full OGCG pattern.
        llvm::BasicBlock &entry = F.getEntryBlock();
        llvm::IRBuilder<> B(&*entry.getFirstNonPHIOrDbgOrAlloca());
        llvm::AllocaInst *alloca = B.CreateAlloca(byrefTy, /*AS*/ 5);
        alloca->setAlignment(arg->getParamAlign().valueOrOne());
        llvm::Value *flatAlloca = B.CreateAddrSpaceCast(
            alloca, llvm::PointerType::get(module.getContext(), 0));
        B.CreateMemCpy(flatAlloca, alloca->getAlign(), arg,
                       arg->getParamAlign().valueOrOne(), structSize);
        arg->replaceAllUsesWith(flatAlloca);
        // Fix the memcpy source: replaceAllUsesWith also replaced the
        // memcpy's source operand, so restore it to the original arg.
        for (llvm::User *U : flatAlloca->users()) {
          if (auto *MCI = llvm::dyn_cast<llvm::MemCpyInst>(U)) {
            MCI->setSource(arg);
            break;
          }
        }
        ++fixCount;
      }
    }
    if (std::getenv("CIR_DUMP_GPU_LLVM"))
      llvm::errs() << "[CIR] Added memcpy-to-alloca for " << fixCount
                   << " kernel arg structs\n";
  }

  // NOTE: Previously had a pass here that rewired AS5-alloca GEPs to
  // AS4-arg GEPs.  This was COUNTERPRODUCTIVE: it disconnected the
  // Problem sub-struct from the alloca, preventing SROA from decomposing
  // the struct fields after inlining.  OGCG keeps everything through the
  // alloca and lets SROA + IAS handle it naturally.

  // Fix CIR lowering bug: CIR stores ptr addrspace(5) (32-bit on AMDGPU)
  // into struct fields typed as ptr (64-bit).  The 4-byte store leaves the
  // upper 4 bytes undefined, so later loads read corrupted flat pointers.
  // Insert addrspacecast to flat before each such store.  Doing this pre-O3
  // gives SROA matching store/load sizes so it can promote the allocas.
  {
    auto &DL = module.getDataLayout();
    unsigned flatPtrBits = DL.getPointerSizeInBits(0);
    unsigned fixCount = 0;

    for (llvm::Function &F : module) {
      for (llvm::BasicBlock &BB : F) {
        for (auto &I : BB) {
          auto *SI = llvm::dyn_cast<llvm::StoreInst>(&I);
          if (!SI)
            continue;
          llvm::Value *val = SI->getValueOperand();
          auto *ptrTy = llvm::dyn_cast<llvm::PointerType>(val->getType());
          if (!ptrTy)
            continue;
          unsigned AS = ptrTy->getAddressSpace();
          if (AS == 0)
            continue;
          if (DL.getPointerSizeInBits(AS) >= flatPtrBits)
            continue;
          llvm::IRBuilder<> B(SI);
          llvm::Value *flat = B.CreateAddrSpaceCast(
              val, llvm::PointerType::get(module.getContext(), 0));
          SI->setOperand(0, flat);
          ++fixCount;
        }
      }
    }
    if (std::getenv("CIR_DUMP_GPU_LLVM"))
      llvm::errs() << "[CIR] Pre-O3: fixed " << fixCount
                   << " narrow-ptr stores to use flat pointers\n";
  }

  // Add a volatile inline asm barrier after each workgroup.id.{x,y,z} call.
  // CIR puts the workgroup.id value through store→load chains (sret pattern)
  // that CGSCC-interleaved GVN/SCCP + DSE/InstCombine can forward and then
  // eliminate.  OGCG returns small aggregates by value, so the value stays
  // in SSA registers and isn't subject to the same store forwarding.
  // The inline asm barrier prevents the optimizer from seeing through the
  // workgroup.id value; it compiles to a no-op (constraint "0" ties output
  // to input, producing v_mov or nothing at ISel).
  {
    const char *wgIntrinsics[] = {
        "llvm.amdgcn.workgroup.id.x",
        "llvm.amdgcn.workgroup.id.y",
        "llvm.amdgcn.workgroup.id.z",
    };
    unsigned barrierCount = 0;
    auto &ctx = module.getContext();
    llvm::Type *i32Ty = llvm::Type::getInt32Ty(ctx);
    llvm::FunctionType *asmTy =
        llvm::FunctionType::get(i32Ty, {i32Ty}, false);
    llvm::InlineAsm *ia =
        llvm::InlineAsm::get(asmTy, "", "=v,0", /*hasSideEffects=*/true);

    for (int dim = 0; dim < 3; ++dim) {
      llvm::Function *intrinsic = module.getFunction(wgIntrinsics[dim]);
      if (!intrinsic)
        continue;
      llvm::SmallVector<llvm::CallInst *, 16> calls;
      for (llvm::User *U : intrinsic->users())
        if (auto *CI = llvm::dyn_cast<llvm::CallInst>(U))
          calls.push_back(CI);
      for (llvm::CallInst *CI : calls) {
        llvm::IRBuilder<> B(CI->getNextNode());
        llvm::Value *barrier = B.CreateCall(ia, {CI});
        CI->replaceAllUsesWith(barrier);
        // Fix the barrier's operand to use the original intrinsic result.
        llvm::cast<llvm::CallInst>(barrier)->setArgOperand(0, CI);
        ++barrierCount;
      }
    }
    if (std::getenv("CIR_DUMP_GPU_LLVM"))
      llvm::errs() << "[CIR] Added " << barrierCount
                   << " inline asm barriers after workgroup.id calls\n";
  }

  if (std::getenv("CIR_DUMP_GPU_LLVM")) {
    std::error_code ec;
    llvm::raw_fd_ostream os("/tmp/cir_gpu_pre_o3.ll", ec);
    if (!ec)
      module.print(os, nullptr);
  }

  // Build the O3 pipeline with AMDGPU target-specific passes registered.
  LogicalResult result = [&]() -> LogicalResult {
    FailureOr<llvm::TargetMachine *> tm = getOrCreateTargetMachine();
    if (failed(tm))
      return ModuleToObject::optimizeModule(module, optLevel);

    (*tm)->setOptLevel(static_cast<llvm::CodeGenOptLevel>(optLevel));

    llvm::LoopAnalysisManager lam;
    llvm::FunctionAnalysisManager fam;
    llvm::CGSCCAnalysisManager cgam;
    llvm::ModuleAnalysisManager mam;

    llvm::PipelineTuningOptions pto;
    pto.LoopUnrolling = true;
    pto.LoopInterleaving = true;
    pto.LoopVectorization = true;
    pto.SLPVectorization = true;

    llvm::PassBuilder pb(*tm, pto);
    (*tm)->registerPassBuilderCallbacks(pb);

    // CIR kernels pass sub-struct pointers via addrspacecast AS5→flat.
    // After CGSCC inlining, SROA partially splits the kernel arg alloca
    // (extracting pointer fields) but can't split the Problem sub-struct
    // because its accesses go through flat GEPs.  AMDGPU's target-registered
    // IAS at ScalarOptimizerLateEP converts flat→AS5 where derivable.
    // We add SROA right after so it can split the now-AS5-accessible alloca.
    pb.registerScalarOptimizerLateEPCallback(
        [](llvm::FunctionPassManager &FPM, llvm::OptimizationLevel) {
          FPM.addPass(llvm::SROAPass(llvm::SROAOptions::ModifyCFG));
        });


    pb.registerModuleAnalyses(mam);
    pb.registerCGSCCAnalyses(cgam);
    pb.registerFunctionAnalyses(fam);
    pb.registerLoopAnalyses(lam);
    pb.crossRegisterProxies(lam, fam, cgam, mam);

    llvm::OptimizationLevel ol = llvm::OptimizationLevel::O3;
    switch (optLevel) {
    case 0: ol = llvm::OptimizationLevel::O0; break;
    case 1: ol = llvm::OptimizationLevel::O1; break;
    case 2: ol = llvm::OptimizationLevel::O2; break;
    default: break;
    }

    llvm::ModulePassManager mpm;
    mpm.addPass(pb.buildPerModuleDefaultPipeline(ol));
    mpm.run(module, mam);

    // AMDGPUAttributor may incorrectly infer amdgpu-no-workgroup-id-*
    // and amdgpu-no-workitem-id-* when the use chain is complex (e.g.
    // after heavy inlining from CIR).  Strip these so the backend
    // conservatively preloads all implicit arguments for kernels.
    for (llvm::Function &F : module) {
      if (F.getCallingConv() != llvm::CallingConv::AMDGPU_KERNEL)
        continue;
      F.removeFnAttr("amdgpu-no-workgroup-id-x");
      F.removeFnAttr("amdgpu-no-workgroup-id-y");
      F.removeFnAttr("amdgpu-no-workgroup-id-z");
      F.removeFnAttr("amdgpu-no-workitem-id-x");
      F.removeFnAttr("amdgpu-no-workitem-id-y");
      F.removeFnAttr("amdgpu-no-workitem-id-z");
      F.removeFnAttr("amdgpu-no-dispatch-ptr");
      F.removeFnAttr("amdgpu-no-implicitarg-ptr");
    }

    // Fix type mismatch: CIR stores ptr addrspace(5) (32-bit) values into
    // fields typed as ptr (64-bit).  The 4-byte store leaves the upper 4
    // bytes of the 8-byte slot undefined, so a subsequent `load ptr` reads
    // a corrupted flat pointer → GPU hang.  Expand each such store by
    // addrspacecasting the value to flat before storing.
    {
      auto &DL = module.getDataLayout();
      unsigned flatPtrBits = DL.getPointerSizeInBits(0);
      unsigned fixCount = 0;

      for (llvm::Function &F : module) {
        for (llvm::BasicBlock &BB : F) {
          for (auto &I : BB) {
            auto *SI = llvm::dyn_cast<llvm::StoreInst>(&I);
            if (!SI)
              continue;
            llvm::Value *val = SI->getValueOperand();
            auto *ptrTy = llvm::dyn_cast<llvm::PointerType>(val->getType());
            if (!ptrTy)
              continue;
            unsigned AS = ptrTy->getAddressSpace();
            if (AS == 0)
              continue;
            if (DL.getPointerSizeInBits(AS) >= flatPtrBits)
              continue;
            llvm::IRBuilder<> B(SI);
            llvm::Value *flat = B.CreateAddrSpaceCast(
                val, llvm::PointerType::get(module.getContext(), 0));
            SI->setOperand(0, flat);
            ++fixCount;
          }
        }
      }
      if (std::getenv("CIR_DUMP_GPU_LLVM"))
        llvm::errs() << "[CIR] Fixed " << fixCount
                     << " narrow-ptr stores to use flat pointers\n";
    }

    return success();
  }();

  if (std::getenv("CIR_DUMP_GPU_LLVM")) {
    std::error_code ec;
    llvm::raw_fd_ostream os("/tmp/cir_gpu_post_opt.ll", ec);
    if (!ec)
      module.print(os, nullptr);
  }

  if (adjustedThreshold)
    slpOpt->reset();
  return result;
}

void SerializeGPUModuleBase::addControlVariables(
    llvm::Module &module, AMDGCNLibraries libs, bool wave64, bool daz,
    bool finiteOnly, bool unsafeMath, bool fastMath, bool correctSqrt,
    StringRef abiVer) {
  // Helper function for adding control variables.
  auto addControlVariable = [&module](StringRef name, uint32_t value,
                                      uint32_t bitwidth) {
    if (module.getNamedGlobal(name))
      return;
    llvm::IntegerType *type =
        llvm::IntegerType::getIntNTy(module.getContext(), bitwidth);
    llvm::GlobalVariable *controlVariable = new llvm::GlobalVariable(
        module, /*isConstant=*/type, true,
        llvm::GlobalValue::LinkageTypes::LinkOnceODRLinkage,
        llvm::ConstantInt::get(type, value), name, /*before=*/nullptr,
        /*threadLocalMode=*/llvm::GlobalValue::ThreadLocalMode::NotThreadLocal,
        /*addressSpace=*/4);
    controlVariable->setVisibility(
        llvm::GlobalValue::VisibilityTypes::ProtectedVisibility);
    controlVariable->setAlignment(llvm::MaybeAlign(bitwidth / 8));
    controlVariable->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Local);
  };

  // Note that COV6 requires ROCm 6.3+.
  int abi = 600;
  abiVer.getAsInteger(0, abi);
  module.addModuleFlag(llvm::Module::Error, "amdhsa_code_object_version", abi);
  // Return if no device libraries are required.
  if (libs == AMDGCNLibraries::None)
    return;
  // Add ocml related control variables.
  if (any(libs & AMDGCNLibraries::Ocml)) {
    addControlVariable("__oclc_finite_only_opt", finiteOnly || fastMath, 8);
    addControlVariable("__oclc_daz_opt", daz || fastMath, 8);
    addControlVariable("__oclc_correctly_rounded_sqrt32",
                       correctSqrt && !fastMath, 8);
    addControlVariable("__oclc_unsafe_math_opt", unsafeMath || fastMath, 8);
  }
  // Add ocml or ockl related control variables.
  if (any(libs & (AMDGCNLibraries::Ocml | AMDGCNLibraries::Ockl))) {
    addControlVariable("__oclc_wavefrontsize64", wave64, 8);
    // Get the ISA version.
    llvm::AMDGPU::IsaVersion isaVersion = llvm::AMDGPU::getIsaVersion(chip);
    // Add the ISA control variable.
    addControlVariable("__oclc_ISA_version",
                       isaVersion.Minor + 100 * isaVersion.Stepping +
                           1000 * isaVersion.Major,
                       32);
    addControlVariable("__oclc_ABI_version", abi, 32);
  }
}

FailureOr<SmallVector<char, 0>>
mlir::ROCDL::assembleIsa(StringRef isa, StringRef targetTriple, StringRef chip,
                         StringRef features,
                         function_ref<InFlightDiagnostic()> emitError) {
  SmallVector<char, 0> result;
  llvm::raw_svector_ostream os(result);

  llvm::Triple triple(llvm::Triple::normalize(targetTriple));
  std::string error;
  const llvm::Target *target =
      llvm::TargetRegistry::lookupTarget(triple, error);
  if (!target)
    return emitError() << "failed to lookup target: " << error;

  llvm::SourceMgr srcMgr;
  // Copy buffer to ensure it's null terminated.
  srcMgr.AddNewSourceBuffer(llvm::MemoryBuffer::getMemBufferCopy(isa), SMLoc());

  const llvm::MCTargetOptions mcOptions;
  std::unique_ptr<llvm::MCRegisterInfo> mri(target->createMCRegInfo(triple));
  std::unique_ptr<llvm::MCAsmInfo> mai(
      target->createMCAsmInfo(*mri, triple, mcOptions));
  std::unique_ptr<llvm::MCSubtargetInfo> sti(
      target->createMCSubtargetInfo(triple, chip, features));

  llvm::MCContext ctx(triple, *mai, *mri, *sti, &srcMgr);
  std::unique_ptr<llvm::MCObjectFileInfo> mofi(target->createMCObjectFileInfo(
      ctx, /*PIC=*/false, /*LargeCodeModel=*/false));
  ctx.setObjectFileInfo(mofi.get());

  SmallString<128> cwd;
  if (!llvm::sys::fs::current_path(cwd))
    ctx.setCompilationDir(cwd);

  std::unique_ptr<llvm::MCStreamer> mcStreamer;
  std::unique_ptr<llvm::MCInstrInfo> mcii(target->createMCInstrInfo());

  llvm::MCCodeEmitter *ce = target->createMCCodeEmitter(*mcii, ctx);
  llvm::MCAsmBackend *mab = target->createMCAsmBackend(*sti, *mri, mcOptions);
  mcStreamer.reset(target->createMCObjectStreamer(
      triple, ctx, std::unique_ptr<llvm::MCAsmBackend>(mab),
      mab->createObjectWriter(os), std::unique_ptr<llvm::MCCodeEmitter>(ce),
      *sti));

  std::unique_ptr<llvm::MCAsmParser> parser(
      createMCAsmParser(srcMgr, ctx, *mcStreamer, *mai));
  std::unique_ptr<llvm::MCTargetAsmParser> tap(
      target->createMCAsmParser(*sti, *parser, *mcii));

  if (!tap)
    return emitError() << "assembler initialization error";

  parser->setTargetParser(*tap);
  parser->Run(false);
  return std::move(result);
}

FailureOr<SmallVector<char, 0>>
mlir::ROCDL::linkObjectCode(ArrayRef<char> objectCode, StringRef lldPath,
                            function_ref<InFlightDiagnostic()> emitError) {
  // Save the ISA binary to a temp file.
  int tempIsaBinaryFd = -1;
  SmallString<128> tempIsaBinaryFilename;
  if (llvm::sys::fs::createTemporaryFile("kernel%%", "o", tempIsaBinaryFd,
                                         tempIsaBinaryFilename))
    return emitError()
           << "failed to create a temporary file for dumping the ISA binary";

  llvm::FileRemover cleanupIsaBinary(tempIsaBinaryFilename);
  {
    llvm::raw_fd_ostream tempIsaBinaryOs(tempIsaBinaryFd, true);
    tempIsaBinaryOs << StringRef(objectCode.data(), objectCode.size());
    tempIsaBinaryOs.flush();
  }

  // Create a temp file for HSA code object.
  SmallString<128> tempHsacoFilename;
  if (llvm::sys::fs::createTemporaryFile("kernel", "hsaco", tempHsacoFilename))
    return emitError()
           << "failed to create a temporary file for the HSA code object";

  llvm::FileRemover cleanupHsaco(tempHsacoFilename);

  int lldResult = llvm::sys::ExecuteAndWait(
      lldPath,
      {"ld.lld", "-shared", tempIsaBinaryFilename, "-o", tempHsacoFilename});
  if (lldResult != 0)
    return emitError() << "lld invocation failed";

  // Load the HSA code object.
  auto hsacoFile =
      llvm::MemoryBuffer::getFile(tempHsacoFilename, /*IsText=*/false);
  if (!hsacoFile)
    return emitError()
           << "failed to read the HSA code object from the temp file";

  StringRef buffer = (*hsacoFile)->getBuffer();

  return SmallVector<char, 0>(buffer.begin(), buffer.end());
}

FailureOr<SmallVector<char, 0>>
SerializeGPUModuleBase::compileToBinary(StringRef serializedISA) {
  auto errCallback = [&]() { return getOperation().emitError(); };
  // Assemble the ISA.
  FailureOr<SmallVector<char, 0>> isaBinary = ROCDL::assembleIsa(
      serializedISA, this->triple, this->chip, this->features, errCallback);

  if (failed(isaBinary))
    return failure();

  // Link the object code.
  llvm::SmallString<128> lldPath(toolkitPath);
  llvm::sys::path::append(lldPath, "llvm", "bin", "ld.lld");
  FailureOr<SmallVector<char, 0>> linkedCode =
      ROCDL::linkObjectCode(*isaBinary, lldPath, errCallback);
  if (failed(linkedCode))
    return failure();

  return linkedCode;
}

FailureOr<SmallVector<char, 0>> SerializeGPUModuleBase::moduleToObjectImpl(
    const gpu::TargetOptions &targetOptions, llvm::Module &llvmModule) {
  // Return LLVM IR if the compilation target is offload.
#define DEBUG_TYPE "serialize-to-llvm"
  LLVM_DEBUG({
    llvm::dbgs() << "LLVM IR for module: "
                 << cast<gpu::GPUModuleOp>(getOperation()).getNameAttr() << "\n"
                 << llvmModule << "\n";
  });
#undef DEBUG_TYPE
  if (targetOptions.getCompilationTarget() == gpu::CompilationTarget::Offload)
    return SerializeGPUModuleBase::moduleToObject(llvmModule);

  FailureOr<llvm::TargetMachine *> targetMachine = getOrCreateTargetMachine();
  if (failed(targetMachine))
    return getOperation().emitError()
           << "target Machine unavailable for triple " << triple
           << ", can't compile with LLVM";

  // Translate the Module to ISA.
  FailureOr<SmallString<0>> serializedISA =
      translateModuleToISA(llvmModule, **targetMachine,
                           [&]() { return getOperation().emitError(); });
  if (failed(serializedISA))
    return getOperation().emitError() << "failed translating the module to ISA";

#define DEBUG_TYPE "serialize-to-isa"
  LLVM_DEBUG({
    llvm::dbgs() << "ISA for module: "
                 << cast<gpu::GPUModuleOp>(getOperation()).getNameAttr() << "\n"
                 << *serializedISA << "\n";
  });
#undef DEBUG_TYPE
  // Return ISA assembly code if the compilation target is assembly.
  if (targetOptions.getCompilationTarget() == gpu::CompilationTarget::Assembly)
    return SmallVector<char, 0>(serializedISA->begin(), serializedISA->end());

  // Compiling to binary requires a valid ROCm path, fail if it's not found.
  if (getToolkitPath().empty())
    return getOperation().emitError()
           << "invalid ROCm path, please set a valid path";

  // Compile to binary.
  return compileToBinary(*serializedISA);
}

#if LLVM_HAS_AMDGPU_TARGET
namespace {
class AMDGPUSerializer : public SerializeGPUModuleBase {
public:
  AMDGPUSerializer(Operation &module, ROCDLTargetAttr target,
                   const gpu::TargetOptions &targetOptions);

  FailureOr<SmallVector<char, 0>>
  moduleToObject(llvm::Module &llvmModule) override;

private:
  // Target options.
  gpu::TargetOptions targetOptions;
};
} // namespace

AMDGPUSerializer::AMDGPUSerializer(Operation &module, ROCDLTargetAttr target,
                                   const gpu::TargetOptions &targetOptions)
    : SerializeGPUModuleBase(module, target, targetOptions),
      targetOptions(targetOptions) {}

FailureOr<SmallVector<char, 0>>
AMDGPUSerializer::moduleToObject(llvm::Module &llvmModule) {
  return moduleToObjectImpl(targetOptions, llvmModule);
}
#endif // LLVM_HAS_AMDGPU_TARGET

std::optional<mlir::gpu::SerializedObject>
ROCDLTargetAttrImpl::serializeToObject(
    Attribute attribute, Operation *module,
    const gpu::TargetOptions &options) const {
  assert(module && "The module must be non null.");
  if (!module)
    return std::nullopt;
  if (!mlir::isa<gpu::GPUModuleOp>(module)) {
    module->emitError("module must be a GPU module");
    return std::nullopt;
  }
#if LLVM_HAS_AMDGPU_TARGET
  AMDGPUSerializer serializer(*module, cast<ROCDLTargetAttr>(attribute),
                              options);
  serializer.init();
  std::optional<SmallVector<char, 0>> binary = serializer.run();
  if (!binary)
    return std::nullopt;
  return gpu::SerializedObject{std::move(*binary)};
#else
  module->emitError("the `AMDGPU` target was not built. Please enable it when "
                    "building LLVM");
  return std::nullopt;
#endif // LLVM_HAS_AMDGPU_TARGET
}

Attribute
ROCDLTargetAttrImpl::createObject(Attribute attribute, Operation *module,
                                  const mlir::gpu::SerializedObject &object,
                                  const gpu::TargetOptions &options) const {
  gpu::CompilationTarget format = options.getCompilationTarget();
  // If format is `fatbin` transform it to binary as `fatbin` is not yet
  // supported.
  gpu::KernelTableAttr kernels;
  if (format > gpu::CompilationTarget::Binary) {
    format = gpu::CompilationTarget::Binary;
    kernels = ROCDL::getKernelMetadata(module, object.getObject());
  }
  DictionaryAttr properties{};
  Builder builder(attribute.getContext());
  StringAttr objectStr = builder.getStringAttr(
      StringRef(object.getObject().data(), object.getObject().size()));
  return builder.getAttr<gpu::ObjectAttr>(attribute, format, objectStr,
                                          properties, kernels);
}
