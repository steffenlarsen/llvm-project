//===- SupportOptions.cpp - LLVMSupport library options ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/SupportOptions.h"
#include "DebugOptions.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DebugCounter.h"
#include "llvm/Support/SupportOptionsOptInfos.h"
#include <optional>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::clv2;

SupportOptions SupportOptions::Current;

bool support::StatsEnabled = false;
bool support::StatsAsJsonEnabled = false;
unsigned support::DebugBufferSizeVal = 0;
bool support::ViewBackgroundFlag = false;
StringRef support::DagFileLocation;
/// Backing storage for DagFileLocation.  Deliberately a pointer, not a
/// std::string: llvm/lib/Support is built with -Werror=global-constructors.
/// Kept at namespace scope alongside the other bridge globals above rather
/// than hidden in a function-local static -- this is process-wide state and
/// should look like it.  Never freed; readers hold a StringRef into it.
static std::string *DagFileLocationStorage = nullptr;
bool support::NoOpenDagViewer = false;

// Defined in Signals.cpp — written directly because signal handlers read it
// without going through a getter.
extern bool DisableSymbolicationFlag;

namespace {
/// The options one parse saw, each left empty when not given. Shared by
/// SupportOptions::parse() and the clv2 bridge so both apply identically.
struct SupportOptionValues {
  std::optional<bool> Stats, StatsJson, TrackMemory, SortTimers,
      DisableSymbolication, ViewBackground, NoOpenDagViewer, Debug,
      PrintDebugCounter, PrintDebugCounterQueries, BreakOnLastCount;
  std::optional<std::string> InfoOutputFile, CrashDiagnosticsDir,
      DagFileLocation;
  std::optional<uint64_t> RngSeed;
  std::optional<unsigned> DebugBufferSize;
  std::vector<std::string> DebugOnly, DebugCounter;
};
} // namespace

static void applyValues(const SupportOptionValues &V) {
  if (V.DisableSymbolication)
    DisableSymbolicationFlag = *V.DisableSymbolication;
  if (V.CrashDiagnosticsDir)
    setCrashDiagnosticsDirectory(*V.CrashDiagnosticsDir);

#ifndef NDEBUG
  if (V.Debug)
    llvm::DebugFlag = *V.Debug;

  if (!V.DebugOnly.empty()) {
    llvm::DebugFlag = true;
    SmallVector<const char *, 8> TypePtrs;
    TypePtrs.reserve(V.DebugOnly.size());
    for (const std::string &Type : V.DebugOnly)
      TypePtrs.push_back(Type.c_str());
    llvm::setCurrentDebugTypes(TypePtrs.data(), TypePtrs.size());
  }
#endif

  for (const std::string &Arg : V.DebugCounter)
    llvm::DebugCounter::instance().push_back(Arg);
  if (V.PrintDebugCounter)
    llvm::DebugCounter::instance().setPrintCounter(*V.PrintDebugCounter);
  if (V.PrintDebugCounterQueries)
    llvm::DebugCounter::instance().setPrintCounterQueries(
        *V.PrintDebugCounterQueries);
  if (V.BreakOnLastCount)
    llvm::DebugCounter::instance().setBreakOnLast(*V.BreakOnLastCount);

  if (V.InfoOutputFile)
    setInfoOutputFilename(*V.InfoOutputFile);
  if (V.TrackMemory)
    setTrackSpace(*V.TrackMemory);
  if (V.SortTimers)
    setSortTimers(*V.SortTimers);
  if (V.Stats)
    support::StatsEnabled = *V.Stats;
  if (V.StatsJson)
    support::StatsAsJsonEnabled = *V.StatsJson;
  if (V.DebugBufferSize)
    support::DebugBufferSizeVal = *V.DebugBufferSize;
  if (V.ViewBackground)
    support::ViewBackgroundFlag = *V.ViewBackground;
  if (V.DagFileLocation) {
    if (!DagFileLocationStorage)
      DagFileLocationStorage = new std::string();
    *DagFileLocationStorage = *V.DagFileLocation;
    support::DagFileLocation = *DagFileLocationStorage;
  }
  if (V.NoOpenDagViewer)
    support::NoOpenDagViewer = *V.NoOpenDagViewer;
  if (V.RngSeed)
    SupportOptions::Current.RngSeed = *V.RngSeed;
}

using BoolFieldT = std::optional<bool> SupportOptionValues::*;
using StringFieldT = std::optional<std::string> SupportOptionValues::*;
using ListFieldT = std::vector<std::string> SupportOptionValues::*;

static BoolFieldT boolField(StringRef Name) {
  return StringSwitch<BoolFieldT>(Name)
      .Case("stats", &SupportOptionValues::Stats)
      .Case("stats-json", &SupportOptionValues::StatsJson)
      .Case("track-memory", &SupportOptionValues::TrackMemory)
      .Case("sort-timers", &SupportOptionValues::SortTimers)
      .Case("disable-symbolication", &SupportOptionValues::DisableSymbolication)
      .Case("view-background", &SupportOptionValues::ViewBackground)
      .Case("no-open-dag-viewer", &SupportOptionValues::NoOpenDagViewer)
      .Case("debug", &SupportOptionValues::Debug)
      .Case("print-debug-counter", &SupportOptionValues::PrintDebugCounter)
      .Case("print-debug-counter-queries",
            &SupportOptionValues::PrintDebugCounterQueries)
      .Case("debug-counter-break-on-last",
            &SupportOptionValues::BreakOnLastCount)
      .Default(nullptr);
}

static StringFieldT stringField(StringRef Name) {
  return StringSwitch<StringFieldT>(Name)
      .Case("info-output-file", &SupportOptionValues::InfoOutputFile)
      .Case("crash-diagnostics-dir", &SupportOptionValues::CrashDiagnosticsDir)
      .Case("dag-file-location", &SupportOptionValues::DagFileLocation)
      .Default(nullptr);
}

static ListFieldT listField(StringRef Name) {
  return StringSwitch<ListFieldT>(Name)
      .Case("debug-only", &SupportOptionValues::DebugOnly)
      .Case("debug-counter", &SupportOptionValues::DebugCounter)
      .Default(nullptr);
}

// Like a generated struct's parse(), the whole diagnostic goes in the returned
// Error: chain callers print that and drop Errs on failure.
static Error invalidValue(StringRef Name, const Twine &Msg) {
  return createStringError(inconvertibleErrorCode(),
                           "for the --" + Name + " option: " + Msg);
}

// Mirrors how clv2 parsed these options: a bool never takes the next token
// and a bare flag (or -x=) means true; every other option takes its value
// inline (-x=v) or, failing that, unconditionally from the next token (-x v).
Error SupportOptions::parse(ArrayRef<const char *> Args,
                            SmallVectorImpl<const char *> &Rest,
                            raw_ostream &Errs) {
  SupportOptionValues V;
  for (size_t I = 0, E = Args.size(); I != E; ++I) {
    StringRef Arg = Args[I];
    if (Arg == "--") {
      Rest.append(Args.begin() + I, Args.end());
      break;
    }
    if (!Arg.starts_with("-") || Arg.size() == 1) {
      Rest.push_back(Args[I]);
      continue;
    }
    StringRef NameAndVal = Arg.ltrim('-');
    bool HasInlineVal = NameAndVal.contains('=');
    auto [Name, InlineVal] = NameAndVal.split('=');

    BoolFieldT BoolF = boolField(Name);
    StringFieldT StringF = stringField(Name);
    ListFieldT ListF = listField(Name);
    bool IsUInt = Name == "rng-seed" || Name == "debug-buffer-size";
    if (!BoolF && !StringF && !ListF && !IsUInt) {
      Rest.push_back(Args[I]);
      continue;
    }

    if (BoolF) {
      if (InlineVal.empty() || InlineVal.equals_insensitive("true") ||
          InlineVal == "1" || InlineVal.equals_insensitive("yes") ||
          InlineVal.equals_insensitive("on"))
        V.*BoolF = true;
      else if (InlineVal.equals_insensitive("false") || InlineVal == "0" ||
               InlineVal.equals_insensitive("no") ||
               InlineVal.equals_insensitive("off"))
        V.*BoolF = false;
      else
        return invalidValue(Name,
                            "'" + InlineVal +
                                "' is invalid value for boolean argument! "
                                "Try true/false/1/0/yes/no/on/off");
      continue;
    }

    StringRef Val = InlineVal;
    if (!HasInlineVal) {
      if (I + 1 == E)
        return invalidValue(Name, "requires a value!");
      Val = Args[++I];
    }

    if (StringF) {
      V.*StringF = Val.str();
    } else if (ListF) {
      if (Val.empty()) {
        (V.*ListF).emplace_back();
        continue;
      }
      SmallVector<StringRef, 4> Elts;
      Val.split(Elts, ',', /*MaxSplit=*/-1, /*KeepEmpty=*/true);
      for (StringRef Elt : Elts)
        (V.*ListF).push_back(Elt.str());
    } else if (Name == "rng-seed") {
      uint64_t Seed;
      if (Val.getAsInteger(0, Seed))
        return invalidValue(Name,
                            "'" + Val + "' value invalid for uint argument!");
      V.RngSeed = Seed;
    } else {
      unsigned Size;
      if (Val.getAsInteger(0, Size))
        return invalidValue(Name,
                            "'" + Val + "' value invalid for uint argument!");
      V.DebugBufferSize = Size;
    }
  }
  applyValues(V);
  return Error::success();
}

void support::applySupportOptions(const support::ParsedOpts &Opts) {
  SupportOptionValues V;
  auto Take = [&](auto &Field, bool Specified, const auto &Value) {
    if (Specified)
      Field = Value;
  };
  Take(V.Stats, Opts.specified<&SUP_Stats>(), Opts.get<&SUP_Stats>());
  Take(V.StatsJson, Opts.specified<&SUP_StatsJson>(),
       Opts.get<&SUP_StatsJson>());
  Take(V.InfoOutputFile, Opts.specified<&SUP_InfoOutputFile>(),
       Opts.get<&SUP_InfoOutputFile>());
  Take(V.TrackMemory, Opts.specified<&SUP_TrackMemory>(),
       Opts.get<&SUP_TrackMemory>());
  Take(V.SortTimers, Opts.specified<&SUP_SortTimers>(),
       Opts.get<&SUP_SortTimers>());
  Take(V.DisableSymbolication, Opts.specified<&SUP_DisableSymbolication>(),
       Opts.get<&SUP_DisableSymbolication>());
  Take(V.CrashDiagnosticsDir, Opts.specified<&SUP_CrashDiagnosticsDir>(),
       Opts.get<&SUP_CrashDiagnosticsDir>());
  Take(V.RngSeed, Opts.specified<&SUP_RngSeed>(), Opts.get<&SUP_RngSeed>());
  Take(V.ViewBackground, Opts.specified<&SUP_ViewBackground>(),
       Opts.get<&SUP_ViewBackground>());
  Take(V.DagFileLocation, Opts.specified<&SUP_DagFileLocation>(),
       Opts.get<&SUP_DagFileLocation>());
  Take(V.NoOpenDagViewer, Opts.specified<&SUP_NoOpenDagViewer>(),
       Opts.get<&SUP_NoOpenDagViewer>());
  Take(V.Debug, Opts.specified<&SUP_Debug>(), Opts.get<&SUP_Debug>());
  Take(V.DebugBufferSize, Opts.specified<&SUP_DebugBufferSize>(),
       Opts.get<&SUP_DebugBufferSize>());
  Take(V.PrintDebugCounter, Opts.specified<&SUP_PrintDebugCounter>(),
       Opts.get<&SUP_PrintDebugCounter>());
  Take(V.PrintDebugCounterQueries,
       Opts.specified<&SUP_PrintDebugCounterQueries>(),
       Opts.get<&SUP_PrintDebugCounterQueries>());
  Take(V.BreakOnLastCount, Opts.specified<&SUP_BreakOnLastCount>(),
       Opts.get<&SUP_BreakOnLastCount>());
  if (Opts.specified<&SUP_DebugOnly>()) {
    const auto &Types = Opts.get<&SUP_DebugOnly>();
    V.DebugOnly.assign(Types.begin(), Types.end());
  }
  if (Opts.specified<&SUP_DebugCounter>()) {
    const auto &Counters = Opts.get<&SUP_DebugCounter>();
    V.DebugCounter.assign(Counters.begin(), Counters.end());
  }
  applyValues(V);
}
