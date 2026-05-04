//===- CodeGenPassOptionsOptInfos.h - clv2 option decls --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_CODEGENPASSOPTIONSOPTINFOS_H
#define LLVM_CODEGEN_CODEGENPASSOPTIONSOPTINFOS_H

#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"
#include <climits>

namespace llvm {
class Function;
}

#define CLV2_OPTIONS_DECL
#include "llvm/CodeGen/CodeGenPassOptionsOptInfos.inc"
#undef CLV2_OPTIONS_DECL

// Options guarded by platform-specific #ifdef — defined manually.
namespace llvm::clv2 {

#ifdef LLVM_GISEL_COV_PREFIX
inline constexpr OptionInfo<std::string> CGPASS_GiselCoveragePrefix{
    "gisel-coverage-prefix",
    "Record GlobalISel rule coverage files of this prefix if instrumentation "
    "was generated",
    Init{LLVM_GISEL_COV_PREFIX}, Hidden};
#endif

} // namespace llvm::clv2

namespace llvm::cgpass_opts {

using CGPassAsmPrintRegOpts = decltype(clv2::CGPassAsmPrintReg)::ParsedOptionsT;
using CGPassCore1RegOpts = decltype(clv2::CGPassCore1Reg)::ParsedOptionsT;
using CGPassCore2RegOpts = decltype(clv2::CGPassCore2Reg)::ParsedOptionsT;
using CGPassGISelRegOpts = decltype(clv2::CGPassGISelReg)::ParsedOptionsT;
using CGPassMachine1RegOpts = decltype(clv2::CGPassMachine1Reg)::ParsedOptionsT;
using CGPassMachine2RegOpts = decltype(clv2::CGPassMachine2Reg)::ParsedOptionsT;
using CGPassRegAllocRegOpts = decltype(clv2::CGPassRegAllocReg)::ParsedOptionsT;
using CGPassSched1RegOpts = decltype(clv2::CGPassSched1Reg)::ParsedOptionsT;
using CGPassSched2RegOpts = decltype(clv2::CGPassSched2Reg)::ParsedOptionsT;
using CGPassSelDAGRegOpts = decltype(clv2::CGPassSelDAGReg)::ParsedOptionsT;

LLVM_ABI const CGPassAsmPrintRegOpts *
getCGPassAsmPrintReg(const clv2::OptionsContext &Ctx);
LLVM_ABI const CGPassCore1RegOpts *
getCGPassCore1Reg(const clv2::OptionsContext &Ctx);
LLVM_ABI const CGPassCore2RegOpts *
getCGPassCore2Reg(const clv2::OptionsContext &Ctx);
LLVM_ABI const CGPassGISelRegOpts *
getCGPassGISelReg(const clv2::OptionsContext &Ctx);
LLVM_ABI const CGPassMachine1RegOpts *
getCGPassMachine1Reg(const clv2::OptionsContext &Ctx);
LLVM_ABI const CGPassMachine2RegOpts *
getCGPassMachine2Reg(const clv2::OptionsContext &Ctx);
LLVM_ABI const CGPassRegAllocRegOpts *
getCGPassRegAllocReg(const clv2::OptionsContext &Ctx);
LLVM_ABI const CGPassSched1RegOpts *
getCGPassSched1Reg(const clv2::OptionsContext &Ctx);
LLVM_ABI const CGPassSched2RegOpts *
getCGPassSched2Reg(const clv2::OptionsContext &Ctx);
LLVM_ABI const CGPassSelDAGRegOpts *
getCGPassSelDAGReg(const clv2::OptionsContext &Ctx);

} // namespace llvm::cgpass_opts

#include "llvm/IR/Function.h"
#define CLV2_OPTIONS_GETTERS
#include "llvm/CodeGen/CodeGenPassOptionsOptInfos.inc"
#undef CLV2_OPTIONS_GETTERS

#endif // LLVM_CODEGEN_CODEGENPASSOPTIONSOPTINFOS_H
