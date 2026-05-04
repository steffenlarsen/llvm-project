//=-- ProfilesummaryBuilder.cpp - Profile summary computation ---------------=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for computing profile summary data.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/ProfileSummary.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/ProfileData/ProfileCommon.h"
#include "llvm/ProfileData/ProfileDataOptionsOptInfos.h"
#include "llvm/ProfileData/SampleProf.h"
#include "llvm/Support/OptionsContext.h"

using namespace llvm;

static bool UseContextLessSummary;

bool llvm::getUseContextLessSummary() { return UseContextLessSummary; }

static int getProfileSummaryCutoffHot(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOrDefault<&clv2::PD_ProfileSummaryCutoffHot>(Ctx);
}

static int getProfileSummaryCutoffCold(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOrDefault<&clv2::PD_ProfileSummaryCutoffCold>(Ctx);
}

static bool getUseContextLessSummaryOpt(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOrDefault<&clv2::PD_ProfileSummaryContextless>(Ctx);
}

static bool
getUseContextLessSummaryOptWasSpecified(const clv2::OptionsContext &Ctx) {
  return clv2::wasOptSpecified<&clv2::ProfileDataOptsReg,
                               &clv2::PD_ProfileSummaryContextless>(Ctx);
}

static uint64_t getProfileSummaryHotCountOpt(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOrDefault<&clv2::PD_ProfileSummaryHotCount>(Ctx);
}

static bool
getProfileSummaryHotCountOptWasSpecified(const clv2::OptionsContext &Ctx) {
  return clv2::wasOptSpecified<&clv2::ProfileDataOptsReg,
                               &clv2::PD_ProfileSummaryHotCount>(Ctx);
}

static uint64_t getProfileSummaryColdCountOpt(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOrDefault<&clv2::PD_ProfileSummaryColdCount>(Ctx);
}

static bool
getProfileSummaryColdCountOptWasSpecified(const clv2::OptionsContext &Ctx) {
  return clv2::wasOptSpecified<&clv2::ProfileDataOptsReg,
                               &clv2::PD_ProfileSummaryColdCount>(Ctx);
}

// A set of cutoff values. Each value, when divided by ProfileSummary::Scale
// (which is 1000000) is a desired percentile of total counts.
static const uint32_t DefaultCutoffsData[] = {
    10000,  /*  1% */
    100000, /* 10% */
    200000, 300000, 400000, 500000, 600000, 700000, 800000,
    900000, 950000, 990000, 999000, 999900, 999990, 999999};
const ArrayRef<uint32_t> ProfileSummaryBuilder::DefaultCutoffs =
    DefaultCutoffsData;

// An entry for the 0th percentile to correctly calculate hot/cold count
// thresholds when -profile-summary-cutoff-hot/cold is 0.  If the hot cutoff is
// 0, no sample counts are treated as hot.  If the cold cutoff is 0, all sample
// counts are treated as cold.  Assumes there is no UINT64_MAX sample counts.
static const ProfileSummaryEntry ZeroCutoffEntry = {0, UINT64_MAX, 0};

const ProfileSummaryEntry &
ProfileSummaryBuilder::getEntryForPercentile(const SummaryEntryVector &DS,
                                             uint64_t Percentile) {
  if (Percentile == 0)
    return ZeroCutoffEntry;

  auto It = partition_point(DS, [=](const ProfileSummaryEntry &Entry) {
    return Entry.Cutoff < Percentile;
  });
  // The required percentile has to be <= one of the percentiles in the
  // detailed summary.
  if (It == DS.end())
    report_fatal_error("Desired percentile exceeds the maximum cutoff");
  return *It;
}

void InstrProfSummaryBuilder::addRecord(const InstrProfRecord &R) {
  // The first counter is not necessarily an entry count for IR
  // instrumentation profiles.
  // Eventually MaxFunctionCount will become obsolete and this can be
  // removed.

  if (R.getCountPseudoKind() != InstrProfRecord::NotPseudo)
    return;

  addEntryCount(R.Counts[0]);
  for (size_t I = 1, E = R.Counts.size(); I < E; ++I)
    addInternalCount(R.Counts[I]);
}

// To compute the detailed summary, we consider each line containing samples as
// equivalent to a block with a count in the instrumented profile.
void SampleProfileSummaryBuilder::addRecord(
    const sampleprof::FunctionSamples &FS, bool isCallsiteSample) {
  if (!isCallsiteSample) {
    NumFunctions++;
    if (FS.getHeadSamples() > MaxFunctionCount)
      MaxFunctionCount = FS.getHeadSamples();
  } else if (FS.getContext().hasAttribute(
                 sampleprof::ContextDuplicatedIntoBase)) {
    // Do not recount callee samples if they are already merged into their base
    // profiles. This can happen to CS nested profile.
    return;
  }

  for (const auto &I : FS.getBodySamples()) {
    uint64_t Count = I.second.getSamples();
      addCount(Count);
  }
  for (const auto &I : FS.getCallsiteSamples())
    for (const auto &CS : I.second)
      addRecord(CS.second, true);
}

// The argument to this method is a vector of cutoff percentages and the return
// value is a vector of (Cutoff, MinCount, NumCounts) triplets.
void ProfileSummaryBuilder::computeDetailedSummary() {
  if (DetailedSummaryCutoffs.empty())
    return;
  llvm::sort(DetailedSummaryCutoffs);
  auto Iter = CountFrequencies.begin();
  const auto End = CountFrequencies.end();

  uint32_t CountsSeen = 0;
  uint64_t CurrSum = 0, Count = 0;

  for (const uint32_t Cutoff : DetailedSummaryCutoffs) {
    assert(Cutoff <= 999999);
    APInt Temp(128, TotalCount);
    APInt N(128, Cutoff);
    APInt D(128, ProfileSummary::Scale);
    Temp *= N;
    Temp = Temp.sdiv(D);
    uint64_t DesiredCount = Temp.getZExtValue();
    assert(DesiredCount <= TotalCount);
    while (CurrSum < DesiredCount && Iter != End) {
      Count = Iter->first;
      uint32_t Freq = Iter->second;
      CurrSum += (Count * Freq);
      CountsSeen += Freq;
      Iter++;
    }
    assert(CurrSum >= DesiredCount);
    ProfileSummaryEntry PSE = {Cutoff, Count, CountsSeen};
    DetailedSummary.push_back(PSE);
  }
}

uint64_t
ProfileSummaryBuilder::getHotCountThreshold(const SummaryEntryVector &DS,
                                            const clv2::OptionsContext &Ctx) {
  auto &HotEntry = ProfileSummaryBuilder::getEntryForPercentile(
      DS, getProfileSummaryCutoffHot(Ctx));
  uint64_t HotCountThreshold = HotEntry.MinCount;
  if (getProfileSummaryHotCountOptWasSpecified(Ctx))
    HotCountThreshold = getProfileSummaryHotCountOpt(Ctx);
  return HotCountThreshold;
}

uint64_t
ProfileSummaryBuilder::getColdCountThreshold(const SummaryEntryVector &DS,
                                             const clv2::OptionsContext &Ctx) {
  auto &ColdEntry = ProfileSummaryBuilder::getEntryForPercentile(
      DS, getProfileSummaryCutoffCold(Ctx));
  uint64_t ColdCountThreshold = ColdEntry.MinCount;
  if (getProfileSummaryColdCountOptWasSpecified(Ctx))
    ColdCountThreshold = getProfileSummaryColdCountOpt(Ctx);
  return ColdCountThreshold;
}

std::unique_ptr<ProfileSummary> SampleProfileSummaryBuilder::getSummary() {
  computeDetailedSummary();
  return std::make_unique<ProfileSummary>(
      ProfileSummary::PSK_Sample, DetailedSummary, TotalCount, MaxCount, 0,
      MaxFunctionCount, NumCounts, NumFunctions);
}

std::unique_ptr<ProfileSummary>
SampleProfileSummaryBuilder::computeSummaryForProfiles(
    const SampleProfileMap &Profiles, const clv2::OptionsContext &Ctx) {
  assert(NumFunctions == 0 &&
         "This can only be called on an empty summary builder");
  sampleprof::SampleProfileMap ContextLessProfiles;
  const sampleprof::SampleProfileMap *ProfilesToUse = &Profiles;
  // For CSSPGO, context-sensitive profile effectively split a function profile
  // into many copies each representing the CFG profile of a particular calling
  // context. That makes the count distribution looks more flat as we now have
  // more function profiles each with lower counts, which in turn leads to lower
  // hot thresholds. To compensate for that, by default we merge context
  // profiles before computing profile summary.
  if (getUseContextLessSummaryOpt(Ctx) ||
      (sampleprof::FunctionSamples::ProfileIsCS &&
       !getUseContextLessSummaryOptWasSpecified(Ctx))) {
    ProfileConverter::flattenProfile(Profiles, ContextLessProfiles, true);
    ProfilesToUse = &ContextLessProfiles;
  }

  for (const auto &I : *ProfilesToUse) {
    const sampleprof::FunctionSamples &Profile = I.second;
    addRecord(Profile);
  }

  return getSummary();
}

std::unique_ptr<ProfileSummary> InstrProfSummaryBuilder::getSummary() {
  computeDetailedSummary();
  return std::make_unique<ProfileSummary>(
      ProfileSummary::PSK_Instr, DetailedSummary, TotalCount, MaxCount,
      MaxInternalBlockCount, MaxFunctionCount, NumCounts, NumFunctions);
}

void InstrProfSummaryBuilder::addEntryCount(uint64_t Count) {
  assert(Count <= getInstrMaxCountValue() &&
         "Count value should be less than the max count value.");
  NumFunctions++;
  addCount(Count);
  if (Count > MaxFunctionCount)
    MaxFunctionCount = Count;
}

void InstrProfSummaryBuilder::addInternalCount(uint64_t Count) {
  assert(Count <= getInstrMaxCountValue() &&
         "Count value should be less than the max count value.");
  addCount(Count);
  if (Count > MaxInternalBlockCount)
    MaxInternalBlockCount = Count;
}
