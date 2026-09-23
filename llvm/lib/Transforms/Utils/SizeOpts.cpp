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
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Transforms/Utils/UtilsOptions.h"

using namespace llvm;

static const UtilsOptions &getUtilsOptions(const LLVMContext *Ctx) {
  return Ctx ? Ctx->getOptions<UtilsOptions>() : UtilsOptions::Current;
}

bool llvm::getEnablePGSO(const LLVMContext *Ctx) {
  return getUtilsOptions(Ctx).TU_EnablePGSO;
}

bool llvm::getForcePGSO(const LLVMContext *Ctx) {
  return getUtilsOptions(Ctx).TU_ForcePGSO;
}

int llvm::getPgsoCutoffInstrProf(const LLVMContext *Ctx) {
  return getUtilsOptions(Ctx).TU_PgsoCutoffInstrProf;
}

int llvm::getPgsoCutoffSampleProf(const LLVMContext *Ctx) {
  return getUtilsOptions(Ctx).TU_PgsoCutoffSampleProf;
}

bool llvm::isPGSOColdCodeOnly(ProfileSummaryInfo *PSI, const LLVMContext *Ctx) {
  const UtilsOptions &Opts = getUtilsOptions(Ctx);
  bool ColdCodeOnly = Opts.TU_PGSOColdCodeOnly;
  bool ColdCodeOnlyForInstrPGO = Opts.TU_PGSOColdCodeOnlyForInstrPGO;
  bool ColdCodeOnlyForSamplePGO = Opts.TU_PGSOColdCodeOnlyForSamplePGO;
  bool ColdCodeOnlyForPartialSamplePGO =
      Opts.TU_PGSOColdCodeOnlyForPartialSamplePGO;
  bool LargeWorkingSetSizeOnly = Opts.TU_PGSOLargeWorkingSetSizeOnly;

  return ColdCodeOnly ||
         (PSI->hasInstrumentationProfile() && ColdCodeOnlyForInstrPGO) ||
         (PSI->hasSampleProfile() &&
          ((!PSI->hasPartialSampleProfile() && ColdCodeOnlyForSamplePGO) ||
           (PSI->hasPartialSampleProfile() &&
            ColdCodeOnlyForPartialSamplePGO))) ||
         (LargeWorkingSetSizeOnly && !PSI->hasLargeWorkingSetSize());
}

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
