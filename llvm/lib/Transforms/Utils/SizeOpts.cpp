//===-- SizeOpts.cpp - code size optimization related code ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains some shared code size optimization related code.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/SizeOpts.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Transforms/Utils/UtilsOptionsOptInfos.h"

using namespace llvm;
using namespace llvm::clv2;

bool llvm::getEnablePGSO(const clv2::OptionsContext &Ctx) {
  return getOptValOrDefault<&TU_EnablePGSO>(Ctx);
}

bool llvm::getForcePGSO(const clv2::OptionsContext &Ctx) {
  return getOptValIfSpecified<&TransformUtilsOptsReg, &TU_ForcePGSO>(Ctx,
                                                                     false);
}

int llvm::getPgsoCutoffInstrProf(const clv2::OptionsContext &Ctx) {
  return getOptValOrDefault<&TU_PgsoCutoffInstrProf>(Ctx);
}

int llvm::getPgsoCutoffSampleProf(const clv2::OptionsContext &Ctx) {
  return getOptValOrDefault<&TU_PgsoCutoffSampleProf>(Ctx);
}

bool llvm::isPGSOColdCodeOnly(ProfileSummaryInfo *PSI,
                              const clv2::OptionsContext &Ctx) {
  bool ColdCodeOnly = false;
  bool ColdCodeOnlyForInstrPGO = false;
  bool ColdCodeOnlyForSamplePGO = false;
  bool ColdCodeOnlyForPartialSamplePGO = false;
  bool LargeWorkingSetSizeOnly = true;

  if (auto *O = clv2::getView<&clv2::TransformUtilsOptsReg>(Ctx)) {
    if (O->specified<&TU_PGSOColdCodeOnly>())
      ColdCodeOnly = O->get<&TU_PGSOColdCodeOnly>();
    if (O->specified<&TU_PGSOColdCodeOnlyForInstrPGO>())
      ColdCodeOnlyForInstrPGO = O->get<&TU_PGSOColdCodeOnlyForInstrPGO>();
    if (O->specified<&TU_PGSOColdCodeOnlyForSamplePGO>())
      ColdCodeOnlyForSamplePGO = O->get<&TU_PGSOColdCodeOnlyForSamplePGO>();
    if (O->specified<&TU_PGSOColdCodeOnlyForPartialSamplePGO>())
      ColdCodeOnlyForPartialSamplePGO =
          O->get<&TU_PGSOColdCodeOnlyForPartialSamplePGO>();
    if (O->specified<&TU_PGSOLargeWorkingSetSizeOnly>())
      LargeWorkingSetSizeOnly = O->get<&TU_PGSOLargeWorkingSetSizeOnly>();
  }

  return ColdCodeOnly ||
         (PSI->hasInstrumentationProfile() && ColdCodeOnlyForInstrPGO) ||
         (PSI->hasSampleProfile() &&
          ((!PSI->hasPartialSampleProfile() && ColdCodeOnlyForSamplePGO) ||
           (PSI->hasPartialSampleProfile() &&
            ColdCodeOnlyForPartialSamplePGO))) ||
         (LargeWorkingSetSizeOnly && !PSI->hasLargeWorkingSetSize());
}

namespace {
struct BasicBlockBFIAdapter {
  static bool isFunctionColdInCallGraph(const Function *F,
                                        ProfileSummaryInfo *PSI,
                                        BlockFrequencyInfo &BFI) {
    return PSI->isFunctionColdInCallGraph(F, BFI);
  }
  static bool isFunctionHotInCallGraphNthPercentile(int CutOff,
                                                    const Function *F,
                                                    ProfileSummaryInfo *PSI,
                                                    BlockFrequencyInfo &BFI) {
    return PSI->isFunctionHotInCallGraphNthPercentile(CutOff, F, BFI);
  }
  static bool isFunctionColdInCallGraphNthPercentile(int CutOff,
                                                     const Function *F,
                                                     ProfileSummaryInfo *PSI,
                                                     BlockFrequencyInfo &BFI) {
    return PSI->isFunctionColdInCallGraphNthPercentile(CutOff, F, BFI);
  }
  static bool isColdBlock(const BasicBlock *BB,
                          ProfileSummaryInfo *PSI,
                          BlockFrequencyInfo *BFI) {
    return PSI->isColdBlock(BB, BFI);
  }
  static bool isHotBlockNthPercentile(int CutOff,
                                      const BasicBlock *BB,
                                      ProfileSummaryInfo *PSI,
                                      BlockFrequencyInfo *BFI) {
    return PSI->isHotBlockNthPercentile(CutOff, BB, BFI);
  }
  static bool isColdBlockNthPercentile(int CutOff, const BasicBlock *BB,
                                       ProfileSummaryInfo *PSI,
                                       BlockFrequencyInfo *BFI) {
    return PSI->isColdBlockNthPercentile(CutOff, BB, BFI);
  }
};
} // end anonymous namespace

bool llvm::shouldOptimizeForSize(const Function *F, ProfileSummaryInfo *PSI,
                                 BlockFrequencyInfo *BFI,
                                 PGSOQueryType QueryType) {
  if (F->hasOptSize())
    return true;
  return shouldFuncOptimizeForSizeImpl(F, PSI, BFI, QueryType);
}

bool llvm::shouldOptimizeForSize(const BasicBlock *BB, ProfileSummaryInfo *PSI,
                                 BlockFrequencyInfo *BFI,
                                 PGSOQueryType QueryType) {
  assert(BB);
  if (BB->getParent()->hasOptSize())
    return true;
  return shouldOptimizeForSizeImpl(BB, PSI, BFI, QueryType, BB->getParent());
}
