//===----- HexagonLoopAlign.cpp - Generate loop alignment directives  -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Inspect a basic block and if its single basic block loop with a small
// number of instructions, set the prefLoopAlignment to 32 bytes (5).
//===----------------------------------------------------------------------===//

#include "Hexagon.h"
#include "HexagonTargetMachine.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Target/Hexagon/HexagonOptionsOptInfos.h"

#define DEBUG_TYPE "hexagon-loop-align"

using namespace llvm;

static unsigned getLoopEdgeThreshold(const Function &F) {
  return clv2::getOptValOrDefault<&clv2::HEX_LoopEdgeThreshold>(
      F.getContext().getOptionsContext());
}

static bool getDisableLoopAlign(const Function &F) {
  return clv2::getOptValOrDefault<&clv2::HEX_DisableLoopAlign>(
      F.getContext().getOptionsContext());
}

static unsigned getHVXLoopAlignLimitUB(const Function &F) {
  return clv2::getOptValOrDefault<&clv2::HEX_HVXLoopAlignLimitUB>(
      F.getContext().getOptionsContext());
}

static unsigned getTinyLoopAlignLimitUB(const Function &F) {
  return clv2::getOptValOrDefault<&clv2::HEX_TinyLoopAlignLimitUB>(
      F.getContext().getOptionsContext());
}

static unsigned getLoopAlignLimitUB(const Function &F) {
  return clv2::getOptValOrDefault<&clv2::HEX_LoopAlignLimitUB>(
      F.getContext().getOptionsContext());
}

static unsigned getLoopAlignLimitLB(const Function &F) {
  return clv2::getOptValOrDefault<&clv2::HEX_LoopAlignLimitLB>(
      F.getContext().getOptionsContext());
}

static unsigned getLoopBndlAlignLimit(const Function &F) {
  return clv2::getOptValOrDefault<&clv2::HEX_LoopBndlAlignLimit>(
      F.getContext().getOptionsContext());
}

static unsigned getTinyLoopBndlAlignLimit(const Function &F) {
  return clv2::getOptValOrDefault<&clv2::HEX_TinyLoopBndlAlignLimit>(
      F.getContext().getOptionsContext());
}

namespace {

class HexagonLoopAlign : public MachineFunctionPass {
  const HexagonSubtarget *HST = nullptr;
  const TargetMachine *HTM = nullptr;
  const HexagonInstrInfo *HII = nullptr;

public:
  static char ID;
  HexagonLoopAlign() : MachineFunctionPass(ID) {}
  bool shouldBalignLoop(MachineBasicBlock &BB, bool AboveThres);
  bool isSingleLoop(MachineBasicBlock &MBB);
  bool attemptToBalignSmallLoop(MachineFunction &MF, MachineBasicBlock &MBB);

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineBranchProbabilityInfoWrapperPass>();
    AU.addRequired<MachineBlockFrequencyInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  StringRef getPassName() const override { return "Hexagon LoopAlign pass"; }
  bool runOnMachineFunction(MachineFunction &MF) override;
};

char HexagonLoopAlign::ID = 0;

bool HexagonLoopAlign::shouldBalignLoop(MachineBasicBlock &BB,
                                        bool AboveThres) {
  bool isVec = false;
  unsigned InstCnt = 0;
  unsigned BndlCnt = 0;

  for (MachineBasicBlock::instr_iterator II = BB.instr_begin(),
                                         IE = BB.instr_end();
       II != IE; ++II) {

    // End if the instruction is endloop.
    if (HII->isEndLoopN(II->getOpcode()))
      break;
    // Count the number of bundles.
    if (II->isBundle()) {
      BndlCnt++;
      continue;
    }
    // Skip over debug instructions.
    if (II->isDebugInstr())
      continue;
    // Check if there are any HVX instructions in loop.
    isVec |= HII->isHVXVec(*II);
    // Count the number of instructions.
    InstCnt++;
  }

  LLVM_DEBUG({
    dbgs() << "Bundle Count : " << BndlCnt << "\n";
    dbgs() << "Instruction Count : " << InstCnt << "\n";
  });

  const Function &F = BB.getParent()->getFunction();
  unsigned LimitUB = 0;
  unsigned LimitBndl = getLoopBndlAlignLimit(F);
  // The conditions in the order of priority.
  if (HST->isTinyCore()) {
    LimitUB = getTinyLoopAlignLimitUB(F);
    LimitBndl = getTinyLoopBndlAlignLimit(F);
  } else if (isVec)
    LimitUB = getHVXLoopAlignLimitUB(F);
  else if (AboveThres)
    LimitUB = getLoopAlignLimitUB(F);

  // if the upper bound is not set to a value, implies we didn't meet
  // the criteria.
  if (LimitUB == 0)
    return false;

  return InstCnt >= getLoopAlignLimitLB(F) && InstCnt <= LimitUB &&
         BndlCnt <= LimitBndl;
}

bool HexagonLoopAlign::isSingleLoop(MachineBasicBlock &MBB) {
  int Succs = MBB.succ_size();
  return (MBB.isSuccessor(&MBB) && (Succs == 2));
}

bool HexagonLoopAlign::attemptToBalignSmallLoop(MachineFunction &MF,
                                                MachineBasicBlock &MBB) {
  if (!isSingleLoop(MBB))
    return false;

  const MachineBranchProbabilityInfo *MBPI =
      &getAnalysis<MachineBranchProbabilityInfoWrapperPass>().getMBPI();
  const MachineBlockFrequencyInfo *MBFI =
      &getAnalysis<MachineBlockFrequencyInfoWrapperPass>().getMBFI();

  // Compute frequency of back edge,
  BlockFrequency BlockFreq = MBFI->getBlockFreq(&MBB);
  BranchProbability BrProb = MBPI->getEdgeProbability(&MBB, &MBB);
  BlockFrequency EdgeFreq = BlockFreq * BrProb;
  LLVM_DEBUG({
    dbgs() << "Loop Align Pass:\n";
    dbgs() << "\tedge with freq(" << EdgeFreq.getFrequency() << ")\n";
  });

  bool AboveThres =
      EdgeFreq.getFrequency() > getLoopEdgeThreshold(MF.getFunction());

  if (shouldBalignLoop(MBB, AboveThres)) {
    // We found a loop, change its alignment to be 32 (5).
    MBB.setAlignment(llvm::Align(1 << 5));
    return true;
  }
  return false;
}

// Inspect each basic block, and if its a single BB loop, see if it
// meets the criteria for increasing alignment to 32.

bool HexagonLoopAlign::runOnMachineFunction(MachineFunction &MF) {

  HST = &MF.getSubtarget<HexagonSubtarget>();
  HII = HST->getInstrInfo();
  HTM = &MF.getTarget();

  if (skipFunction(MF.getFunction()))
    return false;
  if (getDisableLoopAlign(MF.getFunction()))
    return false;

  // This optimization is performed at
  // i) -O2 and above, and  when the loop has a HVX instruction.
  // ii) -O3
  if (HST->useHVXOps()) {
    if (HTM->getOptLevel() < CodeGenOptLevel::Default)
      return false;
  } else {
    if (HTM->getOptLevel() < CodeGenOptLevel::Aggressive)
      return false;
  }

  bool Changed = false;
  for (MachineFunction::iterator MBBi = MF.begin(), MBBe = MF.end();
       MBBi != MBBe; ++MBBi) {
    MachineBasicBlock &MBB = *MBBi;
    Changed |= attemptToBalignSmallLoop(MF, MBB);
  }
  return Changed;
}

} // namespace

INITIALIZE_PASS(HexagonLoopAlign, "hexagon-loop-align",
                "Hexagon LoopAlign pass", false, false)

//===----------------------------------------------------------------------===//
//                         Public Constructor Functions
//===----------------------------------------------------------------------===//

FunctionPass *llvm::createHexagonLoopAlign() { return new HexagonLoopAlign(); }
