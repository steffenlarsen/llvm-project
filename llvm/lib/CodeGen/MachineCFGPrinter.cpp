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
#include "llvm/CodeGen/CodeGenPassOptionsMachine1.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/GraphWriter.h"

using namespace llvm;

#define DEBUG_TYPE "dot-machine-cfg"

static std::string getMcfgFuncName(const LLVMContext &Ctx) {
  return Ctx.getOptions<CodeGenMachine1Options>().CGPASS_McfgFuncName;
}

static std::string getMcfgDotFilenamePrefix(const LLVMContext &Ctx) {
  return Ctx.getOptions<CodeGenMachine1Options>()
      .CGPASS_McfgDotFilenamePrefix;
}

static bool getDotMcfgOnly(const LLVMContext &Ctx) {
  return Ctx.getOptions<CodeGenMachine1Options>().CGPASS_DotMcfgOnly;
}

static void writeMCFGToDotFile(MachineFunction &MF) {
  std::string Filename =
      (getMcfgDotFilenamePrefix(MF.getFunction().getContext()) + "." +
       MF.getName() + ".dot")
          .str();
  errs() << "Writing '" << Filename << "'...";

  std::error_code EC;
  raw_fd_ostream File(Filename, EC, sys::fs::OF_Text);

  DOTMachineFuncInfo MCFGInfo(&MF);

  if (!EC)
    WriteGraph(File, &MCFGInfo,
               getDotMcfgOnly(MF.getFunction().getContext()));
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
  if (!getMcfgFuncName(MF.getFunction().getContext()).empty() &&
      !MF.getName().contains(getMcfgFuncName(MF.getFunction().getContext())))
    return false;
  errs() << "Writing Machine CFG for function ";
  errs().write_escaped(MF.getName()) << '\n';

  writeMCFGToDotFile(MF);
  return false;
}

PreservedAnalyses
MachineCFGPrinterPass::run(MachineFunction &MF,
                           MachineFunctionAnalysisManager &MFAM) {
  if (!getMcfgFuncName(MF.getFunction().getContext()).empty() &&
      !MF.getName().contains(getMcfgFuncName(MF.getFunction().getContext())))
    return PreservedAnalyses::all();
  errs() << "Writing Machine CFG for function ";
  errs().write_escaped(MF.getName()) << '\n';

  writeMCFGToDotFile(MF);
  return PreservedAnalyses::all();
}
