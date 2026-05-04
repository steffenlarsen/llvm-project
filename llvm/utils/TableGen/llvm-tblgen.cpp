//===- llvm-tblgen.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the main function for LLVM's TableGen.
//
//===----------------------------------------------------------------------===//

#include "Basic/TableGen.h"
#include "RegisterBackendOptions.h"
#include "llvm/Support/SupportOptions.h"
#include "llvm/Support/SupportOptionsOptInfos.h"

void registerExtraTblgenOptions(llvm::clv2::OptionParser &P) {
  registerAllLLVMTblgenBackendOptions(P);
  P.add<&llvm::clv2::SupportOptsReg, llvm::support::applySupportOptions>();
}

/// Command line parameters are shared between llvm-tblgen and llvm-min-tblgen.
/// The indirection to tblgen_main exists to ensure that the static variables
/// for the llvm::cl:: mechanism are linked into both executables.
int main(int argc, char **argv) { return tblgen_main(argc, argv); }
