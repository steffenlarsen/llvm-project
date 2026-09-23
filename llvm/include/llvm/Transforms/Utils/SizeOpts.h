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
#include <type_traits>

namespace llvm {

class BasicBlock;
class BlockFrequencyInfo;
class Function;
class LLVMContext;

enum class PGSOQueryType {
  IRPass, // A query call from an IR-level transform pass.
  Test,   // A query call from a unit test.
  Other,  // Others.
};

LLVM_ABI bool getEnablePGSO(const LLVMContext *Ctx);
LLVM_ABI bool getForcePGSO(const LLVMContext *Ctx);
LLVM_ABI int getPgsoCutoffInstrProf(const LLVMContext *Ctx);
LLVM_ABI int getPgsoCutoffSampleProf(const LLVMContext *Ctx);
LLVM_ABI bool isPGSOColdCodeOnly(ProfileSummaryInfo *PSI,
                                 const LLVMContext *Ctx);

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
  const LLVMContext *Ctx = IRF ? &IRF->getContext() : nullptr;
  if (getForcePGSO(Ctx))
    return true;
  if (!getEnablePGSO(Ctx))
    return false;
  if (isPGSOColdCodeOnly(PSI, Ctx))
    return PSI->isFunctionColdInCallGraph(F, *BFI);
  if (PSI->hasSampleProfile())
    return PSI->isFunctionColdInCallGraphNthPercentile(
        getPgsoCutoffSampleProf(Ctx), F, *BFI);
  return !PSI->isFunctionHotInCallGraphNthPercentile(
      getPgsoCutoffInstrProf(Ctx), F, *BFI);
}

template <typename BlockTOrBlockFreq, typename BFIT>
bool shouldOptimizeForSizeImpl(BlockTOrBlockFreq BBOrBlockFreq,
                               ProfileSummaryInfo *PSI, BFIT *BFI,
                               PGSOQueryType QueryType,
                               const Function *F = nullptr) {
  if (!PSI || !BFI || !PSI->hasProfileSummary())
    return false;
  const LLVMContext *Ctx = F ? &F->getContext() : nullptr;
  if (getForcePGSO(Ctx))
    return true;
  if (!getEnablePGSO(Ctx))
    return false;
  if (isPGSOColdCodeOnly(PSI, Ctx))
    return PSI->isColdBlock(BBOrBlockFreq, BFI);
  if (PSI->hasSampleProfile())
    return PSI->isColdBlockNthPercentile(getPgsoCutoffSampleProf(Ctx),
                                         BBOrBlockFreq, BFI);
  return !PSI->isHotBlockNthPercentile(getPgsoCutoffInstrProf(Ctx),
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
