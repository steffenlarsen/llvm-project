//===- AllOptions.cpp - Register all LLVM clv2 option registries ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/CodeGenOptionsRegistration.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/SupportOptionsOptInfos.h"
#ifdef LINK_POLLY_INTO_TOOLS
#include "polly/PollyOptionsOptInfos.h"
#endif
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/MC/MCTargetOptionsCommandFlags.h"
#include "llvm/Support/SupportOptions.h"

namespace llvm {

void RegisterCoreLLVMOptions(clv2::OptionParser &P) {
  using namespace clv2;
  P.add<&SupportOptsReg, support::applySupportOptions>();
}

void RegisterCommonLLVMOptions(clv2::OptionParser &P) {
  using namespace clv2;
  // Core
  P.add<&SupportOptsReg, support::applySupportOptions>();
  P.enableGlobalDynamicEntries();
}

void RegisterAllLLVMOptions(clv2::OptionParser &P) {
  using namespace clv2;
  P.add<&SupportOptsReg, support::applySupportOptions>();
  registerCGOptsOptions(P);
#ifdef LINK_POLLY_INTO_TOOLS
  P.add<&PollyOptsReg, polly_opts::applyPollyOptions>();
#endif
  P.enableGlobalDynamicEntries();
}

void RegisterCommonLLVMOptionsHidden(clv2::OptionParser &P) {
  RegisterAllLLVMOptions(P);
  P.hideAllDynamicEntries();
}

} // namespace llvm
