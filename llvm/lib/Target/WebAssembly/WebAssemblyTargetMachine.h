// WebAssemblyTargetMachine.h - Define TargetMachine for WebAssembly -*- C++ -*-
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the WebAssembly-specific subclass of
/// TargetMachine.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYTARGETMACHINE_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYTARGETMACHINE_H

#include "WebAssemblySubtarget.h"
#include "llvm/CodeGen/CodeGenTargetMachineImpl.h"
#include <optional>

namespace llvm {

class Function;
namespace clv2 {
class OptionsContext;
}

namespace WebAssembly {
// Exception handling / setjmp-longjmp handling option getters
LLVM_ABI bool getWasmEnableEmEH(const Function *F = nullptr);
LLVM_ABI bool getWasmEnableEmSjLj(const Function *F = nullptr);
LLVM_ABI bool getWasmEnableEH(const Function *F = nullptr);
LLVM_ABI bool getWasmEnableSjLj(const Function *F = nullptr);
LLVM_ABI bool getWasmUseLegacyEH(const Function *F = nullptr);
LLVM_ABI bool getWasmEnableEmEH(const clv2::OptionsContext &Ctx);
LLVM_ABI bool getWasmEnableEmSjLj(const clv2::OptionsContext &Ctx);
LLVM_ABI bool getWasmEnableEH(const clv2::OptionsContext &Ctx);
LLVM_ABI bool getWasmEnableSjLj(const clv2::OptionsContext &Ctx);
LLVM_ABI bool getWasmUseLegacyEH(const clv2::OptionsContext &Ctx);
} // namespace WebAssembly

class WebAssemblyTargetMachine final : public CodeGenTargetMachineImpl {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  mutable StringMap<std::unique_ptr<WebAssemblySubtarget>> SubtargetMap;
  bool UsesMultivalueABI = false;

public:
  WebAssemblyTargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                           StringRef FS, const TargetOptions &Options,
                           std::optional<Reloc::Model> RM,
                           std::optional<CodeModel::Model> CM,
                           CodeGenOptLevel OL, bool JIT);

  ~WebAssemblyTargetMachine() override;

  const WebAssemblySubtarget *getSubtargetImpl() const;
  const WebAssemblySubtarget *getSubtargetImpl(std::string CPU,
                                               std::string FS) const;
  const WebAssemblySubtarget *
  getSubtargetImpl(const Function &F) const override;

  // Pass Pipeline Configuration
  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;

  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }

  MachineFunctionInfo *
  createMachineFunctionInfo(BumpPtrAllocator &Allocator, const Function &F,
                            const TargetSubtargetInfo *STI) const override;

  TargetTransformInfo getTargetTransformInfo(const Function &F) const override;

  bool usesPhysRegsForValues() const override { return false; }

  yaml::MachineFunctionInfo *createDefaultFuncInfoYAML() const override;
  yaml::MachineFunctionInfo *
  convertFuncInfoToYAML(const MachineFunction &MF) const override;
  bool parseMachineFunctionInfo(const yaml::MachineFunctionInfo &,
                                PerFunctionMIParsingState &PFS,
                                SMDiagnostic &Error,
                                SMRange &SourceRange) const override;

  bool usesMultivalueABI() const { return UsesMultivalueABI; }

  void registerPassBuilderCallbacks(PassBuilder &PbB) override;

  Error buildCodeGenPipeline(ModulePassManager &MPM, ModuleAnalysisManager &MAM,
                             raw_pwrite_stream &Out, raw_pwrite_stream *DwoOut,
                             CodeGenFileType FileType,
                             const CGPassBuilderOption &Opt, MCContext &Ctx,
                             PassInstrumentationCallbacks *PIC) override;
};

} // end namespace llvm

#endif
