//===- AsmParserOptionsRegistration.cpp - clv2 option registration
//----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The single place this library's option registries are instantiated for
// registration.  See AsmParserOptionsRegistration.h.
//
//===----------------------------------------------------------------------===//

#include "llvm/AsmParser/AsmParserOptionsRegistration.h"
#include "llvm/AsmParser/AsmParserOptionsOptInfos.h"
#include "llvm/Support/CommandLineV2.h"

void llvm::registerAsmParserOptsOptions(llvm::clv2::OptionParser &P) {
  P.add<&clv2::AsmParserOptsReg>();
}
