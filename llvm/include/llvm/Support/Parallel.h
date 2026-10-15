//===- llvm/Support/Parallel.h - Parallel algorithms ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_PARALLEL_H
#define LLVM_SUPPORT_PARALLEL_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Threading.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

namespace llvm {

namespace parallel {

namespace detail {
class ThreadPoolExecutor;

class Latch {
  std::atomic<uint32_t> Count;
  mutable std::mutex Mutex;
  mutable std::condition_variable Cond;

public:
  explicit Latch(uint32_t Count = 0) : Count(Count) {}
  ~Latch() { assert(Count.load(std::memory_order_relaxed) == 0); }

  void inc() { Count.fetch_add(1, std::memory_order_relaxed); }

  // dec() must hold Mutex so that sync() cannot observe Count==0 and
  // destroy the Latch while dec() is still running.
  void dec() {
    std::lock_guard<std::mutex> lock(Mutex);
    // fetch_sub returns the previous value; == 1 means Count is now 0.
    if (Count.fetch_sub(1, std::memory_order_acq_rel) == 1)
      Cond.notify_all();
  }

  uint32_t getCount() const { return Count.load(std::memory_order_acquire); }

  void sync() const {
    std::unique_lock<std::mutex> lock(Mutex);
    Cond.wait(lock, [&] { return Count.load(std::memory_order_relaxed) == 0; });
  }
};
} // namespace detail

/// A pool of threads for the parallel routines in this file, owned by the
/// caller. This lets a host share one pool between tool invocations running in
/// the same process, each with its own concurrency limit (see ExecutorRef).
/// Nothing may be running on the executor when it is destroyed. A strategy
/// requesting a single thread creates no threads; the routines then run
/// everything on the caller.
class Executor {
#if LLVM_ENABLE_THREADS
  std::unique_ptr<detail::ThreadPoolExecutor> Impl;
#endif
  friend class ExecutorRef;

public:
  LLVM_ABI explicit Executor(ThreadPoolStrategy S = {});
  LLVM_ABI ~Executor();
  Executor(const Executor &) = delete;
  Executor &operator=(const Executor &) = delete;

  LLVM_ABI size_t getThreadCount() const;

  /// Stops the threads without waiting for queued work, which is discarded.
  /// Only for use at process exit.
  LLVM_ABI void stop();
};

/// Selects the executor that a parallel routine runs on, and how many of its
/// threads the routine may use. There is no default executor: a routine runs
/// in parallel only on an Executor that the caller provides.
class ExecutorRef {
  // Null when there is no executor.
  detail::ThreadPoolExecutor *Exec = nullptr;
  // Zero for no limit beyond the size of the pool.
  unsigned MaxThreads = 0;
  // False to run everything on the caller.
  bool Parallel = false;

  friend class TaskGroup;
  ExecutorRef() = default;

public:
  /// Run on \p E, using at most \p Limit.compute_thread_count() of its threads.
  /// A limit that requests a single thread runs everything on the caller.
  LLVM_ABI ExecutorRef(Executor &E, ThreadPoolStrategy Limit = {});

  /// Runs everything on the caller, without any executor.
  static ExecutorRef sequential() { return ExecutorRef(); }

  bool isParallel() const {
#if LLVM_ENABLE_THREADS
    return Parallel;
#else
    return false;
#endif
  }

  /// The number of threads the routines may use: the size of the pool, capped
  /// by the limit.
  LLVM_ABI size_t getThreadCount() const;
};

class TaskGroup {
  detail::Latch L;
  ExecutorRef E;
  bool Parallel;

public:
  LLVM_ABI explicit TaskGroup(ExecutorRef E);
  LLVM_ABI ~TaskGroup();

  // Spawn a task, but does not wait for it to finish.
  LLVM_ABI void spawn(std::function<void()> f);

  bool isParallel() const { return Parallel; }
};

namespace detail {

#if LLVM_ENABLE_THREADS
const ptrdiff_t MinParallelSize = 1024;

/// Inclusive median.
template <class RandomAccessIterator, class Comparator>
RandomAccessIterator medianOf3(RandomAccessIterator Start,
                               RandomAccessIterator End,
                               const Comparator &Comp) {
  RandomAccessIterator Mid = Start + (std::distance(Start, End) / 2);
  return Comp(*Start, *(End - 1))
             ? (Comp(*Mid, *(End - 1)) ? (Comp(*Start, *Mid) ? Mid : Start)
                                       : End - 1)
             : (Comp(*Mid, *Start) ? (Comp(*(End - 1), *Mid) ? Mid : End - 1)
                                   : Start);
}

template <class RandomAccessIterator, class Comparator>
void parallel_quick_sort(RandomAccessIterator Start, RandomAccessIterator End,
                         const Comparator &Comp, TaskGroup &TG, size_t Depth) {
  // Do a sequential sort for small inputs.
  if (std::distance(Start, End) < detail::MinParallelSize || Depth == 0) {
    llvm::sort(Start, End, Comp);
    return;
  }

  // Partition.
  auto Pivot = medianOf3(Start, End, Comp);
  // Move Pivot to End.
  std::swap(*(End - 1), *Pivot);
  Pivot = std::partition(Start, End - 1, [&Comp, End](decltype(*Start) V) {
    return Comp(V, *(End - 1));
  });
  // Move Pivot to middle of partition.
  std::swap(*Pivot, *(End - 1));

  // Recurse.
  TG.spawn([=, &Comp, &TG] {
    parallel_quick_sort(Start, Pivot, Comp, TG, Depth - 1);
  });
  parallel_quick_sort(Pivot + 1, End, Comp, TG, Depth - 1);
}

template <class RandomAccessIterator, class Comparator>
void parallel_sort(ExecutorRef E, RandomAccessIterator Start,
                   RandomAccessIterator End, const Comparator &Comp) {
  TaskGroup TG(E);
  parallel_quick_sort(Start, End, Comp, TG,
                      llvm::Log2_64(std::distance(Start, End)) + 1);
}

// TaskGroup has a relatively high overhead, so we want to reduce
// the number of spawn() calls. We'll create up to 1024 tasks here.
// (Note that 1024 is an arbitrary number. This code probably needs
// improving to take the number of available cores into account.)
enum { MaxTasksPerGroup = 1024 };

template <class IterTy, class ResultTy, class ReduceFuncTy,
          class TransformFuncTy>
ResultTy parallel_transform_reduce(ExecutorRef E, IterTy Begin, IterTy End,
                                   ResultTy Init, ReduceFuncTy Reduce,
                                   TransformFuncTy Transform) {
  // Limit the number of tasks to MaxTasksPerGroup to limit job scheduling
  // overhead on large inputs.
  size_t NumInputs = std::distance(Begin, End);
  if (NumInputs == 0)
    return std::move(Init);
  size_t NumTasks = std::min(static_cast<size_t>(MaxTasksPerGroup), NumInputs);
  std::vector<ResultTy> Results(NumTasks, Init);
  {
    // Each task processes either TaskSize or TaskSize+1 inputs. Any inputs
    // remaining after dividing them equally amongst tasks are distributed as
    // one extra input over the first tasks.
    TaskGroup TG(E);
    size_t TaskSize = NumInputs / NumTasks;
    size_t RemainingInputs = NumInputs % NumTasks;
    IterTy TBegin = Begin;
    for (size_t TaskId = 0; TaskId < NumTasks; ++TaskId) {
      IterTy TEnd = TBegin + TaskSize + (TaskId < RemainingInputs ? 1 : 0);
      TG.spawn([=, &Transform, &Reduce, &Results] {
        // Reduce the result of transformation eagerly within each task.
        ResultTy R = Init;
        for (IterTy It = TBegin; It != TEnd; ++It)
          R = Reduce(R, Transform(*It));
        Results[TaskId] = R;
      });
      TBegin = TEnd;
    }
    assert(TBegin == End);
  }

  // Do a final reduction. There are at most 1024 tasks, so this only adds
  // constant single-threaded overhead for large inputs. Hopefully most
  // reductions are cheaper than the transformation.
  ResultTy FinalResult = std::move(Results.front());
  for (ResultTy &PartialResult :
       MutableArrayRef(Results.data() + 1, Results.size() - 1))
    FinalResult = Reduce(FinalResult, std::move(PartialResult));
  return std::move(FinalResult);
}

#endif

} // namespace detail
} // namespace parallel

template <class RandomAccessIterator,
          class Comparator = std::less<
              typename std::iterator_traits<RandomAccessIterator>::value_type>>
void parallelSort(parallel::ExecutorRef E, RandomAccessIterator Start,
                  RandomAccessIterator End,
                  const Comparator &Comp = Comparator()) {
#if LLVM_ENABLE_THREADS
  if (E.isParallel()) {
    parallel::detail::parallel_sort(E, Start, End, Comp);
    return;
  }
#endif
  llvm::sort(Start, End, Comp);
}

LLVM_ABI void parallelFor(parallel::ExecutorRef E, size_t Begin, size_t End,
                          function_ref<void(size_t)> Fn);

template <class IterTy, class FuncTy>
void parallelForEach(parallel::ExecutorRef E, IterTy Begin, IterTy End,
                     FuncTy Fn) {
  parallelFor(E, 0, End - Begin, [&](size_t I) { Fn(Begin[I]); });
}

template <class IterTy, class ResultTy, class ReduceFuncTy,
          class TransformFuncTy>
ResultTy parallelTransformReduce(parallel::ExecutorRef E, IterTy Begin,
                                 IterTy End, ResultTy Init, ReduceFuncTy Reduce,
                                 TransformFuncTy Transform) {
#if LLVM_ENABLE_THREADS
  if (E.isParallel()) {
    return parallel::detail::parallel_transform_reduce(E, Begin, End, Init,
                                                       Reduce, Transform);
  }
#endif
  for (IterTy I = Begin; I != End; ++I)
    Init = Reduce(std::move(Init), Transform(*I));
  return std::move(Init);
}

// Range wrappers.
template <class RangeTy, class Comparator = std::less<std::remove_reference_t<
                             decltype(*std::begin(std::declval<RangeTy &>()))>>>
void parallelSort(parallel::ExecutorRef E, RangeTy &&R,
                  const Comparator &Comp = Comparator()) {
  parallelSort(E, std::begin(R), std::end(R), Comp);
}

template <class RangeTy, class FuncTy>
void parallelForEach(parallel::ExecutorRef E, RangeTy &&R, FuncTy Fn) {
  parallelForEach(E, std::begin(R), std::end(R), Fn);
}

template <class RangeTy, class ResultTy, class ReduceFuncTy,
          class TransformFuncTy>
ResultTy parallelTransformReduce(parallel::ExecutorRef E, RangeTy &&R,
                                 ResultTy Init, ReduceFuncTy Reduce,
                                 TransformFuncTy Transform) {
  return parallelTransformReduce(E, std::begin(R), std::end(R), Init, Reduce,
                                 Transform);
}

// Parallel for-each, but with error handling.
template <class RangeTy, class FuncTy>
Error parallelForEachError(parallel::ExecutorRef E, RangeTy &&R, FuncTy Fn) {
  // The transform_reduce algorithm requires that the initial value be copyable.
  // Error objects are uncopyable. We only need to copy initial success values,
  // so work around this mismatch via the C API. The C API represents success
  // values with a null pointer. The joinErrors discards null values and joins
  // multiple errors into an ErrorList.
  return unwrap(parallelTransformReduce(
      E, std::begin(R), std::end(R), wrap(Error::success()),
      [](LLVMErrorRef Lhs, LLVMErrorRef Rhs) {
        return wrap(joinErrors(unwrap(Lhs), unwrap(Rhs)));
      },
      [&Fn](auto &&V) { return wrap(Fn(V)); }));
}

} // namespace llvm

#endif // LLVM_SUPPORT_PARALLEL_H
