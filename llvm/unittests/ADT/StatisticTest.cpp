//===- llvm/unittest/ADT/StatisticTest.cpp - Statistic unit tests ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"
#include <atomic>
#include <thread>
using namespace llvm;

using OptionalStatistic = std::optional<std::pair<StringRef, uint64_t>>;

namespace {
#define DEBUG_TYPE "unittest"
STATISTIC(Counter, "Counts things");
STATISTIC(Counter2, "Counts other things");
ALWAYS_ENABLED_STATISTIC(AlwaysCounter, "Counts things always");

#if LLVM_ENABLE_STATS
static void
extractCounters(const std::vector<std::pair<StringRef, uint64_t>> &Range,
                OptionalStatistic &S1, OptionalStatistic &S2) {
  for (const auto &S : Range) {
    if (S.first == "Counter")
      S1 = S;
    if (S.first == "Counter2")
      S2 = S;
  }
}
#endif

TEST(StatisticTest, Count) {
  EnableStatistics();

  Counter = 0;
  EXPECT_EQ(Counter, 0ull);
  Counter++;
  Counter++;
  Counter += (std::numeric_limits<uint64_t>::max() - 3);
#if LLVM_ENABLE_STATS
  EXPECT_EQ(Counter, std::numeric_limits<uint64_t>::max() - 1);
#else
  EXPECT_EQ(Counter, UINT64_C(0));
#endif

  AlwaysCounter = 0;
  EXPECT_EQ(AlwaysCounter, 0ull);
  AlwaysCounter++;
  ++AlwaysCounter;
  EXPECT_EQ(AlwaysCounter, 2ull);
}

TEST(StatisticTest, Assign) {
  EnableStatistics();

  Counter = 2;
#if LLVM_ENABLE_STATS
  EXPECT_EQ(Counter, 2u);
#else
  EXPECT_EQ(Counter, 0u);
#endif

  AlwaysCounter = 2;
  EXPECT_EQ(AlwaysCounter, 2u);
}

TEST(StatisticTest, API) {
  EnableStatistics();
  // Reset beforehand to make sure previous tests don't effect this one.
  ResetStatistics();

  Counter = 0;
  EXPECT_EQ(Counter, 0u);
  Counter++;
  Counter++;
#if LLVM_ENABLE_STATS
  EXPECT_EQ(Counter, 2u);
#else
  EXPECT_EQ(Counter, 0u);
#endif

#if LLVM_ENABLE_STATS
  {
    const auto Range1 = GetStatistics();
    EXPECT_NE(Range1.begin(), Range1.end());
    EXPECT_EQ(Range1.begin() + 1, Range1.end());

    OptionalStatistic S1;
    OptionalStatistic S2;
    extractCounters(Range1, S1, S2);

    EXPECT_EQ(S1.has_value(), true);
    EXPECT_EQ(S2.has_value(), false);
  }

  // Counter2 will be registered when it's first touched.
  Counter2++;

  {
    const auto Range = GetStatistics();
    EXPECT_NE(Range.begin(), Range.end());
    EXPECT_EQ(Range.begin() + 2, Range.end());

    OptionalStatistic S1;
    OptionalStatistic S2;
    extractCounters(Range, S1, S2);

    EXPECT_EQ(S1.has_value(), true);
    EXPECT_EQ(S2.has_value(), true);

    EXPECT_EQ(S1->first, "Counter");
    EXPECT_EQ(S1->second, 2u);

    EXPECT_EQ(S2->first, "Counter2");
    EXPECT_EQ(S2->second, 1u);
  }
#else
  Counter2++;
  auto Range = GetStatistics();
  EXPECT_EQ(Range.begin(), Range.end());
#endif

#if LLVM_ENABLE_STATS
  // Check that resetting the statistics works correctly.
  // It should empty the list and zero the counters.
  ResetStatistics();
  {
    auto Range = GetStatistics();
    EXPECT_EQ(Range.begin(), Range.end());
    EXPECT_EQ(Counter, 0u);
    EXPECT_EQ(Counter2, 0u);
    OptionalStatistic S1;
    OptionalStatistic S2;
    extractCounters(Range, S1, S2);
    EXPECT_EQ(S1.has_value(), false);
    EXPECT_EQ(S2.has_value(), false);
  }

  // Now check that they successfully re-register and count.
  Counter++;
  Counter2++;

  {
    auto Range = GetStatistics();
    EXPECT_EQ(Range.begin() + 2, Range.end());
    EXPECT_EQ(Counter, 1u);
    EXPECT_EQ(Counter2, 1u);

    OptionalStatistic S1;
    OptionalStatistic S2;
    extractCounters(Range, S1, S2);

    EXPECT_EQ(S1.has_value(), true);
    EXPECT_EQ(S2.has_value(), true);

    EXPECT_EQ(S1->first, "Counter");
    EXPECT_EQ(S1->second, 1u);

    EXPECT_EQ(S2->first, "Counter2");
    EXPECT_EQ(S2->second, 1u);
  }
#else
  // No need to test the output ResetStatistics(), there's nothing to reset so
  // we can't tell if it failed anyway.
  ResetStatistics();
#endif
}

// The JSON only includes the timers of the given groups. It does not touch the
// timers of other groups, which may belong to other compilations.
TEST(StatisticTest, JSONTimerGroups) {
  TimerGroup Mine("mine", "My timers", /*PrintOnExit=*/false);
  TimerGroup Other("other", "Other timers", /*PrintOnExit=*/false);
  Timer MyTimer("mytimer", "My timer", Mine);
  Timer OtherTimer("othertimer", "Other timer", Other);
  MyTimer.startTimer();
  MyTimer.stopTimer();
  OtherTimer.startTimer();

  std::string JSON;
  raw_string_ostream OS(JSON);
  PrintStatisticsJSON(OS, {&Mine});
  EXPECT_NE(JSON.find("time.mine.mytimer.wall"), std::string::npos);
  EXPECT_EQ(JSON.find("othertimer"), std::string::npos);
  EXPECT_TRUE(OtherTimer.isRunning());
  OtherTimer.stopTimer();
}

// Printing the statistics of one compilation is safe while another compilation
// uses its timers on another thread.
TEST(StatisticTest, JSONTimerGroupsConcurrent) {
  TimerGroup Mine("mine", "My timers", /*PrintOnExit=*/false);
  TimerGroup Other("other", "Other timers", /*PrintOnExit=*/false);
  Timer MyTimer("mytimer", "My timer", Mine);
  MyTimer.startTimer();
  MyTimer.stopTimer();

  std::atomic<bool> Done = false;
  std::thread OtherCompilation([&] {
    Timer OtherTimer("othertimer", "Other timer", Other);
    while (!Done) {
      OtherTimer.startTimer();
      OtherTimer.stopTimer();
    }
  });
  for (int I = 0; I < 1000; ++I) {
    std::string JSON;
    raw_string_ostream OS(JSON);
    PrintStatisticsJSON(OS, {&Mine});
    EXPECT_EQ(JSON.find("othertimer"), std::string::npos);
  }
  Done = true;
  OtherCompilation.join();
}

} // end anonymous namespace
