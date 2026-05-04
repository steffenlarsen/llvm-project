//===-- TargetOptionsCommandFlags.cpp ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lld/Common/TargetOptionsCommandFlags.h"
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"
#include <optional>

llvm::TargetOptions lld::initTargetOptionsFromCodeGenFlags(
    const llvm::clv2::OptionsContext &optsCtx) {
  return llvm::codegen::InitTargetOptionsFromCodeGenFlags(llvm::Triple(),
                                                          optsCtx);
}

std::optional<llvm::Reloc::Model>
lld::getRelocModelFromCMModel(const llvm::clv2::OptionsContext &optsCtx) {
  return llvm::codegen::getExplicitRelocModel(optsCtx);
}

std::optional<llvm::CodeModel::Model>
lld::getCodeModelFromCMModel(const llvm::clv2::OptionsContext &optsCtx) {
  return llvm::codegen::getExplicitCodeModel(optsCtx);
}

std::string lld::getCPUStr(const llvm::clv2::OptionsContext &optsCtx) {
  return llvm::codegen::getCPUStr(optsCtx);
}

std::vector<std::string>
lld::getMAttrs(const llvm::clv2::OptionsContext &optsCtx) {
  return llvm::codegen::getMAttrs(optsCtx);
}
