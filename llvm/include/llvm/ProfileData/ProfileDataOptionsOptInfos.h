//===- ProfileDataOptionsOptInfos.h - clv2 OptionInfo decls -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// clv2 OptionInfo declarations for ProfileData library command-line flags.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_PROFILEDATAOPTIONSOPTINFOS_H
#define LLVM_PROFILEDATA_PROFILEDATAOPTIONSOPTINFOS_H

#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"

namespace llvm {
class Function;
}

#include "llvm/ProfileData/SampleProf.h"

#define CLV2_OPTIONS_DECL
#include "llvm/ProfileData/ProfileDataOptionsOptInfos.inc"
#undef CLV2_OPTIONS_DECL

namespace llvm::pd_opts {
using ParsedOpts = decltype(clv2::ProfileDataOptsReg)::ParsedOptionsT;
} // namespace llvm::pd_opts

#include "llvm/IR/Function.h"
#define CLV2_OPTIONS_GETTERS
#include "llvm/ProfileData/ProfileDataOptionsOptInfos.inc"
#undef CLV2_OPTIONS_GETTERS

#endif // LLVM_PROFILEDATA_PROFILEDATAOPTIONSOPTINFOS_H
