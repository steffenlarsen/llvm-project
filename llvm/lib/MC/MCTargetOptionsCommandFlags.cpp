//===-- MCTargetOptionsCommandFlags.cpp -----------------------*- C++ //-*-===//
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

#include "llvm/MC/MCTargetOptionsCommandFlags.h"
#include "llvm/MC/MCOptionsOptInfos.h"
#include "llvm/MC/MCTargetOptions.h"

using namespace llvm;

// Each getter reads the session's parsed options.  There is no process-wide
// snapshot, so two parses in one process see their own values and a second
// parse cannot inherit the first's.
// The value these getters produce when the option was not given: the
// descriptor's Init default, which is why these read through getOptValOr:
// it returns the parsed slot.
// getOptValIfSpecified would ignore the slot and hand back TY{}, silently
// flipping every option whose default is not the zero value (there are ~28,
// including -x86-relax-relocations and -unique-section-names).

// The value these getters must produce when the registry is absent from the
// context: the descriptor's Init default if it has one, else the zero value.
// This is what the old primed snapshot produced, and it matters --
// CG_BBSections defaults to "none", and TY{} ("") instead routes
// getBBSectionsMode() into the function-list branch and tries to open a file
// named "".

#define MCOPT(TY, NAME)                                                        \
  TY llvm::mc::get##NAME(const clv2::OptionsContext &Ctx) {                    \
    return clv2::getOptValOr<&clv2::MC_##NAME>(Ctx, TY{});                     \
  }

#define MCSTROPT(NAME)                                                         \
  std::string llvm::mc::get##NAME(const clv2::OptionsContext &Ctx) {           \
    return clv2::getOptValOr<&clv2::MC_##NAME>(Ctx, std::string{});            \
  }

#define MCOPT_EXP(TY, NAME)                                                    \
  MCOPT(TY, NAME)                                                              \
  std::optional<TY> llvm::mc::getExplicit##NAME(                               \
      const clv2::OptionsContext &Ctx) {                                       \
    if (clv2::wasOptSpecified<&clv2::MC_##NAME>(Ctx))                          \
      return clv2::getOptValOr<&clv2::MC_##NAME>(Ctx, TY{});                   \
    return std::nullopt;                                                       \
  }

MCOPT_EXP(bool, RelaxAll)
MCOPT(bool, IncrementalLinkerCompatible)
MCOPT(bool, FDPIC)
MCOPT(int, DwarfVersion)
MCOPT(bool, Dwarf64)
MCOPT(bool, EmitCompactUnwindNonCanonical)
MCOPT(bool, EmitSFrameUnwind)
MCOPT(bool, ShowMCInst)
MCOPT(bool, FatalWarnings)
MCOPT(bool, NoWarn)
MCOPT(bool, NoDeprecatedWarn)
MCOPT(bool, NoTypeCheck)
MCOPT(bool, SaveTempLabels)
MCOPT(bool, Crel)
MCOPT(bool, ImplicitMapSyms)
MCOPT(bool, X86RelaxRelocations)
MCOPT(bool, X86Sse2Avx)
MCOPT(RelocSectionSymType, RelocSectionSym)
MCOPT(bool, LargeEHEncoding)
MCSTROPT(ABIName)
MCSTROPT(AsSecureLogFile)

// Unlike the others this one always propagated its Init default, so it reads
// the descriptor default rather than TY{} -- TY{} is Always(0), not Default(2).
EmitDwarfUnwindType
llvm::mc::getEmitDwarfUnwind(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOrDefault<&clv2::MC_EmitDwarfUnwind>(Ctx);
}

// Tri-state: the CLI enum's Default means "no opinion", which stays nullopt.
std::optional<bool>
llvm::mc::getDwarfExtendedLoc(const clv2::OptionsContext &Ctx) {
  if (!clv2::wasOptSpecified<&clv2::MC_DwarfExtendedLoc>(Ctx))
    return std::nullopt;
  auto Val = clv2::getOptValOrDefault<&clv2::MC_DwarfExtendedLoc>(Ctx);
  if (Val == clv2::DefaultOnOff::Default)
    return std::nullopt;
  return Val == clv2::DefaultOnOff::Enable;
}

std::optional<bool>
llvm::mc::getUseLEB128Directives(const clv2::OptionsContext &Ctx) {
  if (!clv2::wasOptSpecified<&clv2::MC_UseLEB128Directives>(Ctx))
    return std::nullopt;
  return clv2::getOptValOr<&clv2::MC_UseLEB128Directives>(Ctx, false);
}

bool llvm::mc::getLFIEnableRewriter(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOr<&clv2::MC_LFIEnableRewriter>(Ctx, true);
}

unsigned llvm::mc::getAsmMacroMaxNestingDepth(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOr<&clv2::MC_AsmMacroMaxNestingDepth>(Ctx, 20u);
}

// Retained as a no-op: tools instantiate it to declare that they want the MC
// options registered.  There is no longer a snapshot for it to prime.
llvm::mc::RegisterMCTargetOptionsFlags::RegisterMCTargetOptionsFlags() =
    default;

MCTargetOptions
llvm::mc::InitMCTargetOptionsFromFlags(const clv2::OptionsContext &OptsCtx) {
  MCTargetOptions Options;
  Options.OptsCtx = &OptsCtx;
  Options.MCRelaxAll = getRelaxAll(OptsCtx);
  Options.MCIncrementalLinkerCompatible =
      getIncrementalLinkerCompatible(OptsCtx);
  Options.FDPIC = getFDPIC(OptsCtx);
  Options.Dwarf64 = getDwarf64(OptsCtx);
  Options.DwarfVersion = getDwarfVersion(OptsCtx);
  Options.ShowMCInst = getShowMCInst(OptsCtx);
  Options.ABIName = getABIName(OptsCtx);
  Options.MCFatalWarnings = getFatalWarnings(OptsCtx);
  Options.MCNoWarn = getNoWarn(OptsCtx);
  Options.MCNoDeprecatedWarn = getNoDeprecatedWarn(OptsCtx);
  Options.MCNoTypeCheck = getNoTypeCheck(OptsCtx);
  Options.MCSaveTempLabels = getSaveTempLabels(OptsCtx);
  Options.Crel = getCrel(OptsCtx);
  Options.ImplicitMapSyms = getImplicitMapSyms(OptsCtx);
  Options.X86RelaxRelocations = getX86RelaxRelocations(OptsCtx);
  Options.X86Sse2Avx = getX86Sse2Avx(OptsCtx);
  Options.RelocSectionSym = getRelocSectionSym(OptsCtx);
  Options.LargeEHEncoding = getLargeEHEncoding(OptsCtx);
  Options.EmitDwarfUnwind = getEmitDwarfUnwind(OptsCtx);
  Options.EmitCompactUnwindNonCanonical =
      getEmitCompactUnwindNonCanonical(OptsCtx);
  Options.EmitSFrameUnwind = getEmitSFrameUnwind(OptsCtx);
  Options.AsSecureLogFile = getAsSecureLogFile(OptsCtx);

  return Options;
}
