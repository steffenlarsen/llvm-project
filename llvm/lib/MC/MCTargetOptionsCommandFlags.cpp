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
#include "llvm/MC/MCOptions.h"
#include "llvm/MC/MCTargetOptions.h"

using namespace llvm;

// Each getter reads the process-wide MCLibraryOptions::Current struct
// populated by parseLibraryOptionsChain<...>() (see
// llvm/include/llvm/Option/LibraryOptions.h); the passed clv2::OptionsContext
// is unused and kept only so this file's stable, LLVM_ABI-exported getter
// signatures don't change out from under their ~8 external callers.

#define MCOPT(TY, NAME)                                                        \
  TY llvm::mc::get##NAME(const clv2::OptionsContext &) {                       \
    return MCLibraryOptions::Current.MC_##NAME;                                \
  }

#define MCSTROPT(NAME)                                                         \
  std::string llvm::mc::get##NAME(const clv2::OptionsContext &) {              \
    return MCLibraryOptions::Current.MC_##NAME;                                \
  }

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
MCOPT(bool, DisableIntegratedAS)
MCOPT(RelocSectionSymType, RelocSectionSym)
MCOPT(bool, LargeEHEncoding)
MCSTROPT(ABIName)
MCSTROPT(AsSecureLogFile)

// RelaxAll needs optional semantics for getExplicitRelaxAll() (nullopt means
// "never specified"), hence the underlying field is std::optional<bool>
// (see MCOptions.td) rather than a plain bool; getRelaxAll() resolves it to
// a plain bool, defaulting to false when unspecified.
bool llvm::mc::getRelaxAll(const clv2::OptionsContext &) {
  return MCLibraryOptions::Current.MC_RelaxAll.value_or(false);
}

std::optional<bool>
llvm::mc::getExplicitRelaxAll(const clv2::OptionsContext &) {
  return MCLibraryOptions::Current.MC_RelaxAll;
}

EmitDwarfUnwindType llvm::mc::getEmitDwarfUnwind(const clv2::OptionsContext &) {
  return MCLibraryOptions::Current.MC_EmitDwarfUnwind;
}

// Tri-state: the CLI enum's Default member means "no opinion", which
// collapses to nullopt (both when the option was never specified -- it then
// carries its compile-time default of DefaultOnOff::Default -- and when it
// was explicitly specified as "Default").
std::optional<bool>
llvm::mc::getDwarfExtendedLoc(const clv2::OptionsContext &) {
  DefaultOnOff Val = MCLibraryOptions::Current.MC_DwarfExtendedLoc;
  if (Val == DefaultOnOff::Default)
    return std::nullopt;
  return Val == DefaultOnOff::Enable;
}

// UseLEB128Directives needs optional semantics (see MCAsmInfo.cpp /
// MCAsmInfoXCOFF.cpp, which distinguish "never specified" from an explicit
// value), hence the underlying field is std::optional<bool>.
std::optional<bool>
llvm::mc::getUseLEB128Directives(const clv2::OptionsContext &) {
  return MCLibraryOptions::Current.MC_UseLEB128Directives;
}

bool llvm::mc::getLFIEnableRewriter(const clv2::OptionsContext &) {
  return MCLibraryOptions::Current.MC_LFIEnableRewriter;
}

unsigned llvm::mc::getAsmMacroMaxNestingDepth(const clv2::OptionsContext &) {
  return MCLibraryOptions::Current.MC_AsmMacroMaxNestingDepth;
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
  Options.DisableIntegratedAS = getDisableIntegratedAS(OptsCtx);
  Options.RelocSectionSym = getRelocSectionSym(OptsCtx);
  Options.LargeEHEncoding = getLargeEHEncoding(OptsCtx);
  Options.EmitDwarfUnwind = getEmitDwarfUnwind(OptsCtx);
  Options.EmitCompactUnwindNonCanonical =
      getEmitCompactUnwindNonCanonical(OptsCtx);
  Options.EmitSFrameUnwind = getEmitSFrameUnwind(OptsCtx);
  Options.AsSecureLogFile = getAsSecureLogFile(OptsCtx);

  return Options;
}
