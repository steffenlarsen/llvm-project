//===- MachineBranchProbabilityInfo.cpp - Machine Branch Probability Info -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This analysis uses probability info stored in Machine Basic Blocks.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/CodeGenPassOptionsOptInfos.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

INITIALIZE_PASS(MachineBranchProbabilityInfoWrapperPass, "machine-branch-prob",
                "Machine Branch Probability Analysis", false, true)

static unsigned getStaticLikelyProb(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOrDefault<&clv2::CGPASS_StaticLikelyProb>(Ctx);
}

MachineBranchProbabilityAnalysis::Result
MachineBranchProbabilityAnalysis::run(MachineFunction &,
                                      MachineFunctionAnalysisManager &) {
  return MachineBranchProbabilityInfo();
}

PreservedAnalyses
MachineBranchProbabilityPrinterPass::run(MachineFunction &MF,
                                         MachineFunctionAnalysisManager &MFAM) {
  OS << "Printing analysis 'Machine Branch Probability Analysis' for machine "
        "function '"
     << MF.getName() << "':\n";
  auto &MBPI = MFAM.getResult<MachineBranchProbabilityAnalysis>(MF);
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineBasicBlock *Succ : MBB.successors())
      MBPI.printEdgeProbability(OS << "  ", &MBB, Succ);
  }
  return PreservedAnalyses::all();
}

char MachineBranchProbabilityInfoWrapperPass::ID = 0;

MachineBranchProbabilityInfoWrapperPass::
    MachineBranchProbabilityInfoWrapperPass()
    : ImmutablePass(ID) {}

void MachineBranchProbabilityInfoWrapperPass::anchor() {}

AnalysisKey MachineBranchProbabilityAnalysis::Key;

bool MachineBranchProbabilityInfo::invalidate(
    MachineFunction &, const PreservedAnalyses &PA,
    MachineFunctionAnalysisManager::Invalidator &) {
  auto PAC = PA.getChecker<MachineBranchProbabilityAnalysis>();
  return !PAC.preservedWhenStateless();
}

BranchProbability
MachineBranchProbabilityInfo::getEdgeProbability(const MachineBasicBlock *Src,
                                                 unsigned SuccIdx) const {
  return Src->getSuccProbability(Src->succ_begin() + SuccIdx);
}

BranchProbability MachineBranchProbabilityInfo::getEdgeProbability(
    const MachineBasicBlock *Src, const MachineBasicBlock *Dst) const {
  // This is a linear search. Try to use the const_succ_iterator version when
  // possible.
  return Src->getSuccProbability(find(Src->successors(), Dst));
}

bool MachineBranchProbabilityInfo::isEdgeHot(
    const MachineBasicBlock *Src, const MachineBasicBlock *Dst) const {
  BranchProbability HotProb(
      getStaticLikelyProb(
          Src->getParent()->getFunction().getContext().getOptionsContext()),
      100);
  return getEdgeProbability(Src, Dst) > HotProb;
}

raw_ostream &MachineBranchProbabilityInfo::printEdgeProbability(
    raw_ostream &OS, const MachineBasicBlock *Src,
    const MachineBasicBlock *Dst) const {

  const BranchProbability Prob = getEdgeProbability(Src, Dst);
  OS << "edge " << printMBBReference(*Src) << " -> " << printMBBReference(*Dst)
     << " probability is " << Prob
     << (isEdgeHot(Src, Dst) ? " [HOT edge]\n" : "\n");

  return OS;
}
