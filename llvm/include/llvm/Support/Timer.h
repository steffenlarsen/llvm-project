//===-- llvm/Support/Timer.h - Interval Timing Support ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_TIMER_H
#define LLVM_SUPPORT_TIMER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Mutex.h"
#include <cassert>
#include <memory>
#include <string>
#include <vector>

namespace llvm {

class TimerGlobals;
class TimerGroup;
class TimerRegistry;
class raw_ostream;

class TimeRecord {
  double WallTime = 0.0;             ///< Wall clock time elapsed in seconds.
  double UserTime = 0.0;             ///< User time elapsed.
  double SystemTime = 0.0;           ///< System time elapsed.
  ssize_t MemUsed = 0;               ///< Memory allocated (in bytes).
  uint64_t InstructionsExecuted = 0; ///< Number of instructions executed

  static TimeRecord getCurrentTimeImpl(bool Start, bool ThreadCPUTime);

public:
  TimeRecord() = default;

  /// Get the current time and memory usage.  If Start is true we get the memory
  /// usage before the time, otherwise we get time before memory usage.  This
  /// matters if the time to get the memory usage is significant and shouldn't
  /// be counted as part of a duration.
  LLVM_ABI static TimeRecord getCurrentTime(bool Start = true);

  /// Like getCurrentTime, but the user and system times only count the calling
  /// thread. Memory usage and executed instructions are still process-wide.
  LLVM_ABI static TimeRecord getCurrentThreadTime(bool Start = true);

  double getProcessTime() const { return UserTime + SystemTime; }
  double getUserTime() const { return UserTime; }
  double getSystemTime() const { return SystemTime; }
  double getWallTime() const { return WallTime; }
  ssize_t getMemUsed() const { return MemUsed; }
  uint64_t getInstructionsExecuted() const { return InstructionsExecuted; }

  bool operator<(const TimeRecord &T) const {
    // Sort by Wall Time elapsed, as it is the only thing really accurate
    return WallTime < T.WallTime;
  }

  void operator+=(const TimeRecord &RHS) {
    WallTime += RHS.WallTime;
    UserTime += RHS.UserTime;
    SystemTime += RHS.SystemTime;
    MemUsed += RHS.MemUsed;
    InstructionsExecuted += RHS.InstructionsExecuted;
  }
  void operator-=(const TimeRecord &RHS) {
    WallTime -= RHS.WallTime;
    UserTime -= RHS.UserTime;
    SystemTime -= RHS.SystemTime;
    MemUsed -= RHS.MemUsed;
    InstructionsExecuted -= RHS.InstructionsExecuted;
  }
  TimeRecord operator-(const TimeRecord &RHS) const {
    TimeRecord R = *this;
    R -= RHS;
    return R;
  }
  // Feel free to add operator+ if you need it

  /// Print the current time record to \p OS, with a breakdown showing
  /// contributions to the \p Total time record.
  LLVM_ABI void print(const TimeRecord &Total, raw_ostream &OS) const;
};

/// This class is used to track the amount of time spent between invocations of
/// its startTimer()/stopTimer() methods.  Given appropriate OS support it can
/// also keep track of the RSS of the program at various points.  By default,
/// the Timer will print the amount of time it has captured to standard error
/// when the last timer is destroyed, otherwise it is printed when its
/// TimerGroup is destroyed.  Timers do not print their information if they are
/// never started.
class Timer {
  TimeRecord Time;          ///< The total time captured.
  TimeRecord StartTime;     ///< The time startTimer() was last called.
  std::string Name;         ///< The name of this time variable.
  std::string Description;  ///< Description of this time variable.
  bool Running = false;     ///< Is the timer currently running?
  bool Triggered = false;   ///< Has the timer ever been triggered?
  TimerGroup *TG = nullptr; ///< The TimerGroup this Timer is in.

  Timer **Prev = nullptr;   ///< Pointer to \p Next of previous timer in group.
  Timer *Next = nullptr;    ///< Next timer in the group.
public:
  explicit Timer(StringRef TimerName, StringRef TimerDescription) {
    init(TimerName, TimerDescription);
  }
  Timer(StringRef TimerName, StringRef TimerDescription, TimerGroup &tg) {
    init(TimerName, TimerDescription, tg);
  }
  Timer(const Timer &RHS) {
    assert(!RHS.TG && "Can only copy uninitialized timers");
  }
  const Timer &operator=(const Timer &T) {
    assert(!TG && !T.TG && "Can only assign uninit timers");
    return *this;
  }
  LLVM_ABI ~Timer();

  /// Create an uninitialized timer, client must use 'init'.
  explicit Timer() = default;
  LLVM_ABI void init(StringRef TimerName, StringRef TimerDescription);
  LLVM_ABI void init(StringRef TimerName, StringRef TimerDescription,
                     TimerGroup &tg);

  const std::string &getName() const { return Name; }
  const std::string &getDescription() const { return Description; }
  bool isInitialized() const { return TG != nullptr; }

  /// Check if the timer is currently running.
  bool isRunning() const { return Running; }

  /// Check if startTimer() has ever been called on this timer.
  bool hasTriggered() const { return Triggered; }

  /// Start the timer running.  Time between calls to startTimer/stopTimer is
  /// counted by the Timer class.  Note that these calls must be correctly
  /// paired.
  LLVM_ABI void startTimer();

  /// Stop the timer.
  LLVM_ABI void stopTimer();

  /// Clear the timer state.
  LLVM_ABI void clear();

  /// Stop the timer and start another timer.
  LLVM_ABI void yieldTo(Timer &);

  /// Return the duration for which this timer has been running.
  TimeRecord getTotalTime() const { return Time; }

private:
  friend class TimerGroup;
};

/// The TimeRegion class is used as a helper class to call the startTimer() and
/// stopTimer() methods of the Timer class.  When the object is constructed, it
/// starts the timer specified as its argument.  When it is destroyed, it stops
/// the relevant timer.  This makes it easy to time a region of code.
class TimeRegion {
  Timer *T;
  TimeRegion(const TimeRegion &) = delete;

public:
  explicit TimeRegion(Timer &t) : T(&t) {
    T->startTimer();
  }
  explicit TimeRegion(Timer *t) : T(t) {
    if (T) T->startTimer();
  }
  ~TimeRegion() {
    if (T) T->stopTimer();
  }
};

/// This class is basically a combination of TimeRegion and Timer.  It allows
/// you to declare a new timer, AND specify the region to time, all in one
/// statement.  All timers with the same name are merged.  This is primarily
/// used for debugging and for hunting performance problems.
struct NamedRegionTimer : TimeRegion {
  /// Time the region with a timer from the process-wide registry, which is
  /// shared by all threads.
  LLVM_ABI explicit NamedRegionTimer(StringRef Name, StringRef Description,
                                     StringRef GroupName,
                                     StringRef GroupDescription,
                                     bool Enabled = true);

  /// Time the region with a timer from \p Registry. The region is not timed
  /// if \p Registry is null.
  LLVM_ABI explicit NamedRegionTimer(StringRef Name, StringRef Description,
                                     StringRef GroupName,
                                     StringRef GroupDescription,
                                     TimerRegistry *Registry);

  // Create or get a TimerGroup stored in the same global map owned by
  // NamedRegionTimer.
  LLVM_ABI static TimerGroup &getNamedTimerGroup(StringRef GroupName,
                                                 StringRef GroupDescription);
};

/// The TimerGroup class is used to group together related timers into a single
/// report that is printed when the TimerGroup is destroyed.  It is illegal to
/// destroy a TimerGroup object before all of the Timers in it are gone.  A
/// TimerGroup can be specified for a newly created timer in its constructor.
class TimerGroup {
  struct PrintRecord {
    TimeRecord Time;
    std::string Name;
    std::string Description;

    PrintRecord(const PrintRecord &Other) = default;
    PrintRecord &operator=(const PrintRecord &Other) = default;
    PrintRecord(const TimeRecord &Time, const std::string &Name,
                const std::string &Description)
      : Time(Time), Name(Name), Description(Description) {}

    bool operator <(const PrintRecord &Other) const {
      return Time < Other.Time;
    }
  };
  std::string Name;
  std::string Description;
  Timer *FirstTimer = nullptr; ///< First timer in the group.
  std::vector<PrintRecord> TimersToPrint;
  bool PrintOnExit;
  bool ThreadCPUTime = false; ///< Do timers only count their thread's CPU?

  TimerGroup **Prev; ///< Pointer to Next field of previous timergroup in list.
  TimerGroup *Next;  ///< Pointer to next timergroup in list.
  TimerGroup(const TimerGroup &TG) = delete;
  void operator=(const TimerGroup &TG) = delete;

  friend class TimerGlobals;
  explicit TimerGroup(StringRef Name, StringRef Description,
                      sys::SmartMutex<true> &lock, bool PrintOnExit);

public:
  LLVM_ABI explicit TimerGroup(StringRef Name, StringRef Description,
                               bool PrintOnExit = true);

  LLVM_ABI explicit TimerGroup(StringRef Name, StringRef Description,
                               const StringMap<TimeRecord> &Records,
                               bool PrintOnExit = true);

  LLVM_ABI ~TimerGroup();

  void setName(StringRef NewName, StringRef NewDescription) {
    Name.assign(NewName.begin(), NewName.end());
    Description.assign(NewDescription.begin(), NewDescription.end());
  }

  /// Make the user and system times of the timers in this group only count
  /// the CPU time of the thread that runs them, rather than of the whole
  /// process. Use this when other threads may be busy while a timer runs.
  void setThreadCPUTime(bool Enable) { ThreadCPUTime = Enable; }
  bool usesThreadCPUTime() const { return ThreadCPUTime; }

  /// Print any started timers in this group, optionally resetting timers after
  /// printing them.
  LLVM_ABI void print(raw_ostream &OS, bool ResetAfterPrint = false);

  /// Clear all timers in this group.
  LLVM_ABI void clear();

  /// This static method prints all timers.
  LLVM_ABI static void printAll(raw_ostream &OS);

  /// Clear out all timers. This is mostly used to disable automatic
  /// printing on shutdown, when timers have already been printed explicitly
  /// using \c printAll or \c printJSONValues.
  LLVM_ABI static void clearAll();

  LLVM_ABI const char *printJSONValues(raw_ostream &OS, const char *delim);

  /// Prints all timers as JSON key/value pairs.
  LLVM_ABI static const char *printAllJSONValues(raw_ostream &OS,
                                                 const char *delim);

  /// Ensure global objects required for statistics printing are initialized.
  /// This function is used by the Statistic code to ensure correct order of
  /// global constructors and destructors.
  LLVM_ABI static void constructForStatistics();

  /// This makes the timer globals unmanaged, and lets the user manage the
  /// lifetime.
  LLVM_ABI static void *acquireTimerGlobals();

private:
  friend class Timer;
  friend class TimerRegistry;
  LLVM_ABI friend void PrintStatisticsJSON(raw_ostream &OS);
  void addTimer(Timer &T);
  void removeTimer(Timer &T);
  void prepareToPrintList(bool reset_time = false);
  void PrintQueuedTimers(raw_ostream &OS);
  void printJSONValue(raw_ostream &OS, const PrintRecord &R,
                      const char *suffix, double Value);
};

/// A set of named timer groups, each holding timers looked up by name, as used
/// by NamedRegionTimer. A compilation that owns a registry gets timers that are
/// separate from those of every other compilation in the process, so that
/// concurrent compilations can be timed independently.
///
/// A registry is not thread-safe: only one thread may use it at a time. Its
/// groups are still registered with TimerGroup::printAll, and are printed when
/// the registry is destroyed if they hold timing data that was not printed.
class TimerRegistry {
  struct GroupEntry {
    std::unique_ptr<TimerGroup> Group;
    StringMap<Timer> Timers;
  };
  StringMap<GroupEntry> Groups;
  std::vector<TimerGroup *> GroupsInCreationOrder;
  bool ThreadCPUTime;

  GroupEntry &getGroupEntry(StringRef GroupName, StringRef GroupDescription);

public:
  /// If \p ThreadCPUTime is true, the registry's groups only count the CPU
  /// time of the thread running their timers. See
  /// TimerGroup::setThreadCPUTime.
  LLVM_ABI explicit TimerRegistry(bool ThreadCPUTime = false);
  TimerRegistry(const TimerRegistry &) = delete;
  TimerRegistry &operator=(const TimerRegistry &) = delete;
  LLVM_ABI ~TimerRegistry();

  /// Get the timer called \p Name in the group called \p GroupName, creating
  /// either if needed.
  LLVM_ABI Timer &getTimer(StringRef Name, StringRef Description,
                           StringRef GroupName, StringRef GroupDescription);

  /// Get the group called \p GroupName, creating it if needed.
  LLVM_ABI TimerGroup &getTimerGroup(StringRef GroupName,
                                     StringRef GroupDescription);

  /// Get the groups of the registry, in creation order.
  ArrayRef<TimerGroup *> getTimerGroups() const {
    return GroupsInCreationOrder;
  }

  /// Move the timing data of \p Other into this registry and reset the timers
  /// of \p Other. Data is added to the group with the same name, summing
  /// timers that have the same name and description. This includes data of
  /// timers that were already destroyed. Several threads may merge into the
  /// same registry at once, as long as nothing else uses it meanwhile.
  LLVM_ABI void mergeFrom(TimerRegistry &Other);

  /// Print the started timers of every group in creation order, and reset
  /// them.
  LLVM_ABI void print(raw_ostream &OS);

  /// Clear the timers of every group, so that they are not printed when the
  /// registry is destroyed.
  LLVM_ABI void clear();
};

} // end namespace llvm

#endif
