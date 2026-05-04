//===- CodeGenOptionsRegistration.cpp - clv2 option registration ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The single place this library's option registries are instantiated for
// registration.  See CodeGenOptionsRegistration.h.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/CodeGenOptionsRegistration.h"
#include "llvm/CodeGen/CodeGenPassOptionsOptInfos.h"
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/Support/CommandLineV2.h"

void llvm::registerCGOptsOptions(llvm::clv2::OptionParser &P) {
  P.add<&clv2::CGOptsReg>();
}

void llvm::registerCGPassAsmPrintOptions(llvm::clv2::OptionParser &P) {
  P.add<&clv2::CGPassAsmPrintReg>();
}

void llvm::registerCGPassCore1Options(llvm::clv2::OptionParser &P) {
  P.add<&clv2::CGPassCore1Reg>();
}

void llvm::registerCGPassCore2Options(llvm::clv2::OptionParser &P) {
  P.add<&clv2::CGPassCore2Reg>();
}

void llvm::registerCGPassGISelOptions(llvm::clv2::OptionParser &P) {
  P.add<&clv2::CGPassGISelReg>();
}

void llvm::registerCGPassMachine1Options(llvm::clv2::OptionParser &P) {
  P.add<&clv2::CGPassMachine1Reg>();
}

void llvm::registerCGPassMachine2Options(llvm::clv2::OptionParser &P) {
  P.add<&clv2::CGPassMachine2Reg>();
}

void llvm::registerCGPassAllocOptions(llvm::clv2::OptionParser &P) {
  P.add<&clv2::CGPassRegAllocReg>();
}

void llvm::registerCGPassSched1Options(llvm::clv2::OptionParser &P) {
  P.add<&clv2::CGPassSched1Reg>();
}

void llvm::registerCGPassSched2Options(llvm::clv2::OptionParser &P) {
  P.add<&clv2::CGPassSched2Reg>();
}

void llvm::registerCGPassSelDAGOptions(llvm::clv2::OptionParser &P) {
  P.add<&clv2::CGPassSelDAGReg>();
}
