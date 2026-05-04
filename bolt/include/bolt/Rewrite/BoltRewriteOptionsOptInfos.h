//===- BoltRewriteOptionsOptInfos.h - clv2 OptionInfo decls -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef BOLT_REWRITE_BOLTREWRITEOPTIONSOPTINFOS_H
#define BOLT_REWRITE_BOLTREWRITEOPTIONSOPTINFOS_H

#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"

namespace llvm {
class Function;
}

#include "bolt/Utils/BoltUtilsOptionsOptInfos.h"

#define CLV2_OPTIONS_DECL
#include "bolt/Rewrite/BoltRewriteOptionsOptInfos.inc"
#undef CLV2_OPTIONS_DECL

// Unscoped aliases for BoltPrintPseudoProbesOptions enum class members
// (consumers reference these as clv2::BOLTRW_PPP_*)
namespace llvm::clv2 {
inline constexpr auto BOLTRW_PPP_None =
    BoltPrintPseudoProbesOptions::BOLTRW_PPP_None;
inline constexpr auto BOLTRW_PPP_Probes_Section_Decode =
    BoltPrintPseudoProbesOptions::BOLTRW_PPP_Probes_Section_Decode;
inline constexpr auto BOLTRW_PPP_Probes_Address_Conversion =
    BoltPrintPseudoProbesOptions::BOLTRW_PPP_Probes_Address_Conversion;
inline constexpr auto BOLTRW_PPP_Encoded_Probes =
    BoltPrintPseudoProbesOptions::BOLTRW_PPP_Encoded_Probes;
inline constexpr auto BOLTRW_PPP_All =
    BoltPrintPseudoProbesOptions::BOLTRW_PPP_All;

// Unscoped aliases for BoltRuntimeLibInitHookTarget enum class members
// (consumers reference these as clv2::BOLTRW_RLIH_*)
inline constexpr auto BOLTRW_RLIH_ENTRY_POINT =
    BoltRuntimeLibInitHookTarget::BOLTRW_RLIH_ENTRY_POINT;
inline constexpr auto BOLTRW_RLIH_INIT =
    BoltRuntimeLibInitHookTarget::BOLTRW_RLIH_INIT;
inline constexpr auto BOLTRW_RLIH_INIT_ARRAY =
    BoltRuntimeLibInitHookTarget::BOLTRW_RLIH_INIT_ARRAY;
} // namespace llvm::clv2

namespace llvm::clv2 {
class OptionsContext;
}

namespace llvm::bolt::bolt_rewrite_opts {
using ParsedOpts = decltype(clv2::BoltRewriteOptsReg)::ParsedOptionsT;
LLVM_ABI const ParsedOpts *getBoltRewriteOpts(const clv2::OptionsContext &Ctx);
} // namespace llvm::bolt::bolt_rewrite_opts

#include "bolt/Core/BinaryContext.h"
#define CLV2_OPTIONS_GETTERS
#include "bolt/Rewrite/BoltRewriteOptionsOptInfos.inc"
#undef CLV2_OPTIONS_GETTERS

#endif // BOLT_REWRITE_BOLTREWRITEOPTIONSOPTINFOS_H
