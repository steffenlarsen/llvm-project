//===-- MipsTargetObjectFile.cpp - Mips Object Files ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MipsTargetObjectFile.h"
#include "MCTargetDesc/MipsMCAsmInfo.h"
#include "MipsSubtarget.h"
#include "MipsTargetMachine.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Target/Mips/MipsOptionsOptInfos.h"
#include "llvm/Target/TargetMachine.h"
using namespace llvm;

static unsigned getSSThreshold(const Module &M) {
  return clv2::getOptValOrDefault<&clv2::MIPS_SSThreshold>(
      M.getContext().getOptionsContext());
}

static bool getLocalSData(const Module &M) {
  return clv2::getOptValOrDefault<&clv2::MIPS_LocalSData>(
      M.getContext().getOptionsContext());
}

static bool getExternSData(const Module &M) {
  return clv2::getOptValOrDefault<&clv2::MIPS_ExternSData>(
      M.getContext().getOptionsContext());
}

static bool getEmbeddedData(const Module &M) {
  return clv2::getOptValOr<&clv2::MipsOptsReg, &clv2::MIPS_EmbeddedData>(
      M.getContext().getOptionsContext(), false);
}

void MipsTargetObjectFile::Initialize(MCContext &Ctx, const TargetMachine &TM){
  TargetLoweringObjectFileELF::Initialize(Ctx, TM);

  SmallDataSection = getContext().getELFSection(
      ".sdata", ELF::SHT_PROGBITS,
      ELF::SHF_WRITE | ELF::SHF_ALLOC | ELF::SHF_MIPS_GPREL);

  SmallBSSSection = getContext().getELFSection(".sbss", ELF::SHT_NOBITS,
                                               ELF::SHF_WRITE | ELF::SHF_ALLOC |
                                                   ELF::SHF_MIPS_GPREL);
  this->TM = &static_cast<const MipsTargetMachine &>(TM);
}

// A address must be loaded from a small section if its size is less than the
// small section size threshold. Data in this section must be addressed using
// gp_rel operator.
static bool IsInSmallSection(uint64_t Size, const Module &M) {
  return Size > 0 && Size <= getSSThreshold(M);
}

/// Return true if this global address should be placed into small data/bss
/// section.
bool MipsTargetObjectFile::IsGlobalInSmallSection(
    const GlobalObject *GO, const TargetMachine &TM) const {
  // We first check the case where global is a declaration, because finding
  // section kind using getKindForGlobal() is only allowed for global
  // definitions.
  if (GO->isDeclaration() || GO->hasAvailableExternallyLinkage())
    return IsGlobalInSmallSectionImpl(GO, TM);

  return IsGlobalInSmallSection(GO, TM, getKindForGlobal(GO, TM));
}

/// Return true if this global address should be placed into small data/bss
/// section.
bool MipsTargetObjectFile::
IsGlobalInSmallSection(const GlobalObject *GO, const TargetMachine &TM,
                       SectionKind Kind) const {
  return IsGlobalInSmallSectionImpl(GO, TM) &&
         (Kind.isData() || Kind.isBSS() || Kind.isCommon() ||
          Kind.isReadOnly());
}

/// Return true if this global address should be placed into small data/bss
/// section. This method does all the work, except for checking the section
/// kind.
bool MipsTargetObjectFile::
IsGlobalInSmallSectionImpl(const GlobalObject *GO,
                           const TargetMachine &TM) const {
  const MipsSubtarget &Subtarget =
      *static_cast<const MipsTargetMachine &>(TM).getSubtargetImpl();

  // Return if small section is not available.
  if (!Subtarget.useSmallSection())
    return false;

  // Only global variables, not functions.
  const GlobalVariable *GVA = dyn_cast<GlobalVariable>(GO);
  if (!GVA)
    return false;

  // If the variable has an explicit section, it is placed in that section but
  // it's addressing mode may change.
  if (GVA->hasSection()) {
    StringRef Section = GVA->getSection();

    // Explicitly placing any variable in the small data section overrides
    // the global -G value.
    if (Section == ".sdata" || Section == ".sbss")
      return true;

    // Otherwise reject accessing it through the gp pointer. There are some
    // historic cases which GCC doesn't appear to respect any more. These
    // are .lit4, .lit8 and .srdata. For the moment reject these as well.
    return false;
  }

  const Module &M = *GO->getParent();

  // Enforce -mlocal-sdata.
  if (!getLocalSData(M) && GVA->hasLocalLinkage())
    return false;

  // Enforce -mextern-sdata.
  if (!getExternSData(M) &&
      ((GVA->hasExternalLinkage() && GVA->isDeclaration()) ||
       GVA->hasCommonLinkage()))
    return false;

  // Enforce -membedded-data.
  if (getEmbeddedData(M) && GVA->isConstant())
    return false;

  Type *Ty = GVA->getValueType();

  // It is possible that the type of the global is unsized, i.e. a declaration
  // of a extern struct. In this case don't presume it is in the small data
  // section. This happens e.g. when building the FreeBSD kernel.
  if (!Ty->isSized())
    return false;

  return IsInSmallSection(GVA->getDataLayout().getTypeAllocSize(Ty), M);
}

MCSection *MipsTargetObjectFile::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  // TODO: Could also support "weak" symbols as well with ".gnu.linkonce.s.*"
  // sections?

  // Handle Small Section classification here.
  if (Kind.isBSS() && IsGlobalInSmallSection(GO, TM, Kind))
    return SmallBSSSection;
  if (Kind.isData() && IsGlobalInSmallSection(GO, TM, Kind))
    return SmallDataSection;
  if (Kind.isReadOnly() && IsGlobalInSmallSection(GO, TM, Kind))
    return SmallDataSection;

  // Otherwise, we work the same as ELF.
  return TargetLoweringObjectFileELF::SelectSectionForGlobal(GO, Kind, TM);
}

/// Return true if this constant should be placed into small data section.
bool MipsTargetObjectFile::IsConstantInSmallSection(const DataLayout &DL,
                                                    const Constant *CN,
                                                    const TargetMachine &TM,
                                                    const Function *F) const {
  if (!static_cast<const MipsTargetMachine &>(TM)
           .getSubtargetImpl()
           ->useSmallSection())
    return false;

  if (F) {
    const Module &M = *F->getParent();
    return getLocalSData(M) &&
           IsInSmallSection(DL.getTypeAllocSize(CN->getType()), M);
  }

  // No Function context available; fall back to global override.
  if (auto *O = clv2::getView<&clv2::MipsOptsReg>(
          CN->getContext().getOptionsContext()))
    return O->get<&clv2::MIPS_LocalSData>() &&
           DL.getTypeAllocSize(CN->getType()) > 0 &&
           DL.getTypeAllocSize(CN->getType()) <=
               O->get<&clv2::MIPS_SSThreshold>();
  return DL.getTypeAllocSize(CN->getType()) > 0 &&
         DL.getTypeAllocSize(CN->getType()) <= 8;
}

/// Return true if this constant should be placed into small data section.
MCSection *MipsTargetObjectFile::getSectionForConstant(
    const DataLayout &DL, SectionKind Kind, const Constant *C, Align &Alignment,
    const Function *F) const {
  if (IsConstantInSmallSection(DL, C, *TM, F))
    return SmallDataSection;

  // Otherwise, we work the same as ELF.
  return TargetLoweringObjectFileELF::getSectionForConstant(DL, Kind, C,
                                                            Alignment, F);
}

const MCExpr *
MipsTargetObjectFile::getDebugThreadLocalSymbol(const MCSymbol *Sym) const {
  const MCExpr *Expr = MCSymbolRefExpr::create(Sym, getContext());
  Expr = MCBinaryExpr::createAdd(
      Expr, MCConstantExpr::create(0x8000, getContext()), getContext());
  return MCSpecifierExpr::create(Expr, Mips::S_DTPREL, getContext());
}
