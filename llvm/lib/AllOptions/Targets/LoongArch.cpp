//===- LoongArch.cpp - clv2 LoongArch option registration -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Registers LoongArch's options into RegisterAllLLVMOptions.
//
// One TU per target, rather than all 18 in AllOptions.cpp: the generated target
// options headers were 55% of that file's 1.42 GB peak RSS.  A file per target
// keeps each TU small and means adding a target is one new obviously-named
// file, with no grouping to rebalance.
//
// These live in LLVMAllOptions rather than calling the
// registerLoongArchOptions() in lib/Target/LoongArch: LLVMAllOptions is linked
// by clang tools that do not link target CodeGen, and making them do so to
// reach a registration function would cost far more binary size than this
// saves.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/CommandLineV2.h"
#include "llvm/Target/LoongArch/LoongArchOptionsOptInfos.h"

namespace llvm {
void registerLoongArchOptionsInAll(clv2::OptionParser &P) {
  P.add<&clv2::LoongArchOptsReg>();
}
} // namespace llvm
