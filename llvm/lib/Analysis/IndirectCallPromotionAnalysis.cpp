//===-- IndirectCallPromotionAnalysis.cpp - Find promotion candidates ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Helper methods for identifying profitable indirect call promotion
// candidates for an instruction when the indirect-call value profile metadata
// is available.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/IndirectCallPromotionAnalysis.h"
#include "llvm/Analysis/AnalysisOptionsOptInfos.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/OptionsContext.h"

using namespace llvm;

#define DEBUG_TYPE "pgo-icall-prom-analysis"

namespace llvm {

// The percent threshold for the direct-call target (this call site vs the
// remaining call count) for it to be considered as the promotion target.

// The percent threshold for the direct-call target (this call site vs the
// total call count) for it to be considered as the promotion target.

// Set the minimum absolute count threshold for indirect call promotion.
// Candidates with counts below this threshold will not be promoted.

// Set the maximum number of targets to promote for a single indirect-call
// callsite.


} // end namespace llvm

static unsigned getICPRemainingPercentThreshold(const Function &F) {
  return clv2::getOptValOrDefault<&clv2::AN_ICPRemainingPercentThreshold>(
      F.getContext().getOptionsContext());
}

static uint64_t getICPTotalPercentThreshold(const Function &F) {
  return clv2::getOptValOrDefault<&clv2::AN_ICPTotalPercentThreshold>(
      F.getContext().getOptionsContext());
}

static unsigned getICPMinimumCountThreshold(const Function &F) {
  return clv2::getOptValOrDefault<&clv2::AN_ICPMinimumCountThreshold>(
      F.getContext().getOptionsContext());
}

static unsigned getMaxNumPromotions(const Function &F) {
  return clv2::getOptValOrDefault<&clv2::AN_MaxNumPromotions>(
      F.getContext().getOptionsContext());
}

// Overloads with optional OptionsContext, using single-path.
static unsigned
getICPRemainingPercentThreshold(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOrDefault<&clv2::AN_ICPRemainingPercentThreshold>(Ctx);
}

static uint64_t getICPTotalPercentThreshold(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOrDefault<&clv2::AN_ICPTotalPercentThreshold>(Ctx);
}

static unsigned getICPMinimumCountThreshold(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOrDefault<&clv2::AN_ICPMinimumCountThreshold>(Ctx);
}

bool ICallPromotionAnalysis::isPromotionProfitable(uint64_t Count,
                                                   uint64_t TotalCount,
                                                   uint64_t RemainingCount,
                                                   const Function *F) {
  unsigned MinCount = getICPMinimumCountThreshold(
      F ? F->getContext().getOptionsContext() : clv2::defaultOptionsContext());
  unsigned RemPct = getICPRemainingPercentThreshold(
      F ? F->getContext().getOptionsContext() : clv2::defaultOptionsContext());
  uint64_t TotPct = getICPTotalPercentThreshold(
      F ? F->getContext().getOptionsContext() : clv2::defaultOptionsContext());
  return Count >= MinCount && Count * 100 >= RemPct * RemainingCount &&
         Count * 100 >= TotPct * TotalCount;
}

// Indirect-call promotion heuristic. The direct targets are sorted based on
// the count. Stop at the first target that is not promoted. Returns the
// number of candidates deemed profitable.
uint32_t ICallPromotionAnalysis::getProfitablePromotionCandidates(
    const Instruction *Inst, uint64_t TotalCount) {
  LLVM_DEBUG(dbgs() << " \nWork on callsite " << *Inst
                    << " Num_targets: " << ValueDataArray.size() << "\n");

  const Function &F = *Inst->getFunction();
  unsigned MinCount = getICPMinimumCountThreshold(F);
  unsigned RemPct = getICPRemainingPercentThreshold(F);
  uint64_t TotPct = getICPTotalPercentThreshold(F);
  uint32_t I = 0;
  uint64_t RemainingCount = TotalCount;
  for (; I < getMaxNumPromotions(F) && I < ValueDataArray.size(); I++) {
    uint64_t Count = ValueDataArray[I].Count;
    assert(Count <= RemainingCount);
    LLVM_DEBUG(dbgs() << " Candidate " << I << " Count=" << Count
                      << "  Target_func: " << ValueDataArray[I].Value << "\n");

    bool Profitable = Count >= MinCount &&
                      Count * 100 >= RemPct * RemainingCount &&
                      Count * 100 >= TotPct * TotalCount;
    if (!Profitable) {
      LLVM_DEBUG(dbgs() << " Not promote: Cold target.\n");
      return I;
    }
    RemainingCount -= Count;
  }
  return I;
}

MutableArrayRef<InstrProfValueData>
ICallPromotionAnalysis::getPromotionCandidatesForInstruction(
    const Instruction *I, uint64_t &TotalCount, uint32_t &NumCandidates,
    unsigned MaxNumValueData) {
  // Use the max of the values specified by -icp-max-prom and the provided
  // MaxNumValueData parameter.
  const Function &F = *I->getFunction();
  if (getMaxNumPromotions(F) > MaxNumValueData)
    MaxNumValueData = getMaxNumPromotions(F);
  ValueDataArray = getValueProfDataFromInst(*I, IPVK_IndirectCallTarget,
                                            MaxNumValueData, TotalCount);
  if (ValueDataArray.empty()) {
    NumCandidates = 0;
    return MutableArrayRef<InstrProfValueData>();
  }
  NumCandidates = getProfitablePromotionCandidates(I, TotalCount);
  return ValueDataArray;
}
