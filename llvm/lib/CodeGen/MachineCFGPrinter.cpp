//===- MachineCFGPrinter.cpp - DOT Printer for Machine Functions ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//===----------------------------------------------------------------------===//
//
// This file defines the `-dot-machine-cfg` analysis pass, which emits
// Machine Function in DOT format in file titled `<prefix>.<function-name>.dot.
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineCFGPrinter.h"
#include "llvm/CodeGen/CodeGenPassOptionsOptInfos.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/OptionsContext.h"

using namespace llvm;

#define DEBUG_TYPE "dot-machine-cfg"

static std::string getMcfgFuncName(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOr<&clv2::CGPassMachine1Reg,
                           &clv2::CGPASS_McfgFuncName>(Ctx, std::string{});
}

static std::string getMcfgDotFilenamePrefix(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOr<&clv2::CGPassMachine1Reg,
                           &clv2::CGPASS_McfgDotFilenamePrefix>(Ctx,
                                                                std::string{});
}

static bool getDotMcfgOnly(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOrDefault<&clv2::CGPASS_DotMcfgOnly>(Ctx);
}

static void writeMCFGToDotFile(MachineFunction &MF) {
  std::string Filename =
      (getMcfgDotFilenamePrefix(
           MF.getFunction().getContext().getOptionsContext()) +
       "." + MF.getName() + ".dot")
          .str();
  errs() << "Writing '" << Filename << "'...";

  std::error_code EC;
  raw_fd_ostream File(Filename, EC, sys::fs::OF_Text);

  DOTMachineFuncInfo MCFGInfo(&MF);

  if (!EC)
    WriteGraph(
        File, &MCFGInfo,
        getDotMcfgOnly(MF.getFunction().getContext().getOptionsContext()));
  else
    errs() << "  error opening file for writing!";
  errs() << '\n';
}

namespace {

class MachineCFGPrinterLegacy : public MachineFunctionPass {
public:
  static char ID;

  MachineCFGPrinterLegacy();

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // namespace

char MachineCFGPrinterLegacy::ID = 0;

char &llvm::MachineCFGPrinterID = MachineCFGPrinterLegacy::ID;

INITIALIZE_PASS(MachineCFGPrinterLegacy, DEBUG_TYPE, "Machine CFG Printer Pass",
                false, true)

/// Default construct and initialize the pass.
MachineCFGPrinterLegacy::MachineCFGPrinterLegacy() : MachineFunctionPass(ID) {}

bool MachineCFGPrinterLegacy::runOnMachineFunction(MachineFunction &MF) {
  if (!getMcfgFuncName(MF.getFunction().getContext().getOptionsContext())
           .empty() &&
      !MF.getName().contains(
          getMcfgFuncName(MF.getFunction().getContext().getOptionsContext())))
    return false;
  errs() << "Writing Machine CFG for function ";
  errs().write_escaped(MF.getName()) << '\n';

  writeMCFGToDotFile(MF);
  return false;
}

PreservedAnalyses
MachineCFGPrinterPass::run(MachineFunction &MF,
                           MachineFunctionAnalysisManager &MFAM) {
  if (!getMcfgFuncName(MF.getFunction().getContext().getOptionsContext())
           .empty() &&
      !MF.getName().contains(
          getMcfgFuncName(MF.getFunction().getContext().getOptionsContext())))
    return PreservedAnalyses::all();
  errs() << "Writing Machine CFG for function ";
  errs().write_escaped(MF.getName()) << '\n';

  writeMCFGToDotFile(MF);
  return PreservedAnalyses::all();
}
