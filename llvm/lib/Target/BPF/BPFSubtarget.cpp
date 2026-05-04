//===-- BPFSubtarget.cpp - BPF Subtarget Information ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the BPF specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "BPFSubtarget.h"
#include "BPF.h"
#include "BPFTargetMachine.h"
#include "GISel/BPFCallLowering.h"
#include "GISel/BPFLegalizerInfo.h"
#include "GISel/BPFRegisterBankInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/TargetParser/Host.h"

using namespace llvm;

#define DEBUG_TYPE "bpf-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "BPFGenSubtargetInfo.inc"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Target/BPF/BPFOptionsOptInfos.h"

void BPFSubtarget::anchor() {}

BPFSubtarget &
BPFSubtarget::initializeSubtargetDependencies(StringRef CPU, StringRef FS,
                                              const TargetMachine &TM) {
  initializeEnvironment();
  initSubtargetFeatures(CPU, FS, TM);
  ParseSubtargetFeatures(CPU, /*TuneCPU*/ CPU, FS);
  return *this;
}

void BPFSubtarget::initializeEnvironment() {
  HasJmpExt = false;
  HasJmp32 = false;
  HasAlu32 = false;
  UseDwarfRIS = false;
  HasLdsx = false;
  HasMovsx = false;
  HasBswap = false;
  HasSdivSmod = false;
  HasGotol = false;
  HasStoreImm = false;
  HasLoadAcqStoreRel = false;
  HasGotox = false;
  AllowsMisalignedMemAccess = false;
}

void BPFSubtarget::initSubtargetFeatures(StringRef CPU, StringRef FS,
                                         const TargetMachine &TM) {
  if (CPU.empty())
    CPU = "v3";
  if (CPU == "probe")
    CPU = sys::detail::getHostCPUNameForBPF();
  if (CPU == "generic" || CPU == "v1")
    return;
  if (CPU == "v2") {
    HasJmpExt = true;
    return;
  }
  if (CPU == "v3") {
    HasJmpExt = true;
    HasJmp32 = true;
    HasAlu32 = true;
    return;
  }
  if (CPU == "v4") {
    HasJmpExt = true;
    HasJmp32 = true;
    HasAlu32 = true;
    const bpf_opts::ParsedOpts *O =
        clv2::getView<&clv2::BPFOptsReg>(TM.getOptionsContext());
    HasLdsx = !(O ? O->get<&clv2::BPF_DisableLdsx>() : false);
    HasMovsx = !(O ? O->get<&clv2::BPF_DisableMovsx>() : false);
    HasBswap = !(O ? O->get<&clv2::BPF_DisableBswap>() : false);
    HasSdivSmod = !(O ? O->get<&clv2::BPF_DisableSdivSmod>() : false);
    HasGotol = !(O ? O->get<&clv2::BPF_DisableGotol>() : false);
    HasStoreImm = !(O ? O->get<&clv2::BPF_DisableStoreImm>() : false);
    HasLoadAcqStoreRel =
        !(O ? O->get<&clv2::BPF_DisableLoadAcqStoreRel>() : false);
    HasGotox = !(O ? O->get<&clv2::BPF_DisableGotox>() : false);
    return;
  }
}

BPFSubtarget::BPFSubtarget(const Triple &TT, const std::string &CPU,
                           const std::string &FS, const TargetMachine &TM)
    : BPFGenSubtargetInfo(TT, CPU, /*TuneCPU*/ CPU, FS, TM.getOptionsContext()),
      InstrInfo(initializeSubtargetDependencies(CPU, FS, TM)),
      FrameLowering(*this), TLInfo(TM, *this) {
  setOptionsContext(TM.getOptionsContext());
  IsLittleEndian = TT.isLittleEndian();

  CallLoweringInfo.reset(new BPFCallLowering(*getTargetLowering()));
  Legalizer.reset(new BPFLegalizerInfo(*this));
  auto *RBI = new BPFRegisterBankInfo(*getRegisterInfo());
  RegBankInfo.reset(RBI);

  InstSelector.reset(createBPFInstructionSelector(
      *static_cast<const BPFTargetMachine *>(&TM), *this, *RBI));
}

const CallLowering *BPFSubtarget::getCallLowering() const {
  return CallLoweringInfo.get();
}

InstructionSelector *BPFSubtarget::getInstructionSelector() const {
  return InstSelector.get();
}

const LegalizerInfo *BPFSubtarget::getLegalizerInfo() const {
  return Legalizer.get();
}

const RegisterBankInfo *BPFSubtarget::getRegBankInfo() const {
  return RegBankInfo.get();
}
