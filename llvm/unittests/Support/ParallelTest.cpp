//===- llvm/unittest/Support/ParallelTest.cpp -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Parallel.h unit tests.
///
//===----------------------------------------------------------------------===//

#include "llvm/Support/Parallel.h"
#include "llvm/Config/llvm-config.h" // for LLVM_ENABLE_THREADS
#include "llvm/Support/ThreadPool.h"
#include "gtest/gtest.h"
#include <future>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <thread>

uint32_t array[1024 * 1024];

using namespace llvm;

// Tests below are hanging up on mingw. Investigating.
#if !defined(__MINGW32__)

TEST(Parallel, sort) {
  std::mt19937 randEngine;
  std::uniform_int_distribution<uint32_t> dist;

  for (auto &i : array)
    i = dist(randEngine);

  parallel::Executor Exec;
  parallelSort(Exec, std::begin(array), std::end(array));
  ASSERT_TRUE(llvm::is_sorted(array));
}

TEST(Parallel, parallel_for) {
  // We need to test the case with a TaskSize > 1. We are white-box testing
  // here. The TaskSize is calculated as (End - Begin) / 1024 at the time of
  // writing.
  uint32_t range[2050];
  std::fill(range, range + 2050, 1);
  parallel::Executor Exec;
  parallelFor(Exec, 0, 2049, [&range](size_t I) { ++range[I]; });

  uint32_t expected[2049];
  std::fill(expected, expected + 2049, 2);
  ASSERT_TRUE(std::equal(range, range + 2049, expected));
  // Check that we don't write past the end of the requested range.
  ASSERT_EQ(range[2049], 1u);
}

TEST(Parallel, TransformReduce) {
  parallel::Executor Exec;
  // Sum an empty list, check that it works.
  auto identity = [](uint32_t v) { return v; };
  uint32_t sum = parallelTransformReduce(Exec, ArrayRef<uint32_t>(), 0U,
                                         std::plus<uint32_t>(), identity);
  EXPECT_EQ(sum, 0U);

  // Sum the lengths of these strings in parallel.
  const char *strs[] = {"a", "ab", "abc", "abcd", "abcde", "abcdef"};
  size_t lenSum = parallelTransformReduce(
      Exec, strs, static_cast<size_t>(0), std::plus<size_t>(),
      [](const char *s) { return strlen(s); });
  EXPECT_EQ(lenSum, static_cast<size_t>(21));

  // Check that we handle non-divisible task sizes as above.
  uint32_t range[2050];
  llvm::fill(range, 1);
  sum =
      parallelTransformReduce(Exec, range, 0U, std::plus<uint32_t>(), identity);
  EXPECT_EQ(sum, 2050U);

  llvm::fill(range, 2);
  sum =
      parallelTransformReduce(Exec, range, 0U, std::plus<uint32_t>(), identity);
  EXPECT_EQ(sum, 4100U);

  // Avoid one large task.
  uint32_t range2[3060];
  llvm::fill(range2, 1);
  sum = parallelTransformReduce(Exec, range2, 0U, std::plus<uint32_t>(),
                                identity);
  EXPECT_EQ(sum, 3060U);
}

TEST(Parallel, ForEachError) {
  int nums[] = {1, 2, 3, 4, 5, 6};
  parallel::Executor Exec;
  Error e = parallelForEachError(Exec, nums, [](int v) -> Error {
    if ((v & 1) == 0)
      return createStringError(std::errc::invalid_argument, "asdf");
    return Error::success();
  });
  EXPECT_TRUE(e.isA<ErrorList>());
  std::string errText = toString(std::move(e));
  EXPECT_EQ(errText, std::string("asdf\nasdf\nasdf"));
}

// Checks that every item of a parallelFor on \p Ref runs on the caller.
static void expectRunsOnCaller(parallel::ExecutorRef Ref) {
  EXPECT_FALSE(Ref.isParallel());
  EXPECT_EQ(Ref.getThreadCount(), 1u);
  EXPECT_FALSE(parallel::TaskGroup(Ref).isParallel());
  std::thread::id Caller = std::this_thread::get_id();
  std::atomic<bool> OffCaller{false};
  parallelFor(Ref, 0, 1000, [&](size_t) {
    if (std::this_thread::get_id() != Caller)
      OffCaller = true;
  });
  EXPECT_FALSE(OffCaller);
}

TEST(Parallel, Sequential) {
  expectRunsOnCaller(parallel::ExecutorRef::sequential());
}

TEST(Parallel, SingleThreadExecutor) {
  parallel::Executor Exec(hardware_concurrency(1));
  EXPECT_EQ(Exec.getThreadCount(), 1u);
  expectRunsOnCaller(Exec);
}

#if LLVM_ENABLE_THREADS
TEST(Parallel, NestedTaskGroup) {
  parallel::Executor Exec;
  parallel::TaskGroup tg(Exec);
  EXPECT_TRUE(tg.isParallel());

  tg.spawn([&]() {
    parallel::TaskGroup nestedTG(Exec);
    EXPECT_TRUE(nestedTG.isParallel());
  });
}

// Verify nested parallelFor doesn't deadlock. This is a simplified version of
// the pattern from https://reviews.llvm.org/D61115 that originally motivated
// serializing nested TaskGroups. With work-stealing in helpSync(), nested
// parallelism now works without deadlock.
TEST(Parallel, NestedParallelFor) {
  parallel::Executor Exec;
  std::atomic<uint32_t> count{0};
  parallelFor(Exec, 0, 8, [&](size_t i) {
    parallelFor(Exec, 0, 8, [&](size_t j) {
      parallelFor(Exec, 0, 8, [&](size_t k) {
        count.fetch_add(1, std::memory_order_relaxed);
      });
    });
  });
  EXPECT_EQ(count.load(), 512u);
}

TEST(Parallel, Executor) {
  parallel::Executor Exec(hardware_concurrency(4));
  EXPECT_EQ(Exec.getThreadCount(), 4u);
  parallel::ExecutorRef Ref(Exec);
  EXPECT_TRUE(Ref.isParallel());
  EXPECT_EQ(Ref.getThreadCount(), 4u);

  uint32_t range[2050];
  llvm::fill(range, 1);
  parallelFor(Ref, 0, 2049, [&range](size_t I) { ++range[I]; });
  EXPECT_TRUE(
      std::all_of(range, range + 2049, [](uint32_t V) { return V == 2; }));
  EXPECT_EQ(range[2049], 1u);

  std::vector<uint32_t> V(100000);
  std::mt19937 randEngine;
  std::uniform_int_distribution<uint32_t> dist;
  for (uint32_t &I : V)
    I = dist(randEngine);
  parallelSort(Ref, V);
  EXPECT_TRUE(llvm::is_sorted(V));
  parallelSort(Ref, V.begin(), V.end(), std::greater<uint32_t>());
  EXPECT_TRUE(llvm::is_sorted(V, std::greater<uint32_t>()));

  std::atomic<uint64_t> Sum{0};
  parallelForEach(Ref, V, [&](uint32_t I) { Sum += I % 7; });
  parallelForEach(Ref, V.begin(), V.end(), [&](uint32_t I) { Sum -= I % 7; });
  EXPECT_EQ(Sum.load(), 0u);

  auto identity = [](uint32_t v) { return v; };
  EXPECT_EQ(
      parallelTransformReduce(Ref, range, 0U, std::plus<uint32_t>(), identity),
      2 * 2049U + 1);

  int nums[] = {1, 2, 3, 4, 5, 6};
  Error e = parallelForEachError(Ref, nums, [](int v) -> Error {
    if ((v & 1) == 0)
      return createStringError(std::errc::invalid_argument, "asdf");
    return Error::success();
  });
  EXPECT_EQ(toString(std::move(e)), std::string("asdf\nasdf\nasdf"));
}

#if GTEST_HAS_DEATH_TEST && !defined(NDEBUG)
TEST(Parallel, ExecutorDestroyedWithQueuedWork) {
  EXPECT_DEATH(
      {
        auto Exec =
            std::make_unique<parallel::Executor>(hardware_concurrency(2));
        std::promise<void> Never;
        std::shared_future<void> Blocked = Never.get_future().share();
        // Leaked, so that its destructor does not wait for the work.
        auto *TG = new parallel::TaskGroup(*Exec);
        // Occupy both threads, and keep one more task queued.
        for (int I = 0; I < 3; ++I)
          TG->spawn([Blocked] { Blocked.wait(); });
        Exec.reset();
      },
      "executor destroyed with work queued");
}
#endif

TEST(Parallel, ExecutorLimit) {
  parallel::Executor Exec(hardware_concurrency(4));

  // A limit of one thread runs everything on the caller.
  parallel::ExecutorRef Serial(Exec, hardware_concurrency(1));
  EXPECT_FALSE(Serial.isParallel());
  EXPECT_EQ(Serial.getThreadCount(), 1u);
  EXPECT_FALSE(parallel::TaskGroup(Serial).isParallel());
  std::thread::id Caller = std::this_thread::get_id();
  std::atomic<bool> OffCaller{false};
  parallelFor(Serial, 0, 1000, [&](size_t) {
    if (std::this_thread::get_id() != Caller)
      OffCaller = true;
  });
  EXPECT_FALSE(OffCaller);

  // parallelFor uses at most the limit's number of threads.
  parallel::ExecutorRef Two(Exec, hardware_concurrency(2));
  EXPECT_TRUE(Two.isParallel());
  EXPECT_EQ(Two.getThreadCount(), 2u);
  std::mutex M;
  std::set<std::thread::id> Ids;
  parallelFor(Two, 0, 1000, [&](size_t) {
    std::lock_guard<std::mutex> Lock(M);
    Ids.insert(std::this_thread::get_id());
  });
  EXPECT_LE(Ids.size(), 2u);

  // The limit cannot exceed the size of the pool.
  EXPECT_EQ(
      parallel::ExecutorRef(Exec, hardware_concurrency(8)).getThreadCount(),
      4u);
}

TEST(Parallel, ExecutorNestedParallelFor) {
  parallel::Executor Exec(hardware_concurrency(3));
  parallel::ExecutorRef Ref(Exec);
  std::atomic<uint32_t> count{0};
  parallelFor(Ref, 0, 8, [&](size_t i) {
    parallelFor(Ref, 0, 8, [&](size_t j) {
      parallelFor(Ref, 0, 8, [&](size_t k) {
        count.fetch_add(1, std::memory_order_relaxed);
      });
    });
  });
  EXPECT_EQ(count.load(), 512u);
}

TEST(Parallel, ExecutorsConcurrently) {
  parallel::Executor A(hardware_concurrency(2));
  parallel::Executor B(hardware_concurrency(3));
  auto Run = [](parallel::ExecutorRef Ref) {
    std::atomic<uint64_t> Sum{0};
    for (int R = 0; R < 50; ++R)
      parallelFor(Ref, 0, 1000, [&](size_t I) { Sum += I; });
    return Sum.load();
  };
  uint64_t SumA = 0, SumB = 0;
  std::thread TA([&] { SumA = Run(A); });
  std::thread TB([&] { SumB = Run(B); });
  TA.join();
  TB.join();
  EXPECT_EQ(SumA, 50u * 499500u);
  EXPECT_EQ(SumB, 50u * 499500u);
}
#endif

#endif
