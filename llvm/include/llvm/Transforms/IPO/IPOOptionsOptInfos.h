//===- IPOOptionsOptInfos.h - clv2 OptionInfo decls for IPO flags -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_IPOOPTIONSOPTINFOS_H
#define LLVM_TRANSFORMS_IPO_IPOOPTIONSOPTINFOS_H

#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"

namespace llvm {
class Function;
}

#include "llvm/Analysis/ReplayInlinerEnums.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/ExpandVariadicsMode.h"

#define CLV2_OPTIONS_DECL
#include "llvm/Transforms/IPO/IPOOptionsOptInfos.inc"
#undef CLV2_OPTIONS_DECL

// These were plain enums in the original header — bring values into scope
// for backward compatibility with consumers using unscoped names.
namespace llvm::clv2 {
inline constexpr auto DotScope_All = DotScope::DotScope_All;
inline constexpr auto DotScope_Alloc = DotScope::DotScope_Alloc;
inline constexpr auto DotScope_Context = DotScope::DotScope_Context;
inline constexpr auto WPDCheckMode_None = WPDCheckMode::WPDCheckMode_None;
inline constexpr auto WPDCheckMode_Trap = WPDCheckMode::WPDCheckMode_Trap;
inline constexpr auto WPDCheckMode_Fallback =
    WPDCheckMode::WPDCheckMode_Fallback;
} // namespace llvm::clv2

namespace llvm::ipo_opts {
using ParsedOpts = decltype(clv2::IPOOptsReg)::ParsedOptionsT;
} // namespace llvm::ipo_opts

#include "llvm/IR/Function.h"
#define CLV2_OPTIONS_GETTERS
#include "llvm/Transforms/IPO/IPOOptionsOptInfos.inc"
#undef CLV2_OPTIONS_GETTERS

#endif // LLVM_TRANSFORMS_IPO_IPOOPTIONSOPTINFOS_H
