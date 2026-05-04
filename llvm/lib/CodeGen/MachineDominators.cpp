//===- MachineDominators.cpp - Machine Dominator Calculation --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements simple dominator construction algorithms for finding
// forward dominators on machine functions.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/CodeGenPassOptionsOptInfos.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/Function.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/GenericDomTreeConstruction.h"
#include "llvm/Support/OptionsContext.h"

using namespace llvm;

static bool getVerifyMachineDomInfo(const clv2::OptionsContext &Ctx) {
  if (auto *O = clv2::getView<&clv2::CGPassMachine1Reg>(Ctx))
    return O->get<&clv2::CGPASS_VerifyMachineDomInfo>();
#ifdef EXPENSIVE_CHECKS
  return true;
#else
  return false;
#endif
}

namespace llvm {
template class LLVM_EXPORT_TEMPLATE DomTreeNodeBase<MachineBasicBlock>;
template class LLVM_EXPORT_TEMPLATE
    DominatorTreeBase<MachineBasicBlock, false>; // DomTreeBase

namespace DomTreeBuilder {
template LLVM_EXPORT_TEMPLATE void Calculate<MBBDomTree>(MBBDomTree &DT);
template LLVM_EXPORT_TEMPLATE void
CalculateWithUpdates<MBBDomTree>(MBBDomTree &DT, MBBUpdates U);

template LLVM_EXPORT_TEMPLATE void
InsertEdge<MBBDomTree>(MBBDomTree &DT, MachineBasicBlock *From,
                       MachineBasicBlock *To);

template LLVM_EXPORT_TEMPLATE void
DeleteEdge<MBBDomTree>(MBBDomTree &DT, MachineBasicBlock *From,
                       MachineBasicBlock *To);

template LLVM_EXPORT_TEMPLATE void
ApplyUpdates<MBBDomTree>(MBBDomTree &DT, MBBDomTreeGraphDiff &,
                         MBBDomTreeGraphDiff *);

template LLVM_EXPORT_TEMPLATE bool
Verify<MBBDomTree>(const MBBDomTree &DT, MBBDomTree::VerificationLevel VL);
} // namespace DomTreeBuilder
}

bool MachineDominatorTree::invalidate(
    MachineFunction &, const PreservedAnalyses &PA,
    MachineFunctionAnalysisManager::Invalidator &) {
  // Check whether the analysis, all analyses on machine functions, or the
  // machine function's CFG have been preserved.
  auto PAC = PA.getChecker<MachineDominatorTreeAnalysis>();
  return !PAC.preserved() &&
         !PAC.preservedSet<AllAnalysesOn<MachineFunction>>() &&
         !PAC.preservedSet<CFGAnalyses>();
}

AnalysisKey MachineDominatorTreeAnalysis::Key;

MachineDominatorTreeAnalysis::Result
MachineDominatorTreeAnalysis::run(MachineFunction &MF,
                                  MachineFunctionAnalysisManager &) {
  return MachineDominatorTree(MF);
}

PreservedAnalyses
MachineDominatorTreePrinterPass::run(MachineFunction &MF,
                                     MachineFunctionAnalysisManager &MFAM) {
  OS << "MachineDominatorTree for machine function: " << MF.getName() << '\n';
  MFAM.getResult<MachineDominatorTreeAnalysis>(MF).print(OS);
  return PreservedAnalyses::all();
}

char MachineDominatorTreeWrapperPass::ID = 0;

INITIALIZE_PASS(MachineDominatorTreeWrapperPass, "machinedomtree",
                "MachineDominator Tree Construction", true, true)

MachineDominatorTreeWrapperPass::MachineDominatorTreeWrapperPass()
    : MachineFunctionPass(ID) {}

char &llvm::MachineDominatorsID = MachineDominatorTreeWrapperPass::ID;

bool MachineDominatorTreeWrapperPass::runOnMachineFunction(MachineFunction &F) {
  if (F.empty()) {
    assert(F.getProperties().hasFailedISel() &&
           "Machine function should not be empty unless ISel failed.");
    return false;
  }

  DT = MachineDominatorTree(F);
  return false;
}

void MachineDominatorTreeWrapperPass::releaseMemory() { DT.reset(); }

void MachineDominatorTreeWrapperPass::verifyAnalysis() const {
  if (DT && DT->root_size() > 0) {
    const auto &Ctx = DT->getRoot()
                          ->getParent()
                          ->getFunction()
                          .getContext()
                          .getOptionsContext();
    if (getVerifyMachineDomInfo(Ctx))
      if (!DT->verify(MachineDominatorTree::VerificationLevel::Basic))
        report_fatal_error("MachineDominatorTree verification failed!");
  }
}

void MachineDominatorTreeWrapperPass::print(raw_ostream &OS,
                                            const Module *) const {
  if (DT)
    DT->print(OS);
}
