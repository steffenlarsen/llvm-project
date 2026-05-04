#include "llvm/Support/DebugCounter.h"

#include "DebugOptions.h"

#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/ManagedStatic.h"

using namespace llvm;

namespace llvm {

void DebugCounter::printChunks(raw_ostream &OS,
                               ArrayRef<IntegerInclusiveInterval> Chunks) {
  IntegerInclusiveIntervalUtils::printIntervals(OS, Chunks, ':');
}

} // namespace llvm

namespace {

// Values are set by applySupportOptions() via
// setPrintCounter()/push_back()/etc.
struct DebugCounterOwner : DebugCounter {
  DebugCounterOwner() {
    (void)dbgs();
  }

  ~DebugCounterOwner() {
    if (ShouldPrintCounter)
      print(dbgs());
  }
};

} // anonymous namespace

// Use ManagedStatic instead of function-local static variable to ensure
// the destructor (which accesses counters and streams) runs during
// llvm_shutdown() rather than at some unspecified point.
static ManagedStatic<DebugCounterOwner> Owner;

void llvm::initDebugCounterOptions() { (void)DebugCounter::instance(); }

DebugCounter &DebugCounter::instance() { return *Owner; }

// This is called by the command line parser when it sees a value for the
// debug-counter option defined above.
void DebugCounter::push_back(const std::string &Val) {
  if (Val.empty())
    return;

  // The strings should come in as counter=chunk_list
  auto CounterPair = StringRef(Val).split('=');
  if (CounterPair.second.empty()) {
    errs() << "DebugCounter Error: " << Val << " does not have an = in it\n";
    exit(1);
  }
  StringRef CounterName = CounterPair.first;

  CounterInfo *Counter = getCounterInfo(CounterName);
  if (!Counter) {
    errs() << "DebugCounter Error: " << CounterName
           << " is not a registered counter\n";
    return;
  }

  auto ExpectedChunks =
      IntegerInclusiveIntervalUtils::parseIntervals(CounterPair.second, ':');
  if (!ExpectedChunks) {
    handleAllErrors(ExpectedChunks.takeError(), [&](const StringError &E) {
      errs() << "DebugCounter Error: " << E.getMessage() << "\n";
    });
    exit(1);
  }
  Counter->Chunks = std::move(*ExpectedChunks);
  Counter->Active = Counter->IsSet = true;
}

void DebugCounter::print(raw_ostream &OS) const {
  SmallVector<StringRef, 16> CounterNames(Counters.keys());
  sort(CounterNames);

  OS << "Counters and values:\n";
  for (StringRef CounterName : CounterNames) {
    const CounterInfo *C = getCounterInfo(CounterName);
    OS << left_justify(C->Name, 32) << ": {" << C->Count << ",";
    printChunks(OS, C->Chunks);
    OS << "}\n";
  }
}

bool DebugCounter::handleCounterIncrement(CounterInfo &Info) {
  int64_t CurrCount = Info.Count++;
  uint64_t CurrIdx = Info.CurrChunkIdx;

  if (Info.Chunks.empty())
    return true;
  if (CurrIdx >= Info.Chunks.size())
    return false;

  bool Res = Info.Chunks[CurrIdx].contains(CurrCount);
  if (BreakOnLast && CurrIdx == (Info.Chunks.size() - 1) &&
      CurrCount == Info.Chunks[CurrIdx].getEnd()) {
    LLVM_BUILTIN_DEBUGTRAP;
  }
  if (CurrCount > Info.Chunks[CurrIdx].getEnd()) {
    Info.CurrChunkIdx++;

    /// Handle consecutive blocks.
    if (Info.CurrChunkIdx < Info.Chunks.size() &&
        CurrCount == Info.Chunks[Info.CurrChunkIdx].getBegin())
      return true;
  }
  return Res;
}

bool DebugCounter::shouldExecuteImpl(CounterInfo &Counter) {
  auto &Us = instance();
  bool Res = Us.handleCounterIncrement(Counter);
  if (Us.ShouldPrintCounterQueries && Counter.IsSet) {
    dbgs() << "DebugCounter " << Counter.Name << "=" << (Counter.Count - 1)
           << (Res ? " execute" : " skip") << "\n";
  }
  return Res;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void DebugCounter::dump() const {
  print(dbgs());
}
#endif
