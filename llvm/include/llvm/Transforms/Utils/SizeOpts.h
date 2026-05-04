//===- llvm/Transforms/Utils/SizeOpts.h - size optimization -----*- C++ -*-===//
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

#ifndef LLVM_TRANSFORMS_UTILS_SIZEOPTS_H
#define LLVM_TRANSFORMS_UTILS_SIZEOPTS_H

#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"
#include <type_traits>

namespace llvm {

namespace clv2 {
class OptionsContext;
}

class BasicBlock;
class BlockFrequencyInfo;
class Function;

enum class PGSOQueryType {
  IRPass, // A query call from an IR-level transform pass.
  Test,   // A query call from a unit test.
  Other,  // Others.
};

LLVM_ABI bool getEnablePGSO(const clv2::OptionsContext &Ctx);
LLVM_ABI bool getForcePGSO(const clv2::OptionsContext &Ctx);
LLVM_ABI int getPgsoCutoffInstrProf(const clv2::OptionsContext &Ctx);
LLVM_ABI int getPgsoCutoffSampleProf(const clv2::OptionsContext &Ctx);
LLVM_ABI bool isPGSOColdCodeOnly(ProfileSummaryInfo *PSI,
                                 const clv2::OptionsContext &Ctx);

/// Helper to extract a Function* from either a Function* or a type that
/// provides getFunction() (e.g. MachineFunction).
template <typename T> inline const Function *extractFunction(const T *V) {
  if constexpr (std::is_same_v<T, Function>)
    return V;
  else
    return &V->getFunction();
}

template <typename FuncT, typename BFIT>
bool shouldFuncOptimizeForSizeImpl(const FuncT *F, ProfileSummaryInfo *PSI,
                                   BFIT *BFI, PGSOQueryType QueryType) {
  assert(F);
  if (!PSI || !BFI || !PSI->hasProfileSummary())
    return false;
  const Function *IRF = extractFunction(F);
  if (getForcePGSO(IRF ? IRF->getContext().getOptionsContext()
                       : clv2::defaultOptionsContext()))
    return true;
  if (!getEnablePGSO(IRF ? IRF->getContext().getOptionsContext()
                         : clv2::defaultOptionsContext()))
    return false;
  if (isPGSOColdCodeOnly(PSI, IRF ? IRF->getContext().getOptionsContext()
                                  : clv2::defaultOptionsContext()))
    return PSI->isFunctionColdInCallGraph(F, *BFI);
  if (PSI->hasSampleProfile())
    return PSI->isFunctionColdInCallGraphNthPercentile(
        getPgsoCutoffSampleProf(IRF ? IRF->getContext().getOptionsContext()

                                    : clv2::defaultOptionsContext()),
        F, *BFI);
  return !PSI->isFunctionHotInCallGraphNthPercentile(
      getPgsoCutoffInstrProf(IRF ? IRF->getContext().getOptionsContext()

                                 : clv2::defaultOptionsContext()),
      F, *BFI);
}

template <typename BlockTOrBlockFreq, typename BFIT>
bool shouldOptimizeForSizeImpl(BlockTOrBlockFreq BBOrBlockFreq,
                               ProfileSummaryInfo *PSI, BFIT *BFI,
                               PGSOQueryType QueryType,
                               const Function *F = nullptr) {
  if (!PSI || !BFI || !PSI->hasProfileSummary())
    return false;
  if (getForcePGSO(F ? F->getContext().getOptionsContext()
                     : clv2::defaultOptionsContext()))
    return true;
  if (!getEnablePGSO(F ? F->getContext().getOptionsContext()
                       : clv2::defaultOptionsContext()))
    return false;
  if (isPGSOColdCodeOnly(PSI, F ? F->getContext().getOptionsContext()
                                : clv2::defaultOptionsContext()))
    return PSI->isColdBlock(BBOrBlockFreq, BFI);
  if (PSI->hasSampleProfile())
    return PSI->isColdBlockNthPercentile(
        getPgsoCutoffSampleProf(F ? F->getContext().getOptionsContext()

                                  : clv2::defaultOptionsContext()),
        BBOrBlockFreq, BFI);
  return !PSI->isHotBlockNthPercentile(
      getPgsoCutoffInstrProf(F ? F->getContext().getOptionsContext()
                               : clv2::defaultOptionsContext()),
      BBOrBlockFreq, BFI);
}

/// Returns true if function \p F is suggested to be size-optimized based on the
/// profile.
LLVM_ABI bool
shouldOptimizeForSize(const Function *F, ProfileSummaryInfo *PSI,
                      BlockFrequencyInfo *BFI,
                      PGSOQueryType QueryType = PGSOQueryType::Other);

/// Returns true if basic block \p BB is suggested to be size-optimized based on
/// the profile.
LLVM_ABI bool
shouldOptimizeForSize(const BasicBlock *BB, ProfileSummaryInfo *PSI,
                      BlockFrequencyInfo *BFI,
                      PGSOQueryType QueryType = PGSOQueryType::Other);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_SIZEOPTS_H
