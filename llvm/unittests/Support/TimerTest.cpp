//===- unittests/TimerTest.cpp - Timer tests ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"
#include <atomic>
#include <chrono>
#include <thread>

#if _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

using namespace llvm;

namespace {

// FIXME: Put this somewhere in Support, it's also used in LockFileManager.
void SleepMS() {
#if _WIN32
  Sleep(1);
#else
  struct timespec Interval;
  Interval.tv_sec = 0;
  Interval.tv_nsec = 1000000;
#if defined(__MVS__)
  long Microseconds = (Interval.tv_nsec + 999) / 1000;
  usleep(Microseconds);
#else
  nanosleep(&Interval, nullptr);
#endif
#endif
}

TEST(Timer, Additivity) {
  Timer T1("T1", "T1");

  EXPECT_TRUE(T1.isInitialized());

  T1.startTimer();
  T1.stopTimer();
  auto TR1 = T1.getTotalTime();

  T1.startTimer();
  SleepMS();
  T1.stopTimer();
  auto TR2 = T1.getTotalTime();

  EXPECT_LT(TR1, TR2);
}

TEST(Timer, CheckIfTriggered) {
  Timer T1("T1", "T1");

  EXPECT_FALSE(T1.hasTriggered());
  T1.startTimer();
  EXPECT_TRUE(T1.hasTriggered());
  T1.stopTimer();
  EXPECT_TRUE(T1.hasTriggered());

  T1.clear();
  EXPECT_FALSE(T1.hasTriggered());
}

TEST(Timer, TimerGroupTimerDestructed) {
  testing::internal::CaptureStderr();

  {
    TimerGroup TG("tg", "desc");
    {
      Timer T1("T1", "T1", TG);
      T1.startTimer();
      T1.stopTimer();
    }
    EXPECT_TRUE(testing::internal::GetCapturedStderr().empty());
    testing::internal::CaptureStderr();
  }
  EXPECT_FALSE(testing::internal::GetCapturedStderr().empty());
}

static std::string printRegistry(TimerRegistry &Registry) {
  std::string Out;
  raw_string_ostream OS(Out);
  Registry.print(OS);
  return Out;
}

static unsigned countLinesEndingWith(StringRef Text, StringRef Suffix) {
  SmallVector<StringRef> Lines;
  Text.split(Lines, '\n');
  return count_if(Lines,
                  [&](StringRef Line) { return Line.ends_with(Suffix); });
}

TEST(TimerRegistry, Isolation) {
  TimerRegistry A, B;
  {
    NamedRegionTimer T("t", "Timer A", "g", "Group", &A);
  }
  {
    NamedRegionTimer T("t", "Timer B", "g", "Group", &B);
  }
  EXPECT_NE(&A.getTimer("t", "Timer A", "g", "Group"),
            &B.getTimer("t", "Timer B", "g", "Group"));

  std::string ReportA = printRegistry(A);
  EXPECT_NE(ReportA.find("Timer A"), std::string::npos);
  EXPECT_EQ(ReportA.find("Timer B"), std::string::npos);
  std::string ReportB = printRegistry(B);
  EXPECT_NE(ReportB.find("Timer B"), std::string::npos);
  EXPECT_EQ(ReportB.find("Timer A"), std::string::npos);
}

TEST(TimerRegistry, NullRegistryDoesNotTime) {
  TimerRegistry Registry;
  {
    NamedRegionTimer T("t", "Timer", "g", "Group", nullptr);
  }
  EXPECT_FALSE(Registry.getTimer("t", "Timer", "g", "Group").hasTriggered());
}

// Timers with the same name in different registries can run at the same time,
// unlike those of the process-wide registry.
TEST(TimerRegistry, ConcurrentRegions) {
  TimerRegistry A, B;
  auto Loop = [](TimerRegistry &Registry) {
    for (int I = 0; I < 10000; ++I)
      NamedRegionTimer T("t", "Timer", "g", "Group", &Registry);
  };
  std::thread ThreadA(Loop, std::ref(A)), ThreadB(Loop, std::ref(B));
  ThreadA.join();
  ThreadB.join();
  EXPECT_TRUE(A.getTimer("t", "Timer", "g", "Group").hasTriggered());
  EXPECT_TRUE(B.getTimer("t", "Timer", "g", "Group").hasTriggered());
  A.clear();
  B.clear();
}

TEST(TimerRegistry, MergeSumsByNameAndDescription) {
  TimerRegistry Merged;
  for (int I = 0; I < 2; ++I) {
    TimerRegistry Task;
    {
      NamedRegionTimer T("named", "Named timer", "g", "Group", &Task);
    }
    {
      // A timer destroyed before the merge still contributes its time.
      Timer Destroyed("gone", "Destroyed timer",
                      Task.getTimerGroup("g", "Group"));
      Destroyed.startTimer();
      Destroyed.stopTimer();
    }
    Merged.mergeFrom(Task);
    // The merge moved the data out of Task, so it has nothing to print.
    EXPECT_TRUE(printRegistry(Task).empty());
  }

  std::string Report = printRegistry(Merged);
  EXPECT_EQ(countLinesEndingWith(Report, "Named timer"), 1u);
  EXPECT_EQ(countLinesEndingWith(Report, "Destroyed timer"), 1u);
  // Printing resets the merged data.
  EXPECT_TRUE(printRegistry(Merged).empty());
}

#if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)
// A thread CPU time timer does not count CPU used by other threads.
TEST(TimerRegistry, ThreadCPUTime) {
  TimerGroup ProcessGroup("process", "Process CPU time");
  TimerGroup ThreadGroup("thread", "Thread CPU time");
  ThreadGroup.setThreadCPUTime(true);
  Timer ProcessTimer("p", "p", ProcessGroup);
  Timer ThreadTimer("t", "t", ThreadGroup);

  ProcessTimer.startTimer();
  ThreadTimer.startTimer();
  std::thread Busy([] {
    auto End =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    std::atomic<unsigned> Spins = 0;
    while (std::chrono::steady_clock::now() < End)
      ++Spins;
  });
  Busy.join();
  ThreadTimer.stopTimer();
  ProcessTimer.stopTimer();

  EXPECT_LT(ThreadTimer.getTotalTime().getProcessTime(),
            ProcessTimer.getTotalTime().getProcessTime() / 2);
  ProcessGroup.clear();
  ThreadGroup.clear();
}
#endif

} // namespace
