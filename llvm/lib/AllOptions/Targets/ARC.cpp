//===- ARC.cpp - clv2 ARC option registration -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Registers ARC's options into RegisterAllLLVMOptions.  See XCore.cpp for why
// there is one TU per target.
//
// ARC is an experimental target, so this file is only in the build when
// LLVM_EXPERIMENTAL_TARGETS_TO_BUILD selects it.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/CommandLineV2.h"
#include "llvm/Target/ARC/ARCOptionsOptInfos.h"

namespace llvm {
void registerARCOptionsInAll(clv2::OptionParser &P) {
  P.add<&clv2::ARCOptsReg>();
}
} // namespace llvm
