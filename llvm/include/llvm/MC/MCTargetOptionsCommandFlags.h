//===-- MCTargetOptionsCommandFlags.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains machine code-specific flags that are shared between
// different command line tools.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCTARGETOPTIONSCOMMANDFLAGS_H
#define LLVM_MC_MCTARGETOPTIONSCOMMANDFLAGS_H

#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"
#include <optional>
#include <string>

namespace llvm {

class MCTargetOptions;
enum class RelocSectionSymType;
enum class EmitDwarfUnwindType;
class StringRef;

namespace mc {

LLVM_ABI bool getRelaxAll(const clv2::OptionsContext &Ctx);
LLVM_ABI std::optional<bool>
getExplicitRelaxAll(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getIncrementalLinkerCompatible(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getFDPIC(const clv2::OptionsContext &Ctx);

LLVM_ABI int getDwarfVersion(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getDwarf64(const clv2::OptionsContext &Ctx);

LLVM_ABI EmitDwarfUnwindType
getEmitDwarfUnwind(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getEmitCompactUnwindNonCanonical(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getEmitSFrameUnwind(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getShowMCInst(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getFatalWarnings(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getNoWarn(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getNoDeprecatedWarn(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getNoTypeCheck(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getSaveTempLabels(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getCrel(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getImplicitMapSyms(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getX86RelaxRelocations(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getX86Sse2Avx(const clv2::OptionsContext &Ctx);

LLVM_ABI RelocSectionSymType
getRelocSectionSym(const clv2::OptionsContext &Ctx);

LLVM_ABI std::string getABIName(const clv2::OptionsContext &Ctx);

LLVM_ABI std::string getAsSecureLogFile(const clv2::OptionsContext &Ctx);

LLVM_ABI std::optional<bool>
getDwarfExtendedLoc(const clv2::OptionsContext &Ctx);

LLVM_ABI std::optional<bool>
getUseLEB128Directives(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getLFIEnableRewriter(const clv2::OptionsContext &Ctx);

LLVM_ABI unsigned getAsmMacroMaxNestingDepth(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getLargeEHEncoding(const clv2::OptionsContext &Ctx);

} // namespace mc
} // namespace llvm

#include "llvm/MC/MCOptionsOptInfos.h"

namespace llvm::mc {

/// The parsed-options view type for the MC library registry.
using ParsedOpts = decltype(clv2::MCOptsReg)::ParsedOptionsT;

/// Create this object with static storage to register mc-related command
/// line options.
struct RegisterMCTargetOptionsFlags {
  LLVM_ABI RegisterMCTargetOptionsFlags();
};

LLVM_ABI MCTargetOptions
InitMCTargetOptionsFromFlags(const clv2::OptionsContext &OptsCtx);

/// Self-register MCOptsReg for runtime option parsing.

} // namespace llvm::mc

#endif
