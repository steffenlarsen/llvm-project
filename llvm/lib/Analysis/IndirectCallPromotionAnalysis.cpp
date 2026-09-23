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
#include "llvm/Analysis/AnalysisOptions.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/Debug.h"

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

static unsigned getICPRemainingPercentThreshold(const AnalysisOptions &Opts) {
  return Opts.AN_ICPRemainingPercentThreshold;
}

static uint64_t getICPTotalPercentThreshold(const AnalysisOptions &Opts) {
  return Opts.AN_ICPTotalPercentThreshold;
}

static unsigned getICPMinimumCountThreshold(const AnalysisOptions &Opts) {
  return Opts.AN_ICPMinimumCountThreshold;
}

static unsigned getMaxNumPromotions(const AnalysisOptions &Opts) {
  return Opts.AN_MaxNumPromotions;
}

bool ICallPromotionAnalysis::isPromotionProfitable(uint64_t Count,
                                                   uint64_t TotalCount,
                                                   uint64_t RemainingCount,
                                                   const Function *F) {
  const AnalysisOptions &Opts =
      F ? F->getContext().getOptions<AnalysisOptions>()
        : AnalysisOptions::Current;
  unsigned MinCount = getICPMinimumCountThreshold(Opts);
  unsigned RemPct = getICPRemainingPercentThreshold(Opts);
  uint64_t TotPct = getICPTotalPercentThreshold(Opts);
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
  const AnalysisOptions &Opts = F.getContext().getOptions<AnalysisOptions>();
  unsigned MinCount = getICPMinimumCountThreshold(Opts);
  unsigned RemPct = getICPRemainingPercentThreshold(Opts);
  uint64_t TotPct = getICPTotalPercentThreshold(Opts);
  uint32_t I = 0;
  uint64_t RemainingCount = TotalCount;
  for (; I < getMaxNumPromotions(Opts) && I < ValueDataArray.size(); I++) {
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
  const AnalysisOptions &Opts = F.getContext().getOptions<AnalysisOptions>();
  if (getMaxNumPromotions(Opts) > MaxNumValueData)
    MaxNumValueData = getMaxNumPromotions(Opts);
  ValueDataArray = getValueProfDataFromInst(*I, IPVK_IndirectCallTarget,
                                            MaxNumValueData, TotalCount);
  if (ValueDataArray.empty()) {
    NumCandidates = 0;
    return MutableArrayRef<InstrProfValueData>();
  }
  NumCandidates = getProfitablePromotionCandidates(I, TotalCount);
  return ValueDataArray;
}
