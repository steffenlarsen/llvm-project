//===- ClangCodeGenOptionsOptInfos.h - clv2 option decls --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_CODEGEN_CLANGCODEGENOPTIONSOPTINFOS_H
#define CLANG_CODEGEN_CLANGCODEGENOPTIONSOPTINFOS_H

#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"

#define CLV2_OPTIONS_DECL
#include "clang/CodeGen/ClangCodeGenOptionsOptInfos.inc"
#undef CLV2_OPTIONS_DECL

namespace clang_codegen_opts {
using ParsedOpts = decltype(llvm::clv2::ClangCodeGenOptsReg)::ParsedOptionsT;
} // namespace clang_codegen_opts

#endif // CLANG_CODEGEN_CLANGCODEGENOPTIONSOPTINFOS_H
