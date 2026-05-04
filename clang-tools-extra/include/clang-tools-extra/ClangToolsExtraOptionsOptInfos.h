//===- ClangToolsExtraOptionsOptInfos.h - clv2 option decls --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_TOOLS_EXTRA_OPTIONSOPTINFOS_H
#define CLANG_TOOLS_EXTRA_OPTIONSOPTINFOS_H

#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"

namespace llvm {
class Function;
}

#define CLV2_OPTIONS_DECL
#include "clang-tools-extra/ClangToolsExtraOptionsOptInfos.inc"
#undef CLV2_OPTIONS_DECL

// Options guarded by platform-specific #ifdef — defined manually.
namespace llvm::clv2 {

#if defined(__GLIBC__) && CLANGD_MALLOC_TRIM
inline constexpr OptionInfo<bool> CTE_D_MallocTrim{
    "malloc-trim", "Release memory periodically via malloc_trim(3).",
    Init{true}};
#endif

#if CLANGD_ENABLE_REMOTE
inline constexpr OptionInfo<std::string> CTE_D_RemoteIndexAddress{
    "remote-index-address", "Address of the remote index server"};

inline constexpr OptionInfo<std::string> CTE_D_ProjectRoot{
    "project-root",
    "Path to the project root. Requires remote-index-address to be set."};
#endif

} // namespace llvm::clv2

#include "llvm/IR/Function.h"
#define CLV2_OPTIONS_GETTERS
#include "clang-tools-extra/ClangToolsExtraOptionsOptInfos.inc"
#undef CLV2_OPTIONS_GETTERS

#endif // CLANG_TOOLS_EXTRA_OPTIONSOPTINFOS_H
