//===- LoongArchInstPrinter.cpp - Convert LoongArch MCInst to asm syntax --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class prints an LoongArch MCInst to a .s file.
//
//===----------------------------------------------------------------------===//

#include "LoongArchInstPrinter.h"
#include "LoongArchMCTargetDesc.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Target/LoongArch/LoongArchOptionsOptInfos.h"
using namespace llvm;

#define DEBUG_TYPE "loongarch-asm-printer"

// Include the auto-generated portion of the assembly writer.
#define PRINT_ALIAS_INSTR
#include "LoongArchGenAsmWriter.inc"

static bool NumericReg = false;

static bool getNoAliases(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOr<&clv2::LoongArchOptsReg, &clv2::LA_NoAliases>(Ctx,
                                                                         false);
}

static bool getNumericReg(const clv2::OptionsContext &Ctx) {
  if (auto *O = clv2::getView<&clv2::LoongArchOptsReg>(Ctx))
    return O->get<&clv2::LA_NumericReg>() || NumericReg;
  return NumericReg;
}

// The command-line flag above is used by llvm-mc and llc. It can be used by
// `llvm-objdump`, but we override the value here to handle options passed to
// `llvm-objdump` with `-M` (which matches GNU objdump). There did not seem to
// be an easier way to allow these options in all these tools, without doing it
// this way.
bool LoongArchInstPrinter::applyTargetSpecificCLOption(StringRef Opt) {
  if (Opt == "no-aliases") {
    PrintAliases = false;
    return true;
  }

  if (Opt == "numeric") {
    NumericReg = true;
    return true;
  }

  return false;
}

void LoongArchInstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                     StringRef Annot,
                                     const MCSubtargetInfo &STI,
                                     raw_ostream &O) {
  if (!PrintAliases || getNoAliases(getOptionsContext()) ||
      !printAliasInstr(MI, Address, STI, O))
    printInstruction(MI, Address, STI, O);
  printAnnotation(O, Annot);
}

void LoongArchInstPrinter::printRegName(raw_ostream &O, MCRegister Reg) {
  O << '$'
    << getRegisterName(Reg, getNumericReg(getOptionsContext())
                                ? LoongArch::NoRegAltName
                                : LoongArch::RegAliasName);
}

void LoongArchInstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                        const MCSubtargetInfo &STI,
                                        raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNo);

  if (MO.isReg()) {
    printRegName(O, MO.getReg());
    return;
  }

  if (MO.isImm()) {
    O << MO.getImm();
    return;
  }

  assert(MO.isExpr() && "Unknown operand kind in printOperand");
  MAI.printExpr(O, *MO.getExpr());
}

void LoongArchInstPrinter::printAtomicMemOp(const MCInst *MI, unsigned OpNo,
                                            const MCSubtargetInfo &STI,
                                            raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNo);
  assert(MO.isReg() && "printAtomicMemOp can only print register operands");
  printRegName(O, MO.getReg());
}

const char *LoongArchInstPrinter::getRegisterName(MCRegister Reg) {
  // Default print reg alias name
  return getRegisterName(Reg, LoongArch::RegAliasName);
}
