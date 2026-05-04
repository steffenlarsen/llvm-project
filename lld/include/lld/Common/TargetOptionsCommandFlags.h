//===-- TargetOptionsCommandFlags.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Helper to create TargetOptions from command line flags.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COMMON_TARGETOPTIONSCOMMANDFLAGS_H
#define LLD_COMMON_TARGETOPTIONSCOMMANDFLAGS_H

#include "llvm/Support/CodeGen.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Target/TargetOptions.h"
#include <optional>

namespace lld {
llvm::TargetOptions
initTargetOptionsFromCodeGenFlags(const llvm::clv2::OptionsContext &optsCtx);
std::optional<llvm::Reloc::Model>
getRelocModelFromCMModel(const llvm::clv2::OptionsContext &optsCtx);
std::optional<llvm::CodeModel::Model>
getCodeModelFromCMModel(const llvm::clv2::OptionsContext &optsCtx);
std::string getCPUStr(const llvm::clv2::OptionsContext &optsCtx);
std::vector<std::string> getMAttrs(const llvm::clv2::OptionsContext &optsCtx);
}

#endif
