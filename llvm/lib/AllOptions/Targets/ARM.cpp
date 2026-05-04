//===- ARM.cpp - clv2 ARM option registration -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Registers ARM's options into RegisterAllLLVMOptions.
//
// One TU per target, rather than all 18 in AllOptions.cpp: the generated target
// options headers were 55% of that file's 1.42 GB peak RSS.  A file per target
// keeps each TU small and means adding a target is one new obviously-named
// file, with no grouping to rebalance.
//
// These live in LLVMAllOptions rather than calling the registerARMOptions()
// in lib/Target/ARM: LLVMAllOptions is linked by clang tools that do not link
// target CodeGen, and making them do so to reach a registration function would
// cost far more binary size than this saves.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/CommandLineV2.h"
#include "llvm/Target/ARM/ARMOptionsOptInfos.h"

namespace llvm {
void registerARMOptionsInAll(clv2::OptionParser &P) {
  P.add<&clv2::ARMOptsReg>();
}
} // namespace llvm
