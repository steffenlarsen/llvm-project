//===- XCoreOptionsRegistration.cpp - clv2 option registration
//-------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/CommandLineV2.h"
#include "llvm/Target/TargetOptionsRegistration.h"
#include "llvm/Target/XCore/XCoreOptionsOptInfos.h"

void llvm::registerXCoreOptions(llvm::clv2::OptionParser &P) {
  P.add<&llvm::clv2::XCoreOptsReg>();
}
