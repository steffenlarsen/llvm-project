//===- AMDGPUOptionsOptInfos.h - clv2 OptionInfo decls for AMDGPU -*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_AMDGPU_AMDGPUOPTIONSOPTINFOS_H
#define LLVM_TARGET_AMDGPU_AMDGPUOPTIONSOPTINFOS_H

#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"

namespace llvm {
class Function;
}

#define CLV2_OPTIONS_DECL
#include "llvm/Target/AMDGPU/AMDGPUOptionsOptInfos.inc"
#undef CLV2_OPTIONS_DECL

namespace llvm::amdgpu_opts {
using ParsedOpts = decltype(clv2::AMDGPUOptsReg)::ParsedOptionsT;
} // namespace llvm::amdgpu_opts

#include "llvm/IR/Function.h"
#define CLV2_OPTIONS_GETTERS
#include "llvm/Target/AMDGPU/AMDGPUOptionsOptInfos.inc"
#undef CLV2_OPTIONS_GETTERS

#endif // LLVM_TARGET_AMDGPU_AMDGPUOPTIONSOPTINFOS_H
