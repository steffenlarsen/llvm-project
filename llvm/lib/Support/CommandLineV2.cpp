//===- CommandLineV2.cpp - Compile-time CLI option interface --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/CommandLineV2.h"
#include "DebugOptions.h"
#include "llvm-c/Support.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/config.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineTokenizer.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PluginLoader.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/raw_ostream.h"

#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::clv2;
using namespace llvm::clv2::detail;

//===----------------------------------------------------------------------===//
// Type-specific value parsers
//===----------------------------------------------------------------------===//

static void emitProgNamePrefix(ParseDiag &Diag) {
  if (!Diag.ProgramName.empty())
    Diag.Errs << Diag.ProgramName << ": ";
}

bool clv2::detail::rejectOptionValue(StringRef OptName, const Twine &Msg,
                                     ParseDiag &Diag) {
  emitProgNamePrefix(Diag);
  Diag.Errs << "for the --" << OptName << " option: " << Msg << "\n";
  return false;
}

bool clv2::detail::validateRegexOption(StringRef Pattern, StringRef OptName,
                                       ParseDiag &Diag) {
  if (Pattern.empty())
    return true;
  Regex R(Pattern);
  std::string Error;
  if (R.isValid(Error))
    return true;
  // Deliberately not rejectOptionValue(): this wording predates clv2 and is
  // asserted by existing tests, so it is reproduced verbatim.  The rejection
  // still travels through ParseDiag, so it honours the OnError policy instead
  // of terminating the process the way the old validation did.
  emitProgNamePrefix(Diag);
  Diag.Errs << "Invalid regular expression '" << Pattern << "' in -" << OptName
            << ": " << Error << "\n";
  return false;
}

bool clv2::detail::parseBoolArg(StringRef OptName, StringRef Val, bool &Out,
                                ParseDiag &Diag) {
  if (Val.empty() || Val.equals_insensitive("true") ||
      Val.equals_insensitive("1") || Val.equals_insensitive("yes") ||
      Val.equals_insensitive("on")) {
    Out = true;
    return true;
  }
  if (Val.equals_insensitive("false") || Val.equals_insensitive("0") ||
      Val.equals_insensitive("no") || Val.equals_insensitive("off")) {
    Out = false;
    return true;
  }
  // Same shape as the other seven parsers: program-name prefix, then
  // "for the --<opt> option: ...".  This one used to print a bare
  // "error: invalid boolean value ..." with no prefix.
  emitProgNamePrefix(Diag);
  Diag.Errs << "for the --" << OptName << " option: '" << Val
            << "' is invalid value for boolean argument! Try "
               "true/false/1/0/yes/no/on/off\n";
  return false;
}

bool clv2::detail::parseIntArg(StringRef OptName, StringRef Val, int &Out,
                               ParseDiag &Diag) {
  if (Val.getAsInteger(0, Out)) {
    emitProgNamePrefix(Diag);
    Diag.Errs << "for the --" << OptName << " option: '" << Val
              << "' value invalid for integer argument!\n";
    return false;
  }
  return true;
}

bool clv2::detail::parseUIntArg(StringRef OptName, StringRef Val, unsigned &Out,
                                ParseDiag &Diag) {
  if (Val.getAsInteger(0, Out)) {
    emitProgNamePrefix(Diag);
    Diag.Errs << "for the --" << OptName << " option: '" << Val
              << "' value invalid for uint argument!\n";
    return false;
  }
  return true;
}

bool clv2::detail::parseInt64Arg(StringRef OptName, StringRef Val, int64_t &Out,
                                 ParseDiag &Diag) {
  if (Val.getAsInteger(0, Out)) {
    emitProgNamePrefix(Diag);
    Diag.Errs << "for the --" << OptName << " option: '" << Val
              << "' value invalid for integer argument!\n";
    return false;
  }
  return true;
}

bool clv2::detail::parseUInt64Arg(StringRef OptName, StringRef Val,
                                  uint64_t &Out, ParseDiag &Diag) {
  if (Val.getAsInteger(0, Out)) {
    emitProgNamePrefix(Diag);
    Diag.Errs << "for the --" << OptName << " option: '" << Val
              << "' value invalid for uint argument!\n";
    return false;
  }
  return true;
}

bool clv2::detail::parseFloatArg(StringRef OptName, StringRef Val, float &Out,
                                 ParseDiag &Diag) {
  std::string S = Val.str();
  char *End = nullptr;
  errno = 0;
  float F = std::strtof(S.c_str(), &End);
  if (errno != 0 || End == S.c_str() || *End != '\0') {
    emitProgNamePrefix(Diag);
    Diag.Errs << "for the --" << OptName << " option: '" << Val
              << "' value invalid for floating point argument!\n";
    return false;
  }
  Out = F;
  return true;
}

bool clv2::detail::parseDoubleArg(StringRef OptName, StringRef Val, double &Out,
                                  ParseDiag &Diag) {
  std::string S = Val.str();
  char *End = nullptr;
  errno = 0;
  double D = std::strtod(S.c_str(), &End);
  if (errno != 0 || End == S.c_str() || *End != '\0') {
    emitProgNamePrefix(Diag);
    Diag.Errs << "for the --" << OptName << " option: '" << Val
              << "' value invalid for floating point argument!\n";
    return false;
  }
  Out = D;
  return true;
}

bool clv2::detail::parseElementCountArg(StringRef OptName, StringRef Val,
                                        ElementCount &Out, ParseDiag &Diag) {
  Val = Val.trim();
  unsigned MinValue;
  if (!Val.getAsInteger(0, MinValue)) {
    Out = ElementCount::getFixed(MinValue);
    return true;
  }
  StringRef Remainder = Val;
  if (!Remainder.consume_front("vscale")) {
    emitProgNamePrefix(Diag);
    Diag.Errs << "for the --" << OptName << " option: '" << Val
              << "' value invalid for ElementCount argument!\n";
    return false;
  }
  Remainder = Remainder.ltrim();
  if (!Remainder.consume_front("x")) {
    emitProgNamePrefix(Diag);
    Diag.Errs << "for the --" << OptName << " option: '" << Val
              << "' value invalid for ElementCount argument!\n";
    return false;
  }
  Remainder = Remainder.ltrim();
  if (Remainder.getAsInteger(0, MinValue)) {
    emitProgNamePrefix(Diag);
    Diag.Errs << "for the --" << OptName << " option: '" << Val
              << "' value invalid for ElementCount argument!\n";
    return false;
  }
  Out = ElementCount::getScalable(MinValue);
  return true;
}

//===----------------------------------------------------------------------===//
// Built-in options (help, version) — registered as real OptionEntry objects
//===----------------------------------------------------------------------===//

// The category under which help/version options are grouped in --help output.
const OptionCategory clv2::GenericOptionsCategory{"Generic Options"};

// The category for options with no explicit category, matching cl's default
// "General options" section.
static const OptionCategory GeneralOptionsCategory{"General options"};

// (BuiltinOccurrences moved into ParseFrame — no file-scope static needed.)

// Forward declarations — defined in the runtime subcommand registry section
// below.
template <typename T> class RegistrationList;
static RegistrationList<RuntimeSubCommandEntry> &getRuntimeSubcmdRegistry();

static void applyHideUnrelatedFilter(std::vector<OptionEntry> &Entries,
                                     const ParseFrame &Frame) {
  if (!Frame.HideUnrelated)
    return;
  for (OptionEntry &E : Entries) {
    if (E.isPositional())
      continue;
    if (E.Cat == &clv2::GenericOptionsCategory)
      continue;
    bool Allowed = false;
    for (const OptionCategory *C : Frame.AllowedCategories) {
      if (E.Cat == C) {
        Allowed = true;
        break;
      }
      // Name-based match for categories with the same name but different
      // instances.
      if (E.Cat && C && StringRef(E.Cat->Name) == StringRef(C->Name)) {
        Allowed = true;
        break;
      }
    }
    if (!Allowed)
      E.HiddenFlag = ReallyHidden;
  }
  // showOptions overrides hideUnrelatedOptions for specific options.
  // Also assign the tool's allowed category so they display in the
  // right section (not a separate "General options:" header).
  const OptionCategory *FirstAllowed =
      Frame.AllowedCategories.empty() ? nullptr : Frame.AllowedCategories[0];
  for (OptionEntry &E : Entries)
    for (StringRef SN : Frame.ShownNames)
      if (E.name() == SN && E.HiddenFlag == ReallyHidden) {
        E.HiddenFlag = NotHidden;
        if (!E.Cat && FirstAllowed)
          E.Cat = FirstAllowed;
      }
}

// (CurrentActiveEntries, CurrentActiveSubCommandName, CurrentCLArgPosition,
// and CurrentProgramName moved into ParseFrame — no file-scope statics needed.)

// Built-in option actions.  Each receives the shared BuiltinOptionState via
// OptionEntry::ParseDesc; the value slot is unused.
namespace {
const BuiltinOptionState &builtinState(const void *D) {
  return *static_cast<const BuiltinOptionState *>(D);
}

/// Shared body for every help-printing builtin.  All of them set HelpPrinted
/// so runParser skips occurrence validation — printing help is terminal, and a
/// tool with a Required positional must not then also report it missing.
bool printBuiltinHelp(const void *D, bool ShowHidden, bool ListForm) {
  const BuiltinOptionState &S = builtinState(D);
  if (ListForm)
    printHelpList(*S.Frame->ActiveEntries, S.Overview, S.ProgName, ShowHidden,
                  *S.HelpOS, *S.Frame);
  else
    printHelp(*S.Frame->ActiveEntries, S.Overview, S.ProgName, ShowHidden,
              *S.HelpOS, *S.Frame, S.ExtraHelp);
  S.Frame->HelpPrinted = true;
  if (S.Frame->OnErr == OnError::ExitProcess)
    std::exit(0);
  return true;
}

bool builtinHelp(const void *D, void *, unsigned, StringRef, ParseDiag &) {
  return printBuiltinHelp(D, /*ShowHidden=*/false, /*ListForm=*/false);
}
bool builtinHelpHidden(const void *D, void *, unsigned, StringRef,
                       ParseDiag &) {
  return printBuiltinHelp(D, /*ShowHidden=*/true, /*ListForm=*/false);
}
bool builtinHelpList(const void *D, void *, unsigned, StringRef, ParseDiag &) {
  return printBuiltinHelp(D, /*ShowHidden=*/false, /*ListForm=*/true);
}
bool builtinHelpListHidden(const void *D, void *, unsigned, StringRef,
                           ParseDiag &) {
  return printBuiltinHelp(D, /*ShowHidden=*/true, /*ListForm=*/true);
}

bool builtinVersion(const void *D, void *, unsigned, StringRef, ParseDiag &) {
  const BuiltinOptionState &S = builtinState(D);
  if (!S.VersionString.empty())
    *S.HelpOS << S.VersionString << "\n";
  // Route the banner through the caller-supplied stream; the no-argument
  // overload would always write to outs().
  cl::PrintVersionMessage(*S.HelpOS);
  if (S.VersionPrinter)
    S.VersionPrinter(*S.HelpOS);
  // Printing the version is terminal, exactly like printing help: under
  // OnError::ExitProcess we exit, and otherwise the caller must be able to see
  // that the parse result is not meant to be acted on.  Without this the
  // Return path handed back a live context and the tool would run anyway.
  S.Frame->HelpPrinted = true;
  if (S.Frame->OnErr == OnError::ExitProcess)
    std::exit(0);
  return true;
}

/// --print-all-options / --print-options.  These only record the request:
/// the values are not final until parsing completes, so runParser does the
/// printing on its way out.
bool builtinPrintAllOptions(const void *D, void *, unsigned, StringRef,
                            ParseDiag &) {
  const_cast<BuiltinOptionState *>(static_cast<const BuiltinOptionState *>(D))
      ->PrintAllOptions = true;
  return true;
}

bool builtinPrintOptions(const void *D, void *, unsigned, StringRef,
                         ParseDiag &) {
  const_cast<BuiltinOptionState *>(static_cast<const BuiltinOptionState *>(D))
      ->PrintSpecifiedOptions = true;
  return true;
}
} // namespace

std::size_t clv2::detail::buildBuiltinEntries(
    std::vector<OptionEntry> &Entries, StringRef Overview, StringRef ProgName,
    StringRef VersionString, raw_ostream *HelpOS, StringRef ExtraHelp,
    std::function<void(raw_ostream &)> VersionPrinter, raw_ostream *Errs,
    ParseFrame &Frame) {

  // Helper to build one built-in entry.
  // Fill the per-parse state the builtin actions read through ParseDesc.
  Frame.Builtins.Overview = Overview;
  Frame.Builtins.ExtraHelp = ExtraHelp;
  Frame.Builtins.VersionString = VersionString;
  Frame.Builtins.Errs = Errs;
  Frame.Builtins.Frame = &Frame;
  Frame.Builtins.VersionPrinter = std::move(VersionPrinter);

  // Index into Frame.BuiltinStatics, bumped per builtin created below.
  unsigned NextBuiltin = 0;
  auto makeBuiltin = [&](StringRef Name, StringRef Desc, OptionHidden Hidden,
                         unsigned &Counter,
                         bool (*Action)(const void *, void *, unsigned,
                                        StringRef,
                                        ParseDiag &)) -> OptionEntry {
    assert(NextBuiltin < std::size(Frame.BuiltinStatics) &&
           "more builtins than reserved static-info slots");
    OptionStaticInfo &S = Frame.BuiltinStatics[NextBuiltin++];
    S = OptionStaticInfo{};
    S.Name = Name;
    S.Description = Desc;
    S.ValueDesc = "";
    S.IsPositional = false;
    S.IsPrefix = false;
    S.OccurrencesFlag = Optional;
    S.ValueExpected = ValueDisallowed;
    S.DefaultHidden = Hidden;
    S.MiscFlagsBits = 0;
    S.DefaultCat = &clv2::GenericOptionsCategory;
    S.Desc = &Frame.Builtins;
    S.ParseFn = Action;
    S.IsBuiltin = true;

    OptionEntry E;
    E.HiddenFlag = Hidden;
    E.OccurrenceCount = &Counter;
    E.LastPosition = nullptr;
    E.Cat = &clv2::GenericOptionsCategory;
    E.Static = &S;
    return E;
  };

  Frame.Builtins.HelpOS = HelpOS ? HelpOS : &llvm::outs();
  Frame.Builtins.ProgName =
      ProgName.empty() ? "" : sys::path::filename(ProgName);

  // Each builtin gets its own counter slot, named by BuiltinSlot; sharing one
  // would make a builtin's occurrence count observable through another's.

  // The front builtins are collected first and spliced in with a single
  // insert; inserting them one at a time would shift the whole entry vector
  // once per builtin.
  llvm::SmallVector<OptionEntry, 6> Front;
  // -h comes first so it sorts before --help in alphabetical help output,
  // matching cl's order.
  Front.push_back(makeBuiltin("h", "Alias for --help", Hidden,
                              Frame.BuiltinOccurrences[BS_H], &builtinHelp));
  Front.push_back(
      makeBuiltin("help", "Display available options (--help-hidden for more)",
                  NotHidden, Frame.BuiltinOccurrences[BS_Help], &builtinHelp));
  Front.push_back(makeBuiltin("help-hidden", "Display all available options",
                              Hidden, Frame.BuiltinOccurrences[BS_HelpHidden],
                              &builtinHelpHidden));
  Front.push_back(makeBuiltin(
      "help-list",
      "Display list of available options (--help-list-hidden for more)",
      NotHidden, Frame.BuiltinOccurrences[BS_HelpList], &builtinHelpList));
  Front.push_back(makeBuiltin(
      "help-list-hidden", "Display list of all available options", Hidden,
      Frame.BuiltinOccurrences[BS_HelpListHidden], &builtinHelpListHidden));
  Front.push_back(makeBuiltin("version", "Display the version of this program",
                              NotHidden, Frame.BuiltinOccurrences[BS_Version],
                              &builtinVersion));
  Entries.insert(Entries.begin(), Front.begin(), Front.end());

  // Print all / non-default option values after parsing.  Hidden, so they
  // show up only under --help-hidden.
  Entries.push_back(makeBuiltin(
      "print-all-options", "Print all option values after command line parsing",
      Hidden, Frame.BuiltinOccurrences[BS_PrintAllOptions],
      &builtinPrintAllOptions));
  Entries.push_back(makeBuiltin(
      "print-options", "Print non-default options after command line parsing",
      Hidden, Frame.BuiltinOccurrences[BS_PrintOptions], &builtinPrintOptions));

  // The front-inserted builtins shifted every pre-existing entry by this much;
  // the print-* builtins above were appended, so they do not shift anything.
  return BS_PrintAllOptions;
}

//===----------------------------------------------------------------------===//
// Help text printer
//===----------------------------------------------------------------------===//

static std::string valuePlaceholder(OptionValueExpected VE, StringRef Desc,
                                    StringRef DefaultValueName = "value",
                                    bool IsEnum = false) {
  StringRef Inner = Desc.empty() ? DefaultValueName : Desc;
  if (Inner.empty())
    return "";
  switch (VE) {
  case ValueRequired:
    return ("=<" + Inner + ">").str();
  case ValueOptional:
    if (IsEnum)
      return ("=<" + Inner + ">").str();
    return ("[=<" + Inner + ">]").str();
  case ValueDisallowed:
    return "";
  }
  return "";
}

static std::string shortValuePlaceholder(OptionValueExpected VE, StringRef Desc,
                                         StringRef DefaultValueName = "value") {
  StringRef Inner = Desc.empty() ? DefaultValueName : Desc;
  switch (VE) {
  case ValueRequired:
    return (" <" + Inner + ">").str();
  case ValueOptional:
    return ("[=<" + Inner + ">]").str();
  case ValueDisallowed:
    return "";
  }
  return "";
}

/// True when this parse has subcommands to advertise in the USAGE line.
static bool hasSelectableSubcommands(const ParseFrame &Frame) {
  return !Frame.Subcommands.empty() && Frame.ActiveSubCommandName.empty();
}

/// Print the SUBCOMMANDS section listing every subcommand visible to this
/// parse.  No-op once a subcommand is already active, or when there are none.
static void printSubcommandsSection(raw_ostream &OS, StringRef ProgName,
                                    const ParseFrame &Frame) {
  if (!hasSelectableSubcommands(Frame))
    return;
  OS << "SUBCOMMANDS:\n\n";
  SmallVector<std::pair<StringRef, StringRef>, 8> Sorted(
      Frame.Subcommands.begin(), Frame.Subcommands.end());
  llvm::sort(Sorted, [](const std::pair<StringRef, StringRef> &A,
                        const std::pair<StringRef, StringRef> &B) {
    return A.first < B.first;
  });
  std::size_t MaxSCLen = 0;
  for (const auto &SC : Sorted)
    MaxSCLen = std::max(MaxSCLen, SC.first.size());
  for (const auto &SC : Sorted) {
    OS << "  " << SC.first;
    OS.indent(MaxSCLen - SC.first.size());
    OS << " - " << SC.second << "\n";
  }
  OS << "\n  Type \"" << ProgName
     << " <subcommand> --help\" to get more help on a specific "
        "subcommand\n\n";
}

//===----------------------------------------------------------------------===//
// BakedNameIndex
//
// The name index for a fixed prefix of the entry vector -- the options
// contributed by a CompiledParser's registries, which BuildInto emits in the
// same order for every parse, so indices stay valid across parses.
//
// Const after construction.  Concurrent parses only read it.
//===----------------------------------------------------------------------===//

namespace llvm {
namespace clv2 {
namespace detail {

class BakedNameIndex {
  llvm::StringMap<llvm::SmallVector<unsigned, 1>> ByName;
  llvm::SmallVector<unsigned, 4> PrefixEntries; ///< cl::Prefix, longest first
  int SinkEntry = -1;

public:
  BakedNameIndex(const std::vector<OptionEntry> &Entries, std::size_t N) {
    ByName = llvm::StringMap<llvm::SmallVector<unsigned, 1>>(N);
    for (unsigned I = 0; I < N; ++I) {
      const OptionEntry &E = Entries[I];
      if (!E.isPositional() || E.isPositionalEatsArgs())
        ByName[E.name()].push_back(I);
      if (E.isPrefix())
        PrefixEntries.push_back(I);
      if (SinkEntry < 0 && (E.miscFlagsBits() & Sink))
        SinkEntry = static_cast<int>(I);
    }
    // Longest name first, so the first match is the longest; stable so equal
    // lengths keep declaration order, matching the linear scan this replaces.
    llvm::stable_sort(PrefixEntries, [&Entries](unsigned A, unsigned B) {
      return Entries[A].name().size() > Entries[B].name().size();
    });
  }

  /// Indices for \p Name, ascending, or empty.
  llvm::ArrayRef<unsigned> lookup(llvm::StringRef Name) const {
    auto It = ByName.find(Name);
    return It == ByName.end() ? llvm::ArrayRef<unsigned>() : It->second;
  }
  llvm::ArrayRef<unsigned> prefixEntries() const { return PrefixEntries; }
  int sinkEntry() const { return SinkEntry; }
};

} // namespace detail

#ifndef NDEBUG
namespace {
/// Reads that went through defaultOptionsContext() -- i.e. through a context
/// nobody threaded.  Relaxed: this is a diagnostic tally, not a correctness
/// signal, and contention here would distort the parallelism it exists to
/// protect.
std::atomic<uint64_t> UnthreadedReads{0};

bool strictUnthreadedReads() {
  static const bool Strict = [] {
    const char *V = std::getenv("LLVM_OPTIONS_CONTEXT_STRICT");
    return V && llvm::StringRef(V) != "0";
  }();
  return Strict;
}
} // namespace

void noteUnthreadedRead() {
  UnthreadedReads.fetch_add(1, std::memory_order_relaxed);
  if (strictUnthreadedReads())
    report_fatal_error("clv2: option read through an unthreaded "
                       "OptionsContext (LLVM_OPTIONS_CONTEXT_STRICT=1). "
                       "Some caller reached defaultOptionsContext() where a "
                       "real context was expected.");
}

uint64_t unthreadedReadCount() {
  return UnthreadedReads.load(std::memory_order_relaxed);
}
#endif // NDEBUG

const OptionsContext &defaultOptionsContext() {
  // One shared instance rather than a static per accessor: it is const and
  // never acquires a view, so concurrent parses can all reference it safely,
  // and magic-static initialisation handles the race on first use.
  static const OptionsContext Empty{OptionsContext::DefaultTag{}};
  return Empty;
}

} // namespace clv2
} // namespace llvm

void clv2::detail::resolveAliases(std::vector<OptionEntry> &Entries,
                                  const std::vector<AliasEntry> &Aliases,
                                  ParseFrame &Frame, raw_ostream *Errs) {
  // Nothing to resolve, and building the target index below is O(entries) --
  // which most parses would pay for no reason.
  if (Aliases.empty())
    return;

  // Resolve each alias to its target's entry index.
  //
  // A StringMap over every entry would cost hashing and allocation even for a
  // registry set with a single alias in it.  Instead: a compiled parser answers
  // from its baked index for free, and otherwise one pass matches entries
  // against the handful of names actually aliased.
  llvm::SmallDenseMap<llvm::StringRef, unsigned, 8> TargetIdx;
  constexpr unsigned NotFoundYet = ~0u;
  for (const AliasEntry &A : Aliases)
    TargetIdx.try_emplace(A.Target, NotFoundYet);
  unsigned Unresolved = TargetIdx.size();
  if (Frame.Baked) {
    for (auto &KV : TargetIdx)
      for (unsigned I : Frame.Baked->lookup(KV.first)) {
        KV.second = static_cast<unsigned>(Frame.BakedFirst) + I;
        --Unresolved;
        break;
      }
  }
  // Entries the baked index does not cover (and all of them when there is no
  // baked index): first occurrence wins, matching the old map's semantics.
  // Skipped entirely when the baked index already answered every alias, which
  // is the usual case -- aliases almost always target a registry option.
  if (Unresolved) {
    const std::size_t SkipFirst = Frame.Baked ? Frame.BakedFirst : 0;
    const std::size_t SkipLast =
        Frame.Baked ? Frame.BakedFirst + Frame.BakedCount : 0;
    for (unsigned I = 0, N = Entries.size(); I < N; ++I) {
      if (I >= SkipFirst && I < SkipLast)
        continue;
      auto It = TargetIdx.find(Entries[I].name());
      if (It != TargetIdx.end() && It->second == NotFoundYet) {
        It->second = I;
        if (--Unresolved == 0)
          break;
      }
    }
  }

  // Collect proxies separately to avoid invalidating iterators during the loop.
  std::vector<OptionEntry> Proxies;
  Proxies.reserve(Aliases.size());
  for (const AliasEntry &A : Aliases) {
    bool Found = false;
    {
      auto It = TargetIdx.find(A.Target);
      if (It != TargetIdx.end() && It->second != NotFoundYet) {
        const OptionEntry &E = Entries[It->second];
        OptionEntry Proxy = E;
        // The proxy differs from its target by name (and maybe description),
        // so it needs its own static half rather than sharing the target's.
        assert(E.Static && "alias target has no static info");
        Frame.AliasStatics.push_back(*E.Static);
        OptionStaticInfo &PS = Frame.AliasStatics.back();
        PS.Name = A.Name;
        Proxy.Static = &PS;
        if (A.Desc && A.Desc[0]) {
          PS.Description = A.Desc;
          PS.SuppressValuePlaceholder = true;
        } else {
          Proxy.HiddenFlag = Hidden;
        }
        if (A.HiddenFlag != NotHidden)
          Proxy.HiddenFlag = A.HiddenFlag;
        Proxies.push_back(std::move(Proxy));
        Found = true;
      }
    }
    if (!Found) {
      std::string Msg = "error: alias '-";
      Msg += A.Name.str();
      Msg += "' refers to unknown option '-";
      Msg += A.Target.str();
      Msg += "'\n";
      if (Errs)
        *Errs << Msg;
      else
        llvm::errs() << Msg;
    }
  }
  Entries.insert(Entries.end(), std::make_move_iterator(Proxies.begin()),
                 std::make_move_iterator(Proxies.end()));
}

/// The options both help printers show, in entry order.
///
/// Positionals appear only in the USAGE line, except
/// PositionalEatsArgs ones which also get a body entry.  Names are
/// deduplicated so an option contributed by both a tool-local and a global
/// registry is listed once.
///
/// Under a subcommand, only that subcommand's options are listed, plus the
/// generic built-ins (--help, --version, ...) which every subcommand accepts.
/// A top-level option is not part of the subcommand's interface, and where
/// both define the same name the subcommand's own descriptor is the one that
/// describes what the user gets -- matching how EntryIndex::find() resolves
/// the name at parse time.
static std::vector<const OptionEntry *>
collectVisibleEntries(const std::vector<OptionEntry> &Entries, bool ShowHidden,
                      bool InSubCmd, std::size_t GlobalEntryCount) {
  auto IsShadowedGlobal = [&](std::size_t I, const OptionEntry &E) {
    return InSubCmd && I < GlobalEntryCount &&
           E.Cat != &clv2::GenericOptionsCategory;
  };

  std::vector<const OptionEntry *> Visible;
  llvm::DenseSet<llvm::StringRef> SeenNames;
  for (std::size_t I = 0, N = Entries.size(); I < N; ++I) {
    const OptionEntry &E = Entries[I];
    if (E.HiddenFlag == ReallyHidden)
      continue;
    if (!ShowHidden && E.HiddenFlag == Hidden)
      continue;
    if (E.isPositional() && !E.isPositionalEatsArgs())
      continue;
    if (IsShadowedGlobal(I, E))
      continue;
    if (!E.name().empty() && !E.isEnumGroupMember() &&
        !SeenNames.insert(E.name()).second)
      continue;
    Visible.push_back(&E);
  }
  return Visible;
}

void clv2::detail::printHelpList(const std::vector<OptionEntry> &EntriesIn,
                                 StringRef Overview, StringRef ProgName,
                                 bool ShowHidden, raw_ostream &OS,
                                 const ParseFrame &Frame) {
  const bool InSubCmd = !Frame.ActiveSubCommandName.empty();
  // Work on a private copy.  applyHideUnrelatedFilter rewrites HiddenFlag, and
  // callers may print help more than once or keep parsing afterwards, so those
  // rewrites must not escape into the caller's entry list.
  std::vector<OptionEntry> Entries = EntriesIn;
  applyHideUnrelatedFilter(Entries, Frame);

  if (!Overview.empty()) {
    StringRef OV = Overview;
    while (OV.ends_with("\n"))
      OV = OV.drop_back();
    OS << "OVERVIEW: " << OV << "\n\n";
  } else if (!ProgName.empty())
    OS << "OVERVIEW: Options for " << ProgName << "\n\n";

  if (!Frame.ActiveSubCommandName.empty()) {
    for (const auto &SC : Frame.Subcommands) {
      if (SC.first == Frame.ActiveSubCommandName) {
        OS << "SUBCOMMAND '" << SC.first << "': " << SC.second << "\n\n";
        break;
      }
    }
  }

  if (!ProgName.empty()) {
    if (hasSelectableSubcommands(Frame))
      OS << "USAGE: " << ProgName << " [subcommand] [options]";
    else if (!Frame.ActiveSubCommandName.empty())
      OS << "USAGE: " << ProgName << " " << Frame.ActiveSubCommandName
         << " [options]";
    else
      OS << "USAGE: " << ProgName << " [options]";
    for (std::size_t I = 0, N = Entries.size(); I < N; ++I) {
      const OptionEntry &E = Entries[I];
      if (E.HiddenFlag == ReallyHidden)
        continue;
      if (!ShowHidden && E.HiddenFlag == Hidden)
        continue;
      if (!E.isPositional())
        continue;
      if (InSubCmd && I < Frame.GlobalEntryCount)
        continue;
      if (E.isPositionalEatsArgs() && !E.name().empty())
        OS << " --" << E.name();
      StringRef Ph = E.description().empty() ? E.valueDesc() : E.description();
      if (!Ph.empty())
        OS << " " << Ph;
    }
    OS << "\n\n";
  }

  printSubcommandsSection(OS, ProgName, Frame);

  std::vector<const OptionEntry *> Visible = collectVisibleEntries(
      Entries, ShowHidden, InSubCmd, Frame.GlobalEntryCount);

  // Sort alphabetically by option name.
  llvm::sort(Visible, [](const OptionEntry *A, const OptionEntry *B) {
    return A->name().compare_insensitive(B->name()) < 0;
  });

  std::size_t MaxArgLen = 0;
  for (const OptionEntry *E : Visible) {
    bool IsShort = (E->name().size() == 1);
    std::string Ph =
        E->suppressValuePlaceholder() ? ""
        : IsShort ? shortValuePlaceholder(E->valueExpected(), E->valueDesc(),
                                          E->defaultValueName())
                  : valuePlaceholder(E->valueExpected(), E->valueDesc(),
                                     E->defaultValueName());
    std::size_t PrefixLen = IsShort ? 2 : 3;
    std::size_t Len = PrefixLen + E->name().size() + Ph.size();
    if (Len > MaxArgLen)
      MaxArgLen = Len;
  }

  OS << "OPTIONS:\n";
  for (const OptionEntry *E : Visible) {
    bool IsShort = (E->name().size() == 1);
    std::string Ph =
        E->suppressValuePlaceholder() ? ""
        : IsShort ? shortValuePlaceholder(E->valueExpected(), E->valueDesc(),
                                          E->defaultValueName())
                  : valuePlaceholder(E->valueExpected(), E->valueDesc(),
                                     E->defaultValueName());
    std::size_t PrefixLen = IsShort ? 2 : 3;
    std::size_t Used = PrefixLen + E->name().size() + Ph.size();
    if (IsShort)
      OS << "  -" << E->name() << Ph;
    else
      OS << "  --" << E->name() << Ph;
    for (std::size_t I = Used; I <= MaxArgLen; ++I)
      OS << ' ';
    OS << "- ";
    StringRef Desc = E->description();
    std::size_t Indent = MaxArgLen + 4;
    bool First = true;
    while (!Desc.empty()) {
      auto [Line, Rest] = Desc.split('\n');
      if (!First) {
        for (std::size_t I = 0; I < Indent; ++I)
          OS << ' ';
      }
      OS << Line << "\n";
      Desc = Rest;
      First = false;
      if (Line.empty() && Rest.empty())
        break;
    }
    if (First)
      OS << "\n";
  }
}

/// A ValueOptional option's help width is under-counted by 2: the placeholder
/// prints as "[=<val>]" but only three characters are charged.  Enum options
/// are exempt.  The column computation and the per-line padding must agree on
/// this, or the columns do not line up.
static bool hasValueOptionalWidthQuirk(const OptionEntry &E) {
  return E.valueExpected() == ValueOptional && !E.hasEnumPrinter();
}

void clv2::detail::printHelp(const std::vector<OptionEntry> &EntriesIn,
                             StringRef Overview, StringRef ProgName,
                             bool ShowHidden, raw_ostream &OS,
                             const ParseFrame &Frame, StringRef ExtraHelp) {
  // Work on a private copy.  Both applyHideUnrelatedFilter (HiddenFlag) and the
  // category normalisation below (E.Cat) rewrite entries; callers may print
  // help more than once or keep parsing afterwards, so those rewrites must not
  // escape into the caller's entry list.
  std::vector<OptionEntry> Entries = EntriesIn;
  applyHideUnrelatedFilter(Entries, Frame);

  const bool InSubCmd = !Frame.ActiveSubCommandName.empty();
  std::vector<const OptionEntry *> Visible = collectVisibleEntries(
      Entries, ShowHidden, InSubCmd, Frame.GlobalEntryCount);

  // One column width for all visible options, shared across categories.
  // Enum value widths also contribute (long enum names widen the column).
  std::size_t MaxArgLen = 0;
  for (const OptionEntry *E : Visible) {
    std::string Ph;
    if (E->isPositionalEatsArgs()) {
      StringRef Inner =
          E->defaultValueName().empty() ? "value" : E->defaultValueName();
      Ph = (" <" + Inner + ">...").str();
    } else {
      Ph = E->suppressValuePlaceholder()
               ? ""
               : valuePlaceholder(E->valueExpected(), E->valueDesc(),
                                  E->defaultValueName(), E->hasEnumPrinter());
    }
    std::size_t DashLen = (E->name().size() == 1) ? 1 : 2;
    std::size_t Prefix = E->isEnumGroupMember() ? 6 : 2;
    std::size_t PhLen = Ph.size();
    if (hasValueOptionalWidthQuirk(*E) && PhLen >= 2)
      PhLen -= 2;
    std::size_t Len = Prefix + DashLen + E->name().size() + PhLen + 3;
    if (Len > MaxArgLen)
      MaxArgLen = Len;
    if (std::size_t EnumUsed = E->maxEnumUsed(); EnumUsed > MaxArgLen)
      MaxArgLen = EnumUsed;
  }

  // Header.  If the overview text itself ends with '\n', the result is a blank
  // line between OVERVIEW and USAGE.
  if (!Overview.empty()) {
    StringRef OV = Overview;
    while (OV.ends_with("\n"))
      OV = OV.drop_back();
    bool HadTrailingNewline = (Overview.size() != OV.size());
    OS << "OVERVIEW: " << OV << "\n";
    if (HadTrailingNewline)
      OS << "\n";
  }
  if (!Frame.ActiveSubCommandName.empty())
    OS << "SUBCOMMAND '" << Frame.ActiveSubCommandName << "'\n\n";
  if (!ProgName.empty()) {
    if (hasSelectableSubcommands(Frame))
      OS << "USAGE: " << ProgName << " [subcommand] [options]";
    else
      OS << "USAGE: " << ProgName << " [options]";
    // Append positional argument placeholders from all entries (positionals
    // aren't in Visible but still appear in the USAGE line).
    // PositionalEatsArgs options are prefixed with --name.
    // When in a subcommand, skip top-level positionals.
    for (std::size_t I = 0, N = Entries.size(); I < N; ++I) {
      const OptionEntry &E = Entries[I];
      if (!E.isPositional())
        continue;
      if (E.HiddenFlag == ReallyHidden)
        continue;
      if (!ShowHidden && E.HiddenFlag == Hidden)
        continue;
      if (InSubCmd && I < Frame.GlobalEntryCount)
        continue;
      if (E.isPositionalEatsArgs() && !E.name().empty())
        OS << " --" << E.name();
      StringRef Ph = E.description();
      if (!Ph.empty())
        OS << " " << Ph;
      else
        OS << " ";
    }
    OS << "\n\n";
  }

  // If subcommands are registered and none is active, print a SUBCOMMANDS
  // section.
  printSubcommandsSection(OS, ProgName, Frame);

  OS << "OPTIONS:\n";

  auto printOneLine = [&](StringRef Name, StringRef Ph, StringRef Desc,
                          std::size_t MaxArgLen, std::size_t UsedAdjust = 0) {
    bool IsShort = (Name.size() == 1);
    std::size_t DashLen = IsShort ? 1 : 2;
    std::size_t Used = 2 + DashLen + Name.size() + Ph.size() - UsedAdjust;
    if (IsShort)
      OS << "  -" << Name << Ph;
    else
      OS << "  --" << Name << Ph;
    for (std::size_t I = Used; I < MaxArgLen - 3; ++I)
      OS << ' ';
    OS << " - ";
    std::size_t Indent = MaxArgLen;
    bool First = true;
    while (!Desc.empty()) {
      auto [Line, Rest] = Desc.split('\n');
      if (!First) {
        for (std::size_t I = 0; I < Indent; ++I)
          OS << ' ';
      }
      OS << Line << "\n";
      Desc = Rest;
      First = false;
      if (Line.empty() && Rest.empty())
        break;
    }
    if (First)
      OS << "\n";
  };

  auto printEntry = [&](const OptionEntry *E, std::size_t MaxArgLen) {
    // Unnamed enum group members: print header + indented entries
    if (E->isEnumGroupMember()) {
      if (!E->enumGroupHeader().empty())
        OS << "  " << E->enumGroupHeader() << "\n";
      bool IsShort = (E->name().size() == 1);
      std::size_t DashLen = IsShort ? 1 : 2;
      OS << "      ";
      if (IsShort)
        OS << "-" << E->name();
      else
        OS << "--" << E->name();
      std::size_t Used = 6 + DashLen + E->name().size();
      for (std::size_t I = Used; I < MaxArgLen - 3; ++I)
        OS << ' ';
      OS << " - " << E->description() << "\n";
      return;
    }
    if (E->showDualDisplay()) {
      printOneLine(E->name(), "", E->description(), MaxArgLen);
    }
    std::string Ph;
    if (E->isPositionalEatsArgs()) {
      // PositionalEatsArgs options use space-separated values with "..."
      // suffix (e.g. "--args <string>...").
      StringRef Inner =
          E->defaultValueName().empty() ? "value" : E->defaultValueName();
      Ph = (" <" + Inner + ">...").str();
    } else {
      Ph = E->suppressValuePlaceholder()
               ? ""
               : valuePlaceholder(E->valueExpected(), E->valueDesc(),
                                  E->defaultValueName(), E->hasEnumPrinter());
    }
    bool IsShort = (E->name().size() == 1);
    if (IsShort && !Ph.empty() && Ph[0] == '=' && !E->hasEnumPrinter()) {
      Ph[0] = ' ';
    }
    std::size_t Adjust =
        (hasValueOptionalWidthQuirk(*E) && !Ph.empty()) ? 2 : 0;
    printOneLine(E->name(), Ph, E->description(), MaxArgLen, Adjust);
    if (E->hasEnumPrinter()) {
      for (std::size_t I = 0; I < E->numEnumVals(); ++I)
        E->printEnumVal(OS, I, MaxArgLen);
    }
  };

  // Normalize: options with no explicit category fall into
  // GeneralOptionsCategory, matching cl's default behavior.
  for (OptionEntry &E : Entries)
    if (!E.Cat)
      E.Cat = &GeneralOptionsCategory;

  // When there are tool-specific categories and no options in
  // GeneralOptionsCategory, fold "General options" into the first tool
  // category to avoid an unwanted "General options:" header.
  {
    const OptionCategory *ToolCat = nullptr;
    for (const OptionEntry *E : Visible) {
      if (E->Cat && E->Cat != &GeneralOptionsCategory &&
          E->Cat != &clv2::GenericOptionsCategory) {
        ToolCat = E->Cat;
        break;
      }
    }
    if (ToolCat) {
      bool HasGeneralOpt = false;
      for (const OptionEntry *E : Visible)
        if (E->Cat == &GeneralOptionsCategory) {
          HasGeneralOpt = true;
          break;
        }
      if (!HasGeneralOpt) {
        for (OptionEntry &E : Entries)
          if (E.Cat == &GeneralOptionsCategory)
            E.Cat = ToolCat;
      }
    }
  }

  // Collect distinct categories in declaration order (stable, not
  // alphabetical).
  std::vector<const OptionCategory *> Cats;
  for (const OptionEntry *E : Visible) {
    bool Seen = false;
    for (const OptionCategory *C : Cats)
      if (C == E->Cat) {
        Seen = true;
        break;
      }
    if (!Seen)
      Cats.push_back(E->Cat);
  }
  // Sort categories alphabetically.
  llvm::sort(Cats, [](const OptionCategory *A, const OptionCategory *B) {
    return StringRef(A->Name) < StringRef(B->Name);
  });

  auto printSection = [&](const OptionCategory *Cat, bool SuppressHeader,
                          bool IsLast = false) {
    SmallVector<const OptionEntry *, 16> CatEntries;
    for (const OptionEntry *E : Visible)
      if (E->Cat == Cat)
        CatEntries.push_back(E);
    // Enum group members keep their registration order within the group.
    // The group sorts among non-group entries by the alphabetically first
    // member's name.
    // Use the alphabetically-first group member's name as sort key,
    // unless GroupSortKeyOverride is set.
    StringRef GroupSortKey;
    for (const OptionEntry *E : CatEntries)
      if (E->isEnumGroupMember()) {
        if (!E->groupSortKeyOverride().empty()) {
          GroupSortKey = E->groupSortKeyOverride();
          break;
        }
        if (GroupSortKey.empty() || E->name() < GroupSortKey)
          GroupSortKey = E->name();
      }
    llvm::stable_sort(
        CatEntries, [GroupSortKey](const OptionEntry *A, const OptionEntry *B) {
          StringRef AKey = A->isEnumGroupMember() ? GroupSortKey : A->name();
          StringRef BKey = B->isEnumGroupMember() ? GroupSortKey : B->name();
          return AKey < BKey;
        });
    if (!SuppressHeader) {
      OS << Cat->Name << ":\n";
      if (Cat->Desc && Cat->Desc[0])
        OS << Cat->Desc << "\n";
      OS << "\n";
    }
    for (const OptionEntry *E : CatEntries)
      printEntry(E, MaxArgLen);
    if (!IsLast)
      OS << "\n";
  };

  if (!Cats.empty()) {
    bool SingleCategory = (Cats.size() == 1);
    OS << "\n";
    for (std::size_t CI = 0; CI < Cats.size(); ++CI) {
      const OptionCategory *C = Cats[CI];
      bool IsLast = (CI == Cats.size() - 1);
      printSection(C, /*SuppressHeader=*/SingleCategory &&
                          C == &GeneralOptionsCategory,
                   IsLast);
    }
  }

  if (!ExtraHelp.empty())
    OS << ExtraHelp;
}

//===----------------------------------------------------------------------===//
// Runtime parser
//===----------------------------------------------------------------------===//

namespace {
/// Adaptive name-to-entry lookup for one parse.
///
/// A linear scan of the whole entry list per argv token makes parse time depend
/// on where an option happens to sit in the vector.  A hash index fixes that
/// but costs more to build than a short command line saves, so the index is
/// built lazily: the first few lookups scan linearly, and once enough have
/// happened to amortise it the map is built and used for the rest of the parse.
/// Short command lines pay nothing extra; long ones get O(1) lookups.
///
/// Holds indices into the entry list, so it must only be used after every
/// mutation of that list (dynamic drain, subcommand merge, alias resolution).
class EntryIndex {
  const std::vector<OptionEntry> &Entries;

  /// When a CompiledParser supplied one, its registry-prefix index -- already
  /// built, covering [0, BakedCount).  The lazy map below then only has to
  /// cover the tail (builtins, alias proxies, dynamic entries), which is small
  /// for most tools, so in practice it is never built at all.
  const BakedNameIndex *Baked = nullptr;
  std::size_t BakedFirst = 0;
  std::size_t BakedCount = 0;
  std::size_t bakedEnd() const { return BakedFirst + BakedCount; }

  llvm::StringMap<llvm::SmallVector<unsigned, 1>> ByName;
  bool NameMapBuilt = false;
  unsigned Lookups = 0;

  /// Prefix entries, longest name first so the first match is the longest.
  /// Built together with SinkEntry on first use; both are cheap (no hashing).
  llvm::SmallVector<unsigned, 4> PrefixEntries;
  int SinkEntry = -1;
  bool ScanBuilt = false;

  /// Lookups to allow before paying to build the hash map.  Measured at `opt`
  /// scale (3445 entries): the map costs ~0.3 ms to build and saves ~13 us per
  /// lookup, so a command line shorter than this can only lose by building it,
  /// and anything longer wins.  Set below the measured break-even so that
  /// building can only help.
  static constexpr unsigned BuildThreshold = 16;

  static bool isNameAddressable(const OptionEntry &E) {
    return !E.isPositional() || E.isPositionalEatsArgs();
  }

  void buildNameMap() {
    // Size the table up front: rehashing during growth is a measurable share
    // of the build cost.  A StringMap beats a sorted flat array here because
    // SmallVector<unsigned,1> keeps the common single-index case inline, so
    // there is no per-entry allocation a sort would save, and hashing costs
    // less than the string compares a sort needs.
    ByName = llvm::StringMap<llvm::SmallVector<unsigned, 1>>(Entries.size() -
                                                             BakedCount);
    for (unsigned I = bakedEnd(), N = Entries.size(); I < N; ++I)
      if (isNameAddressable(Entries[I]))
        ByName[Entries[I].name()].push_back(I);
#ifndef NDEBUG
    // Name resolution is first-wins, so a duplicate silently makes the later
    // option unreachable.  With registries composable at runtime there is no
    // single choke point that would catch it, so flag it here.
    bool AnyDuplicate = false;
    for (const auto &KV : ByName) {
      if (KV.second.size() < 2)
        continue;
      AnyDuplicate = true;
      llvm::errs() << "clv2: duplicate command line option '" << KV.first()
                   << "' -- only the first is reachable:\n";
      for (unsigned I : KV.second)
        llvm::errs() << "  " << Entries[I].name() << ": "
                     << Entries[I].description() << "\n";
    }
    if (AnyDuplicate)
      llvm_unreachable("duplicate command line option name");
#endif
    NameMapBuilt = true;
  }

  void buildScanLists() {
    if (Baked) {
      for (unsigned I : Baked->prefixEntries())
        PrefixEntries.push_back(I + BakedFirst);
      if (Baked->sinkEntry() >= 0)
        SinkEntry = Baked->sinkEntry() + static_cast<int>(BakedFirst);
      // Entries before the baked block (the prepended builtins) are never
      // Prefix or Sink options, so they need no scan here.
    }
    for (unsigned I = bakedEnd(), N = Entries.size(); I < N; ++I) {
      if (Entries[I].isPrefix())
        PrefixEntries.push_back(I);
      if (SinkEntry < 0 && (Entries[I].miscFlagsBits() & Sink))
        SinkEntry = static_cast<int>(I);
    }
    // Stable, so equal-length ties keep declaration order — matching the old
    // scan, which only replaced its best match on a strictly longer name.
    llvm::stable_sort(PrefixEntries, [this](unsigned A, unsigned B) {
      return Entries[A].name().size() > Entries[B].name().size();
    });
    ScanBuilt = true;
  }

public:
  EntryIndex(const std::vector<OptionEntry> &Entries,
             const BakedNameIndex *Baked = nullptr, std::size_t BakedFirst = 0,
             std::size_t BakedCount = 0)
      : Entries(Entries), Baked(Baked), BakedFirst(Baked ? BakedFirst : 0),
        BakedCount(Baked ? BakedCount : 0) {}

  /// Find an OptionEntry by name.  When SubStart > 0, subcommand entries
  /// (indices >= SubStart) shadow global ones.
  ///
  /// \p Es must be the same vector this index was built over -- the stored
  /// indices are meaningless against any other.
  OptionEntry *find(std::vector<OptionEntry> &Es, StringRef Name,
                    std::size_t SubStart) {
    assert(&Es == &Entries && "EntryIndex used against a different vector");
    // Subcommand entries are appended past SubStart and shadow global ones.
    if (SubStart > 0)
      if (OptionEntry *E =
              scan(Es, Name, std::max(SubStart, bakedEnd()), Es.size()))
        return E;

    // Otherwise keep the declaration order the entry vector already has:
    // prepended builtins, then the registry block, then everything appended
    // after it (alias proxies, dynamic entries, drained registries).
    const std::size_t Limit =
        SubStart ? std::min(SubStart, Es.size()) : Es.size();
    if (OptionEntry *E = scan(Es, Name, 0, std::min(BakedFirst, Limit)))
      return E;
    if (Baked)
      for (unsigned I : Baked->lookup(Name)) {
        const std::size_t Abs = BakedFirst + I;
        if (Abs < Limit)
          return &Es[Abs];
        break;
      }
    return findAfterBaked(Es, Name, Limit);
  }

private:
  /// Plain linear scan of [First, Last).
  OptionEntry *scan(std::vector<OptionEntry> &Es, StringRef Name,
                    std::size_t First, std::size_t Last) {
    for (std::size_t I = First; I < Last && I < Es.size(); ++I)
      if (isNameAddressable(Es[I]) && Es[I].name() == Name)
        return &Es[I];
    return nullptr;
  }

  /// Search the entries after the baked block.  With no baked index that is
  /// the whole list, which is the original behaviour: scan linearly, and
  /// switch to a hash map once enough lookups have happened to pay for it.
  OptionEntry *findAfterBaked(std::vector<OptionEntry> &Es, StringRef Name,
                              std::size_t Limit) {
    if (bakedEnd() >= Es.size())
      return nullptr;
    if (!NameMapBuilt && ++Lookups > BuildThreshold)
      buildNameMap();
    if (!NameMapBuilt)
      return scan(Es, Name, bakedEnd(), Limit);

    auto It = ByName.find(Name);
    if (It == ByName.end())
      return nullptr;
    for (unsigned I : It->second) { // ascending
      if (I < Limit)
        return &Es[I];
      break;
    }
    return nullptr;
  }

public:
  OptionEntry *sink(std::vector<OptionEntry> &Es) {
    assert(&Es == &Entries && "EntryIndex used against a different vector");
    if (!ScanBuilt)
      buildScanLists();
    return SinkEntry < 0 ? nullptr : &Es[SinkEntry];
  }

  /// Longest-prefix match among prefix entries.
  ///
  /// Shadowing mirrors find(): with a subcommand active, its own prefix
  /// entries (indices >= SubStart) are tried before the global ones, so a
  /// subcommand can override a global prefix option of the same name.
  std::pair<OptionEntry *, StringRef> findPrefix(std::vector<OptionEntry> &Es,
                                                 StringRef ArgName,
                                                 std::size_t SubStart = 0) {
    assert(&Es == &Entries && "EntryIndex used against a different vector");
    if (!ScanBuilt)
      buildScanLists();
    // With no subcommand active every entry is global; note that `I >= 0` is
    // vacuously true for unsigned I, so SubStart must be tested separately.
    auto IsSub = [SubStart](unsigned I) {
      return SubStart > 0 && I >= SubStart;
    };
    auto Match = [&](bool WantSub) -> std::pair<OptionEntry *, StringRef> {
      for (unsigned I : PrefixEntries) {
        if (IsSub(I) != WantSub)
          continue;
        if (ArgName.starts_with(Es[I].name()))
          return {&Es[I], ArgName.substr(Es[I].name().size())};
      }
      return {nullptr, {}};
    };
    if (SubStart > 0)
      if (auto Sub = Match(/*WantSub=*/true); Sub.first)
        return Sub;
    return Match(/*WantSub=*/false);
  }
};
} // namespace

/// Prefix-format fallback: scan for an entry where E.IsPrefix and E.Name is a
/// prefix of ArgName.  Returns the longest match so that e.g. "-Ifoo" matches
/// an "-I" prefix option rather than a hypothetical "-If" option.
std::pair<OptionEntry *, StringRef>
clv2::detail::findPrefixEntry(std::vector<OptionEntry> &Entries,
                              StringRef ArgName) {
  OptionEntry *Best = nullptr;
  StringRef BestSuffix;
  for (OptionEntry &E : Entries) {
    if (!E.isPrefix())
      continue;
    if (ArgName.starts_with(E.name())) {
      if (!Best || E.name().size() > Best->name().size()) {
        Best = &E;
        BestSuffix = ArgName.substr(E.name().size());
      }
    }
  }
  return {Best, BestSuffix};
}

/// Report an error to *Errs, or to llvm::errs() when the caller supplied none.
/// Returns nothing: it used to return a constant false purely so callers could
/// write `HadError = !reportError(...)`, which reads like the result means
/// something.
static void reportError(StringRef Msg, raw_ostream *Errs) {
  raw_ostream &OS = Errs ? *Errs : llvm::errs();
  OS << Msg;
}

/// Writers to every process-wide registration list share one mutex.  It is
/// taken only to append, and by readers only to copy out element addresses --
/// never while running registry or user code.
static std::mutex &getRegistrationMutex() {
  static std::mutex M;
  return M;
}

/// Append-only registration list.
///
/// std::deque, not std::vector: push_back keeps references to existing
/// elements valid, so a reader holding an address is unaffected by a
/// concurrent append.  Indexing is *not* safe concurrently (operator[] walks
/// the deque's block map, which push_back may reallocate), so readers take a
/// snapshot of addresses under the mutex and then work through those.  Because
/// nothing is ever erased, an index is stable for the life of the process.
template <typename T> class RegistrationList {
  std::deque<T> Items;

public:
  void append(T V) {
    std::lock_guard<std::mutex> Lock(getRegistrationMutex());
    Items.push_back(std::move(V));
  }

  /// Addresses of the elements present now.  Safe to use after the lock is
  /// dropped; later appends neither move nor invalidate them.
  std::vector<T *> snapshot() {
    std::lock_guard<std::mutex> Lock(getRegistrationMutex());
    std::vector<T *> Out;
    Out.reserve(Items.size());
    for (T &I : Items)
      Out.push_back(&I);
    return Out;
  }
};

static RegistrationList<clv2::detail::OptionEntry> &getDynamicEntries();
static RegistrationList<std::function<void()>> &getDynamicPostParseCallbacks();
static RegistrationList<clv2::detail::OptionEntry> &
getEssentialDynamicEntries();
static RegistrationList<std::function<void()>> &
getEssentialPostParseCallbacks();
static RegistrationList<clv2::detail::DynamicRegistration> &
getDynamicRegistrations();

/// Number of parses currently walking a registration list.
///
/// Appending concurrently is memory-safe: RegistrationList holds a deque, so
/// push_back cannot invalidate the addresses a drain already took, and both
/// append() and snapshot() run under the shared mutex.  What is *not* safe is
/// the meaning: a drain snapshots the list once, so an option registered after
/// that point is silently absent from that parse, and whether it appears at
/// all depends on timing.  The asserts below catch that, which is why they
/// survive the synchronisation work rather than being made redundant by it.
static std::atomic<unsigned> DrainsInFlight{0};

namespace {
struct DrainScope {
  DrainScope() { ++DrainsInFlight; }
  ~DrainScope() { --DrainsInFlight; }
};
} // namespace

/// Dump option values after parsing, for --print-all-options / --print-options.
///
/// One line per option, name column padded to
/// the widest name, hidden options included.  With \p AllOptions false only
/// the options that actually appeared on the command line are listed.
static void printOptionValues(const std::vector<OptionEntry> &Entries,
                              bool AllOptions, raw_ostream &OS) {
  std::vector<const OptionEntry *> Sorted;
  Sorted.reserve(Entries.size());
  for (const OptionEntry &E : Entries) {
    if (!E.Static || !E.Static->PrintValueFn || !E.ParseSlot)
      continue;
    if (!AllOptions && !(E.OccurrenceCount && *E.OccurrenceCount))
      continue;
    Sorted.push_back(&E);
  }
  llvm::sort(Sorted, [](const OptionEntry *A, const OptionEntry *B) {
    return A->Static->Name < B->Static->Name;
  });

  std::size_t MaxNameLen = 0;
  for (const OptionEntry *E : Sorted)
    MaxNameLen = std::max(MaxNameLen, E->Static->Name.size());

  for (const OptionEntry *E : Sorted) {
    OS << "  --" << E->Static->Name;
    OS.indent(MaxNameLen - E->Static->Name.size());
    OS << " = ";
    E->Static->PrintValueFn(E->Static->Desc, E->ParseSlot, OS);
    OS << '\n';
  }
}

/// dlopen any -load arguments before dynamic registrations are snapshotted.
/// No-op unless this parse actually has a -load option, so a stray -load on a
/// tool without one still reports as an unknown argument.
static void preloadPlugins(int argc, const char *const *argv) {
  if (!llvm::pluginLoaderOptionRegistered())
    return;
  for (int I = 1; I < argc; ++I) {
    StringRef Arg = argv[I];
    if (!Arg.consume_front("--") && !Arg.consume_front("-"))
      continue;
    if (!Arg.consume_front("load"))
      continue;
    StringRef File;
    if (Arg.consume_front("="))
      File = Arg;
    else if (Arg.empty() && I + 1 < argc)
      File = argv[++I];
    else
      continue; // -loadsomethingelse
    if (File.empty())
      continue;
    PluginLoader PL;
    PL = File.str();
  }
}

bool clv2::detail::runParser(std::vector<OptionEntry> &GlobalEntries,
                             std::vector<SubCommandSpec> &SubCommands, int argc,
                             const char *const *argv, raw_ostream *Errs,
                             ParseFrame &Frame, bool DrainDynamic) {
  // Initialize optional subsystems that register essential entries.
  //
  // Ordering is load-bearing: this registers into an essential list, so it has
  // to run before the snapshots below and before DrainScope raises
  // DrainsInFlight, or it would both miss this parse and trip the
  // registration assert.  Do not sink it into the block below.
  initWithColorOptions();

  // -load brings in a shared object whose static initialisers register more
  // options.  Those initialisers run at dlopen, which the option's own callback
  // performs part-way through the parse -- after the snapshots below have been
  // taken, so the plugin's options would be missing from this parse entirely.
  // Load them up front instead, and only when the tool actually offers -load.
  preloadPlugins(argc, argv);

  // Instantiate per-parse storage for every dynamically-registered registry.
  // Each parse gets its own ParsedOptions, so the entries built here point at
  // slots owned by this frame rather than at a single process-wide instance.
  // Essential registrations (e.g. --color) are always drained; the rest only
  // when the parser opted in.
  // Snapshots outlive the drain scope: the hand-built lists are appended after
  // it, and the registration index recorded in Frame.DynamicStorages has to
  // match what publishDynamicStorages later resolves.
  std::vector<OptionEntry *> EssentialSnap =
      getEssentialDynamicEntries().snapshot();
  std::vector<OptionEntry *> GlobalDynSnap =
      DrainDynamic ? getDynamicEntries().snapshot()
                   : std::vector<OptionEntry *>();
  {
    DrainScope Draining;
    std::vector<DynamicRegistration *> Registrations =
        getDynamicRegistrations().snapshot();
    // Reserve for everything appended below: the dynamic registries plus the
    // hand-built essential/global entries.
    {
      std::size_t Expected = GlobalEntries.size();
      for (const DynamicRegistration *R : Registrations)
        if (R->Essential || DrainDynamic)
          Expected += R->NumOptions;
      Expected += EssentialSnap.size() + GlobalDynSnap.size();
      GlobalEntries.reserve(Expected);
    }
    for (std::size_t I = 0, N = Registrations.size(); I < N; ++I) {
      const DynamicRegistration &R = *Registrations[I];
      if (!R.Essential && !DrainDynamic)
        continue;
      std::unique_ptr<ParsedOptionsBase> Storage = R.MakeStorage();
      // TODO: aliases and subcommands declared by a dynamically-registered
      // registry are discarded, matching the previous behaviour.  Wiring them
      // up would change --help output, so it is left for a separate change.
      std::vector<AliasEntry> IgnoredAliases;
      std::vector<SubCommandSpec> IgnoredSubSpecs;
      R.BuildInto(*Storage, GlobalEntries, IgnoredAliases, IgnoredSubSpecs);
      Frame.DynamicStorages.emplace_back(I, std::move(Storage));
    }
  }

  // Hand-built dynamic entries own their own slots (the caller allocated them),
  // so they are inherently process-global; they are appended as-is.
  for (const OptionEntry *E : EssentialSnap)
    GlobalEntries.push_back(*E);
  for (const OptionEntry *E : GlobalDynSnap)
    GlobalEntries.push_back(*E);

  // If HideAllRegistered, mark all drained entries as Hidden too,
  // UNLESS they have their own OptionCategory (e.g. IR2Vec, MIR2Vec)
  // which should be preserved for proper category display.
  if (Frame.HideAllRegistered) {
    for (auto &E : GlobalEntries) {
      // --color is a registered option rather than a builtin, but tools that
      // hide everything still expect it to stay visible.
      if (E.HiddenFlag == NotHidden && !E.name().empty() && !E.Cat &&
          !E.isPositional() && !E.info().IsBuiltin && E.name() != "color")
        E.HiddenFlag = Hidden;
    }
  }

  // showOptions re-reveals named entries.  This applies whether or not
  // everything else was just hidden -- tools using hideUnrelatedOptions need
  // it to reveal Hidden dynamic entries in allowed categories.
  if (!Frame.ShownNames.empty())
    for (auto &E : GlobalEntries)
      for (StringRef SN : Frame.ShownNames)
        if (E.name() == SN && E.HiddenFlag == Hidden)
          E.HiddenFlag = NotHidden;

  // Error messages produced during parsing name the program, so record it
  // before any option is handled.
  if (argc > 0 && argv[0])
    Frame.ProgramName = llvm::sys::path::filename(argv[0]);

  // 1. Subcommand detection: if argv[1] names a known subcommand, activate it
  //    and merge its option entries into the working set.
  std::vector<OptionEntry> *ActiveEntries = &GlobalEntries;
  Frame.ActiveEntries = &GlobalEntries;
  Frame.ActiveSubCommandName.clear();
  std::vector<OptionEntry> MergedEntries;

  // Snapshot rather than binding a reference: registerRuntimeSubcommand is
  // public, so a concurrent append would otherwise invalidate a live reference
  // into the container.  RuntimeSubEntries is sized from and indexed alongside
  // this snapshot.
  std::vector<RuntimeSubCommandEntry *> RuntimeSubs =
      getRuntimeSubcmdRegistry().snapshot();
  std::vector<std::vector<OptionEntry>> RuntimeSubEntries(RuntimeSubs.size());

  // Publish the subcommands visible to this parse into the frame so the help
  // printers can list them without consulting (or extending) the global
  // registry.  Compile-time specs take precedence over an identically-named
  // runtime registration.
  for (const SubCommandSpec &SC : SubCommands)
    Frame.Subcommands.emplace_back(SC.Name, SC.Desc);
  for (const RuntimeSubCommandEntry *RSC : RuntimeSubs)
    if (llvm::none_of(SubCommands, [&](const SubCommandSpec &SC) {
          return SC.Name == RSC->Name;
        }))
      Frame.Subcommands.emplace_back(RSC->Name, RSC->Desc);

  bool HasAnySub = !SubCommands.empty() || !RuntimeSubs.empty();
  if (HasAnySub && argc >= 2) {
    StringRef FirstArg = argv[1];
    bool SubCommandMatched = false;
    // Skip subcommand detection for flags (start with '-') and built-ins.
    bool LooksLikeSubcommand =
        !FirstArg.empty() && !FirstArg.starts_with("-") &&
        FirstArg != "--help" && FirstArg != "--help-hidden" &&
        FirstArg != "--version";

    // Check compile-time subcommands first.
    for (SubCommandSpec &Sub : SubCommands) {
      if (FirstArg == Sub.Name) {
        std::vector<OptionEntry> SubEntries = Sub.BuildAndInit();
        MergedEntries = GlobalEntries;
        Frame.GlobalEntryCount = MergedEntries.size();
        MergedEntries.insert(MergedEntries.end(),
                             std::make_move_iterator(SubEntries.begin()),
                             std::make_move_iterator(SubEntries.end()));
        if (!Sub.Aliases.empty())
          resolveAliases(MergedEntries, Sub.Aliases, Frame, Errs);
        ActiveEntries = &MergedEntries;
        Frame.ActiveEntries = &MergedEntries;
        Frame.ActiveSubCommandName = Sub.Name.str();
        ++argv;
        --argc;
        SubCommandMatched = true;
        break;
      }
    }

    // Check runtime subcommands.
    if (!SubCommandMatched) {
      for (std::size_t Si = 0; Si < RuntimeSubs.size(); ++Si) {
        const RuntimeSubCommandEntry &RSC = *RuntimeSubs[Si];
        if (FirstArg != RSC.Name)
          continue;

        // Copy subcommand option entries directly.
        auto &Entries = RuntimeSubEntries[Si];
        Entries = RSC.Options;

        MergedEntries = GlobalEntries;
        // Don't set GlobalEntryCount for runtime subcommands — they share
        // the global option scope (positionals, --help, etc.) and only add
        // a few subcommand-specific options.
        MergedEntries.insert(MergedEntries.end(),
                             std::make_move_iterator(Entries.begin()),
                             std::make_move_iterator(Entries.end()));
        ActiveEntries = &MergedEntries;
        Frame.ActiveEntries = &MergedEntries;
        Frame.ActiveSubCommandName = RSC.Name;
        ++argv;
        --argc;
        SubCommandMatched = true;
        break;
      }
    }

    // If it looks like a subcommand (no leading dash) but didn't match,
    // only error if there are no positional entries that could consume it.
    // Tools like clang-ssaf-linker have both subcommands and positional
    // input files — an unmatched first arg should be treated as a positional.
    if (LooksLikeSubcommand && !SubCommandMatched) {
      bool HasPositional =
          llvm::any_of(*ActiveEntries,
                       [](const OptionEntry &E) { return E.isPositional(); });
      if (!HasPositional) {
        raw_ostream &ErrOS = Errs ? *Errs : llvm::errs();
        ErrOS << "error: Unknown subcommand '" << FirstArg << "'\n";
        if (Frame.OnErr == OnError::ExitProcess)
          std::exit(1);
        return false;
      }
    }
  }

  // 2. Apply defaults for every option in scope before parsing.  This must
  //    iterate *ActiveEntries rather than GlobalEntries: runtime subcommand
  //    entries are merged in above and would otherwise keep whatever their
  //    shared storage last held.  Re-applying to compile-time subcommand
  //    entries (already initialised inside BuildAndInit) is harmless because
  //    applyDefault() is idempotent.
  {
    // Registry storages come out of MakeStorage already default-initialised;
    // re-applying is pure overhead.  Everything else (dynamic entries, drained
    // registries, merged subcommand entries) still needs it, since their
    // storage may hold whatever a previous parse left behind.
    const std::size_t SkipFirst = Frame.DefaultedFirst;
    const std::size_t SkipLast = Frame.DefaultedFirst + Frame.DefaultedCount;
    for (std::size_t I = 0, N = ActiveEntries->size(); I < N; ++I)
      if (I < SkipFirst || I >= SkipLast)
        (*ActiveEntries)[I].applyDefault();
  }

  // Determine the error output stream.
  raw_ostream &ErrStream = Errs ? *Errs : llvm::errs();
  ParseDiag Diag{ErrStream, Frame.ProgramName};

  bool HadError = false;

  // Index the finalised entry list.  Everything above may still append to it
  // (dynamic drain, subcommand merge, alias resolution); nothing below does.
  EntryIndex Index(*ActiveEntries, Frame.Baked, Frame.BakedFirst,
                   Frame.BakedCount);

  // Collect positional entries in declaration order for later distribution.
  // When a subcommand is active, skip top-level positionals so subcommand
  // positionals receive the args.
  std::vector<OptionEntry *> Positionals;
  OptionEntry *ConsumeAfterEntry = nullptr;
  bool InSubCmd = !Frame.ActiveSubCommandName.empty();
  for (std::size_t I = 0, N = ActiveEntries->size(); I < N; ++I) {
    OptionEntry &E = (*ActiveEntries)[I];
    if (E.isPositional()) {
      if (InSubCmd && I < Frame.GlobalEntryCount)
        continue;
      if (E.occurrencesFlag() == ConsumeAfter)
        ConsumeAfterEntry = &E;
      else
        Positionals.push_back(&E);
    }
  }

  // Positional values collected during the main loop (value + original argv
  // index).
  std::vector<std::pair<StringRef, int>> PositionalArgs;

  // Compute NumPositionalRequired for ConsumeAfter sweep detection.
  // A positional "requires" a value if its occurrence flag is Required or
  // OneOrMore.
  unsigned NumPositionalRequired = 0;
  if (ConsumeAfterEntry) {
    for (const OptionEntry *PE : Positionals) {
      if (PE->occurrencesFlag() == Required ||
          PE->occurrencesFlag() == OneOrMore)
        ++NumPositionalRequired;
    }
  }

  // Parse a value into an entry, handling CommaSeparated splitting, and
  // record occurrence count, position, and element positions.
  auto ParseAndRecord = [&](OptionEntry *E, StringRef Val, int ArgI) {
    if ((E->miscFlagsBits() & CommaSeparated) && !Val.empty()) {
      SmallVector<StringRef, 8> Tokens;
      Val.split(Tokens, ',', /*MaxSplit=*/-1, /*KeepEmpty=*/true);
      for (StringRef Token : Tokens) {
        if (!E->parse(Token, Diag))
          HadError = true;
        else {
          ++(*E->OccurrenceCount);
          if (E->LastPosition)
            *E->LastPosition = static_cast<unsigned>(ArgI);
          if (E->ElementPositions)
            E->ElementPositions->push_back(static_cast<unsigned>(ArgI));
        }
      }
    } else {
      if (!E->parse(Val, Diag))
        HadError = true;
      else {
        ++(*E->OccurrenceCount);
        if (E->LastPosition)
          *E->LastPosition = static_cast<unsigned>(ArgI);
        if (E->ElementPositions)
          E->ElementPositions->push_back(static_cast<unsigned>(ArgI));
      }
    }
  };

  bool SeenDoubleDash = false; // '--' encountered: treat rest as positional
  OptionEntry *ActiveEatArgsEntry = nullptr;

  // 3. Main argument loop (skip argv[0] = program name).
  int I = 1;
  while (I < argc) {
    Frame.CurArgPosition = static_cast<unsigned>(I);
    StringRef Arg = argv[I];

    // '--' signals end of option processing.
    if (!SeenDoubleDash && Arg == "--") {
      SeenDoubleDash = true;
      ++I;
      continue;
    }

    // PositionalEatsArgs: when an eating positional is active, all subsequent
    // args (including -flags) are consumed by it — unless the arg matches
    // another PositionalEatsArgs entry, which steals the active role.
    if (ActiveEatArgsEntry) {
      bool IsOption = !SeenDoubleDash && Arg.starts_with("-") && Arg.size() > 1;
      if (IsOption) {
        StringRef EatName = Arg.ltrim('-');
        if (EatName.contains('='))
          EatName = EatName.substr(0, EatName.find('='));
        OptionEntry *Candidate =
            Index.find(*ActiveEntries, EatName, Frame.GlobalEntryCount);
        if (Candidate && Candidate->isPositional() &&
            Candidate->isPositionalEatsArgs()) {
          ActiveEatArgsEntry = Candidate;
          ++I;
          continue;
        }
      }
      ParseAndRecord(ActiveEatArgsEntry, Arg, I);
      ++I;
      continue;
    }

    // Positional argument: doesn't start with '-', or we've seen '--'.
    bool LooksLikeOption =
        !SeenDoubleDash && Arg.starts_with("-") && Arg.size() > 1;
    if (!LooksLikeOption) {
      PositionalArgs.push_back({Arg, I});
      ++I;

      // ConsumeAfter sweep: once enough positional values have been collected
      // to satisfy all required positionals, sweep ALL remaining argv tokens
      // (including -flags) into PositionalArgs: everything after the last
      // required positional is forwarded to the ConsumeAfter option.
      if (ConsumeAfterEntry && PositionalArgs.size() >= NumPositionalRequired) {
        for (; I < argc; ++I)
          PositionalArgs.push_back({StringRef(argv[I]), I});
        break; // exit the main argument loop
      }

      continue;
    }

    // Strip leading '-' or '--'.
    StringRef FullArgName = Arg.ltrim('-');

    // Split on the first '=' to separate name from inline value.
    StringRef InlineVal;
    StringRef ArgName = FullArgName;
    auto EqPos = ArgName.find('=');
    bool HasInlineVal = (EqPos != StringRef::npos);
    if (HasInlineVal) {
      InlineVal = ArgName.substr(EqPos + 1);
      ArgName = ArgName.substr(0, EqPos);
    }

    // Look up the option.
    OptionEntry *E =
        Index.find(*ActiveEntries, ArgName, Frame.GlobalEntryCount);

    // Activate PositionalEatsArgs: when the matched option is a positional
    // with EatsArgs, set it as the active eater. No value via '=' is allowed.
    if (E && E->isPositional() && E->isPositionalEatsArgs()) {
      if (HasInlineVal) {
        std::string PN = (argc > 0 && argv[0])
                             ? sys::path::filename(argv[0]).str()
                             : std::string("program");
        ErrStream << PN << ": This argument does not take a value.\n"
                  << "Use '" << PN << " -" << E->name()
                  << " <value>' instead.\n";
        HadError = true;
      }
      ActiveEatArgsEntry = E;
      ++I;
      continue;
    }

    // For AlwaysPrefix options the value is everything
    // after the option name in the token — including any '='.
    // e.g. -D=10 → ArgName="D", but InlineVal must be "=10", not "10".
    // Fix up InlineVal by re-slicing from FullArgName when we found the
    // option by its exact name but the token had a '=' separator.
    // Note: Prefix options (-I path) do NOT do this — they treat -I=path
    // as -I with value "path" (the '=' is stripped), same as -I path.
    if (E && E->isAlwaysPrefix() && HasInlineVal)
      InlineVal = FullArgName.substr(E->name().size());

    // Prefix-format fallback: -Ipath or -DKEY=VAL where "I"/"D" is the option
    // name. When there was no "=" separator, match against ArgName. When there
    // was a "=" separator (e.g. -DKEY=VAL), try matching against the full
    // unsplit name (FullArgName = "DKEY=VAL") so that prefix "D" matches with
    // suffix "KEY=VAL".
    if (!E) {
      if (!HasInlineVal) {
        auto [PE, Suffix] =
            Index.findPrefix(*ActiveEntries, ArgName, Frame.GlobalEntryCount);
        if (PE) {
          E = PE;
          InlineVal = Suffix;
          HasInlineVal = true;
        }
      } else {
        // Try prefix match on the full unsplit arg: e.g. "DKEY=VAL" against
        // prefix "D", yielding suffix "KEY=VAL".
        auto [PE, Suffix] = Index.findPrefix(*ActiveEntries, FullArgName,
                                             Frame.GlobalEntryCount);
        if (PE) {
          E = PE;
          InlineVal = Suffix;
          HasInlineVal = true;
        }
      }
    }
    if (!E) {
      // Grouping: -lp → -l -p.  Only applies when:
      //   (1) no '=' inline value, (2) single dash (not '--'), (3) ≥2 chars,
      //   (4) every char maps to a single-char grouping option.
      if (!HasInlineVal && !Arg.starts_with("--") && ArgName.size() >= 2) {
        bool AllGrouping = true;
        SmallVector<OptionEntry *, 8> GroupOpts;
        for (std::size_t Ci = 0; Ci < ArgName.size(); ++Ci) {
          StringRef Single(ArgName.data() + Ci, 1);
          OptionEntry *CE =
              Index.find(*ActiveEntries, Single, Frame.GlobalEntryCount);
          if (CE && (CE->miscFlagsBits() & Grouping)) {
            GroupOpts.push_back(CE);
          } else {
            AllGrouping = false;
            break;
          }
        }
        if (AllGrouping) {
          unsigned ArgI = I;
          ++I; // consume the option token
          for (OptionEntry *GE : GroupOpts) {
            if (!GE->parse(StringRef{}, Diag)) {
              ErrStream << "error: invalid value for grouped option -"
                        << GE->name() << "\n";
              HadError = true;
            } else {
              if (GE->OccurrenceCount)
                ++(*GE->OccurrenceCount);
              if (GE->LastPosition)
                *GE->LastPosition = static_cast<unsigned>(ArgI);
            }
          }
          continue;
        }
      }

      // Check for a Sink entry — it collects all unrecognized options.
      // The sink receives the full
      // original token (dashes and any =val included) and never consumes the
      // following argument: an unrecognized option carries no information
      // about whether it takes a separate value, so anything that follows must
      // stay available to the normal option/positional handling below.
      if (OptionEntry *Sink = Index.sink(*ActiveEntries)) {
        if (!Sink->parse(Arg, Diag))
          HadError = true;
        ++I;
        continue;
      }
      // ConsumeAfter: when a ConsumeAfter entry exists, unknown options are
      // collected as positional args rather than rejected: all arguments
      // after the last required positional are forwarded, -flags included.
      if (ConsumeAfterEntry) {
        PositionalArgs.push_back({Arg, I});
        ++I;
        continue;
      }
      {
        std::string PN = (argc > 0 && argv[0])
                             ? sys::path::filename(argv[0]).str()
                             : std::string("program");
        std::string Msg = PN + ": Unknown command line argument '" + Arg.str() +
                          "'.  Try: '" + PN + " --help'\n";
        // Suggest the nearest named option.
        StringRef NearestName;
        unsigned BestDist = UINT_MAX;
        for (const OptionEntry &E : *ActiveEntries) {
          if (E.name().empty() || E.isPositional())
            continue;
          unsigned D =
              ArgName.edit_distance(E.name(), /*AllowReplacements=*/true,
                                    /*MaxEditDistance=*/BestDist);
          if (D < BestDist) {
            BestDist = D;
            NearestName = E.name();
          }
        }
        if (!NearestName.empty()) {
          Msg += PN + ": Did you mean '--" + NearestName.str() + "'?\n";
        }
        reportError(Msg, Errs);
        HadError = true;
        ++I;
        continue;
      }
    }

    // Determine the value string to parse.
    StringRef Val;
    switch (E->valueExpected()) {
    case ValueDisallowed:
      if (HasInlineVal) {
        ErrStream << "error: option -" << ArgName
                  << " does not accept a value\n";
        HadError = true;
      }
      Val = {}; // bool flags with ValueDisallowed pass empty → true
      break;

    case ValueRequired:
      if (HasInlineVal) {
        Val = InlineVal;
      } else if (E->isAlwaysPrefix()) {
        // AlwaysPrefix options require the value to be attached (-DVAL, not -D
        // VAL). A bare option token with no inline value is an error.
        if (argc > 0 && argv[0])
          ErrStream << sys::path::filename(argv[0]) << ": ";
        ErrStream << "for the " << (ArgName.size() == 1 ? "-" : "--") << ArgName
                  << " option: requires a value!\n";
        HadError = true;
        ++I;
        continue;
      } else {
        // Steal the next argument unconditionally.
        // A value can legitimately start with '-' (e.g. --test-arg
        // -check-prefixes=).
        if (I + 1 >= argc) {
          if (argc > 0 && argv[0])
            ErrStream << sys::path::filename(argv[0]) << ": ";
          ErrStream << "for the " << (ArgName.size() == 1 ? "-" : "--")
                    << ArgName << " option: requires a value!\n";
          HadError = true;
          ++I;
          continue;
        }
        ++I;
        Val = argv[I];
      }
      break;

    case ValueOptional:
      Val = HasInlineVal ? InlineVal : StringRef{};
      break;
    }

    ParseAndRecord(E, Val, I);

    ++I;
  }

  // 4. Distribute positional arguments.
  std::size_t PosIdx = 0; // index into PositionalArgs

  auto ParseOnePositional = [&](OptionEntry *PE, StringRef Val, int ArgI) {
    Frame.CurArgPosition = static_cast<unsigned>(ArgI);
    ParseAndRecord(PE, Val, ArgI);
  };

  if (ConsumeAfterEntry) {
    // ConsumeAfter distribution.
    // 1. Give values to required positionals first.
    for (OptionEntry *PE : Positionals) {
      if (PosIdx >= PositionalArgs.size())
        break;
      if (PE->occurrencesFlag() == Required ||
          PE->occurrencesFlag() == OneOrMore) {
        auto [Val, ArgI] = PositionalArgs[PosIdx++];
        ParseOnePositional(PE, Val, ArgI);
      }
    }
    // 2. If there is exactly one positional option, it's optional, and no
    //    values were assigned yet, give it the first value.
    if (Positionals.size() == 1 && PosIdx == 0 &&
        PosIdx < PositionalArgs.size()) {
      auto [Val, ArgI] = PositionalArgs[PosIdx++];
      ParseOnePositional(Positionals[0], Val, ArgI);
    }
    // 3. Give all remaining values to ConsumeAfter.
    while (PosIdx < PositionalArgs.size()) {
      auto [Val, ArgI] = PositionalArgs[PosIdx++];
      ParseOnePositional(ConsumeAfterEntry, Val, ArgI);
    }
  } else {
    // Normal positional distribution (no ConsumeAfter).
    for (OptionEntry *PE : Positionals) {
      if (PosIdx >= PositionalArgs.size())
        break;
      if (PE->occurrencesFlag() == ZeroOrMore ||
          PE->occurrencesFlag() == OneOrMore) {
        // Greedy: consume all remaining positionals into this option.
        while (PosIdx < PositionalArgs.size()) {
          auto [Val, ArgI] = PositionalArgs[PosIdx++];
          ParseOnePositional(PE, Val, ArgI);
        }
      } else {
        auto [Val, ArgI] = PositionalArgs[PosIdx++];
        ParseOnePositional(PE, Val, ArgI);
      }
    }
  }

  // Report errors for extra positional arguments that were not consumed.
  // If there are no unlimited positionals (ZeroOrMore/OneOrMore) and no
  // ConsumeAfter, extra positional args are errors.
  if (PosIdx < PositionalArgs.size() && !ConsumeAfterEntry) {
    bool HasUnlimitedPositionals = false;
    for (const OptionEntry *PE : Positionals) {
      if (PE->occurrencesFlag() == ZeroOrMore ||
          PE->occurrencesFlag() == OneOrMore) {
        HasUnlimitedPositionals = true;
        break;
      }
    }
    if (!HasUnlimitedPositionals) {
      std::string PN = (argc > 0 && argv[0])
                           ? sys::path::filename(argv[0]).str()
                           : std::string("program");
      while (PosIdx < PositionalArgs.size()) {
        auto [Val, ArgI] = PositionalArgs[PosIdx++];
        std::string Msg = PN + ": Unknown command line argument '" + Val.str() +
                          "'.  Try: '" + PN + " --help'\n";
        reportError(Msg, Errs);
        HadError = true;
      }
    }
  }

  // 5. Validate occurrence constraints (skip when help was printed).
  if (Frame.HelpPrinted)
    return true;
  for (int Pass = 0; Pass < 2; ++Pass) {
    for (std::size_t I = 0, N = ActiveEntries->size(); I < N; ++I) {
      OptionEntry &E = (*ActiveEntries)[I];
      if (Pass == 0 && !E.isPositional())
        continue;
      if (Pass == 1 && E.isPositional())
        continue;
      if (InSubCmd && I < Frame.GlobalEntryCount)
        continue;
      unsigned Count = *E.OccurrenceCount;
      bool NeedsAtLeastOne =
          (E.occurrencesFlag() == Required || E.occurrencesFlag() == OneOrMore);
      if (NeedsAtLeastOne && Count == 0) {
        if (E.isPositional()) {
          std::string PN = (argc > 0 && argv[0])
                               ? sys::path::filename(argv[0]).str()
                               : std::string("program");
          ErrStream << PN
                    << ": Not enough positional command line arguments "
                       "specified!\n"
                    << "Must specify at least 1 positional argument: See: "
                    << PN << " --help\n";
        } else {
          ErrStream << Frame.ProgramName << ": for the "
                    << (E.name().size() == 1 ? "-" : "--") << E.name()
                    << " option: must be specified at least once!\n";
        }
        HadError = true;
      }
      // Required means "at least once": repeats are allowed, last one wins.
    }
  }

  if (HadError) {
    if (Frame.OnErr == OnError::ExitProcess)
      std::exit(1);
    return false;
  }

  Frame.CurArgPosition = 0;

  // Write parsed values back into legacy globals, using *this* parse's
  // storage.  Note these globals stay process-wide: an Apply function is
  // fundamentally incompatible with two concurrent jobs wanting different
  // values for that option.  Per-parse readers should go through the
  // OptionsContext instead.
  {
    // The list is append-only, so an index taken during the drain still names
    // the same registration here.
    std::vector<DynamicRegistration *> Registrations =
        getDynamicRegistrations().snapshot();
    for (auto &[RegIdx, Storage] : Frame.DynamicStorages)
      if (const auto &Apply = Registrations[RegIdx]->Apply)
        Apply(*Storage);
  }

  // Run essential post-parse callbacks (always).  Snapshot first: a callback
  // is user code and may register more.
  for (std::function<void()> *Cb : getEssentialPostParseCallbacks().snapshot())
    (*Cb)();

  // Run library-local post-parse callbacks only when drain was enabled.
  if (DrainDynamic)
    for (std::function<void()> *Cb : getDynamicPostParseCallbacks().snapshot())
      (*Cb)();

  // Values are final only now, which is why the builtins merely set a flag.
  if (Frame.Builtins.PrintAllOptions || Frame.Builtins.PrintSpecifiedOptions)
    printOptionValues(GlobalEntries, Frame.Builtins.PrintAllOptions,
                      Frame.Builtins.HelpOS ? *Frame.Builtins.HelpOS
                                            : llvm::outs());

  return true;
}

static RegistrationList<clv2::detail::DynamicRegistration> &
getDynamicRegistrations() {
  static RegistrationList<clv2::detail::DynamicRegistration> Registrations;
  return Registrations;
}

void clv2::registerDynamicRegistration(clv2::detail::DynamicRegistration R) {
  assert(DrainsInFlight.load(std::memory_order_relaxed) == 0 &&
         "option registry registered while a parse was in flight; that parse "
         "has already snapshotted the list and will not see it -- register "
         "during static init instead");
  getDynamicRegistrations().append(std::move(R));
}

void clv2::detail::publishDynamicStorages(ParseFrame &Frame,
                                          OptionsContext &Ctx) {
  std::vector<DynamicRegistration *> Registrations =
      getDynamicRegistrations().snapshot();
  for (auto &[RegIdx, Storage] : Frame.DynamicStorages)
    if (Registrations[RegIdx]->PublishInto && Storage)
      Registrations[RegIdx]->PublishInto(Ctx, std::move(Storage));
  Frame.DynamicStorages.clear();
}

static RegistrationList<clv2::detail::OptionEntry> &getDynamicEntries() {
  static RegistrationList<clv2::detail::OptionEntry> Entries;
  return Entries;
}

static RegistrationList<std::function<void()>> &getDynamicPostParseCallbacks() {
  static RegistrationList<std::function<void()>> Callbacks;
  return Callbacks;
}

static RegistrationList<clv2::detail::OptionEntry> &
getEssentialDynamicEntries() {
  static RegistrationList<clv2::detail::OptionEntry> Entries;
  return Entries;
}

static RegistrationList<std::function<void()>> &
getEssentialPostParseCallbacks() {
  static RegistrationList<std::function<void()>> Callbacks;
  return Callbacks;
}

void clv2::registerDynamicEntry(clv2::detail::OptionEntry E) {
  assert(DrainsInFlight.load(std::memory_order_relaxed) == 0 &&
         "option registered while a parse was in flight; that parse has "
         "already snapshotted the list and will not see it -- register during "
         "static init instead");
  assert(E.Static && "entry has no static half; build it with "
                     "clv2::makeEntry<&Opt>() or RuntimeOption::makeEntry()");
  getDynamicEntries().append(std::move(E));
}

void clv2::registerEssentialDynamicEntry(clv2::detail::OptionEntry E) {
  assert(DrainsInFlight.load(std::memory_order_relaxed) == 0 &&
         "option registered while a parse was in flight; that parse has "
         "already snapshotted the list and will not see it -- register during "
         "static init instead");
  assert(E.Static && "entry has no static half; build it with "
                     "clv2::makeEntry<&Opt>() or RuntimeOption::makeEntry()");
  getEssentialDynamicEntries().append(std::move(E));
}

void clv2::registerDynamicPostParseCallback(std::function<void()> Cb) {
  assert(DrainsInFlight.load(std::memory_order_relaxed) == 0 &&
         "option registered while a parse was in flight; that parse has "
         "already snapshotted the list and will not see it -- register during "
         "static init instead");
  getDynamicPostParseCallbacks().append(std::move(Cb));
}

void clv2::registerEssentialDynamicPostParseCallback(std::function<void()> Cb) {
  assert(DrainsInFlight.load(std::memory_order_relaxed) == 0 &&
         "option registered while a parse was in flight; that parse has "
         "already snapshotted the list and will not see it -- register during "
         "static init instead");
  getEssentialPostParseCallbacks().append(std::move(Cb));
}

bool clv2::detail::expandArgs(OnError OnErr, int Argc, const char *const *Argv,
                              BumpPtrAllocator &Alloc,
                              SmallVectorImpl<const char *> &Out,
                              raw_ostream *Errs) {
#ifdef _WIN32
  cl::TokenizerCallback Tok = cl::TokenizeWindowsCommandLine;
#else
  cl::TokenizerCallback Tok = cl::TokenizeGNUCommandLine;
#endif
  Out.append(Argv, Argv + Argc);
  cl::ExpansionContext ECtx(Alloc, Tok);
  if (Error Err = ECtx.expandResponseFiles(Out)) {
    std::string PN = (Argc > 0 && Argv[0]) ? sys::path::filename(Argv[0]).str()
                                           : std::string("program");
    raw_ostream &ErrOS = Errs ? *Errs : llvm::errs();
    ErrOS << PN << ": " << toString(std::move(Err)) << "\n";
    if (OnErr == OnError::ExitProcess)
      std::exit(1);
    return false;
  }
  return true;
}

//===----------------------------------------------------------------------===//
// CompiledParser
//===----------------------------------------------------------------------===//

CompiledParser::CompiledParser() = default;
CompiledParser::CompiledParser(CompiledParser &&) = default;
CompiledParser &CompiledParser::operator=(CompiledParser &&) = default;
CompiledParser::~CompiledParser() = default;

void OptionParser::printHelp(raw_ostream &OS, StringRef Overview,
                             StringRef ProgName, bool ShowHidden) const {
  // Mirrors the entry assembly in parse(), minus the parse itself.  The
  // storages are local: nothing here writes into the parser's own.
  std::vector<std::unique_ptr<ParsedOptionsBase>> LocalStorages;
  std::vector<OptionEntry> Entries;
  std::vector<AliasEntry> Aliases;
  std::vector<SubCommandSpec> SubSpecs;
  LocalStorages.reserve(Registries.size());
  for (const auto &R : Registries) {
    LocalStorages.push_back(R.MakeStorage());
    R.BuildInto(*LocalStorages.back(), Entries, Aliases, SubSpecs);
  }

  // The frame owns the alias proxies' static info, so it outlives printing.
  ParseFrame Frame;
  Frame.AllowedCategories = AllowedCategories;
  Frame.HideUnrelated = HideUnrelated;
  Frame.HideAllRegistered = HideAllRegistered;
  resolveAliases(Entries, Aliases, Frame);
  Entries.insert(Entries.end(), DynamicEntries.begin(), DynamicEntries.end());
  // detail::printHelp applies the hide-unrelated filter on its own copy.
  detail::printHelp(Entries, Overview, ProgName, ShowHidden, OS, Frame,
                    ExtraHelp_);
}

CompiledParser OptionParser::compile() const {
  CompiledParser CP;
  CP.Registries = Registries;
  CP.DynamicEntries = DynamicEntries;
  CP.AllowedCategories = AllowedCategories;
  CP.ExtraHelp_ = ExtraHelp_;
  CP.HiddenNames = HiddenNames;
  CP.ShownNames = ShownNames;
  CP.HideUnrelated = HideUnrelated;
  CP.DrainGlobalDynamic = DrainGlobalDynamic;
  CP.HideAllRegistered = HideAllRegistered;
  CP.ErrorPolicy = ErrorPolicy;
  // Dynamic entries hold pointers to caller-owned value slots, and a bridge
  // writes parsed values into process-wide variables; either way two parses
  // running at once would write to the same storage.  Such a parser still
  // works, just not concurrently.
  CP.Shareable = DynamicEntries.empty() && !DrainGlobalDynamic &&
                 llvm::none_of(Registries, [](const ErasedRegistry &R) {
                   return R.Bridge != nullptr;
                 });

  // Build the index against throwaway storage.  Only names are kept, and those
  // live in the descriptors' OptionStaticInfo (or, for standalone enum flags,
  // in a function-local static), not in the storage -- so they outlive it.
  std::vector<std::unique_ptr<ParsedOptionsBase>> Probe;
  std::vector<OptionEntry> Entries;
  std::vector<AliasEntry> Aliases;
  std::vector<SubCommandSpec> SubSpecs;
  std::size_t Expected = 0;
  for (const auto &R : CP.Registries)
    Expected += R.NumOptions;
  Probe.reserve(CP.Registries.size());
  Entries.reserve(Expected);
  for (auto &R : CP.Registries) {
    Probe.push_back(R.MakeStorage());
    R.BuildInto(*Probe.back(), Entries, Aliases, SubSpecs);
  }
  CP.BakedCount = Entries.size();
  CP.Baked = std::make_shared<const BakedNameIndex>(Entries, CP.BakedCount);
  return CP;
}

std::unique_ptr<OptionsContext>
CompiledParser::parse(int argc, const char *const *argv, StringRef Overview,
                      raw_ostream *Errs, StringRef VersionString,
                      raw_ostream *HelpOS,
                      std::function<void(raw_ostream &)> VersionPrinter) const {
  // Every mutable object here is local, so this is safe to run concurrently on
  // one CompiledParser -- provided the parser is shareable at all.  A
  // non-shareable one aliases caller-owned slots or writes process globals
  // through a bridge, so overlapping parses would race; catch that here rather
  // than leaving isShareable() as advice nothing checks.
#ifndef NDEBUG
  struct InFlightGuard {
    const CompiledParser &P;
    explicit InFlightGuard(const CompiledParser &P) : P(P) {
      unsigned Prev =
          P.ParsesInFlight.N.fetch_add(1, std::memory_order_relaxed);
      assert((P.Shareable || Prev == 0) &&
             "concurrent parse on a CompiledParser that is not shareable; see "
             "CompiledParser::isShareable()");
      (void)Prev;
    }
    ~InFlightGuard() {
      P.ParsesInFlight.N.fetch_sub(1, std::memory_order_relaxed);
    }
  } Guard(*this);
#endif

  std::vector<std::unique_ptr<ParsedOptionsBase>> Storages;
  Storages.reserve(Registries.size());

  std::vector<OptionEntry> Entries;

  std::vector<AliasEntry> Aliases;
  std::vector<SubCommandSpec> SubSpecs;
  Entries.reserve(BakedCount + DynamicEntries.size() + 8 /* builtins */);

  for (const auto &R : Registries) {
    Storages.push_back(R.MakeStorage());
    R.BuildInto(*Storages.back(), Entries, Aliases, SubSpecs);
  }
  // BuildInto must reproduce exactly the layout the index was built against,
  // or the baked indices point at the wrong options.
  assert(Entries.size() == BakedCount &&
         "registry entry layout changed since compile()");

  ParseFrame Frame;
  Frame.AllowedCategories = AllowedCategories;
  Frame.HideUnrelated = HideUnrelated;
  Frame.HideAllRegistered = HideAllRegistered;
  Frame.OnErr =
      ErrorPolicy.value_or(Errs ? OnError::Return : OnError::ExitProcess);
  Frame.Baked = Baked.get();
  Frame.BakedCount = BakedCount;

  BumpPtrAllocator ResponseFileAlloc;
  SmallVector<const char *, 20> ExpandedArgv;
  if (!expandArgs(Frame.OnErr, argc, argv, ResponseFileAlloc, ExpandedArgv,
                  Errs))
    return nullptr;
  int ExpandedArgc = static_cast<int>(ExpandedArgv.size());
  const char *const *ExpandedArgvPtr = ExpandedArgv.data();

  StringRef ProgName =
      ExpandedArgc > 0 ? llvm::sys::path::filename(ExpandedArgvPtr[0]) : "";
  // Builtins are prepended, which shifts the registry block the baked index
  // was built against; record where it landed.
  Frame.BakedFirst =
      buildBuiltinEntries(Entries, Overview, ProgName, VersionString, HelpOS,
                          ExtraHelp_, std::move(VersionPrinter), Errs, Frame);
  Frame.DefaultedFirst = Frame.BakedFirst;
  Frame.DefaultedCount = BakedCount;
  resolveAliases(Entries, Aliases, Frame, Errs);

  Entries.insert(Entries.end(), DynamicEntries.begin(), DynamicEntries.end());

  for (auto &E : Entries)
    for (StringRef HN : HiddenNames)
      if (E.name() == HN && E.HiddenFlag == NotHidden)
        E.HiddenFlag = Hidden;
  Frame.ShownNames = ShownNames;

  bool Ok = runParser(Entries, SubSpecs, ExpandedArgc, ExpandedArgvPtr, Errs,
                      Frame, DrainGlobalDynamic);
  if (!Ok)
    return nullptr;
  if (Frame.HelpPrinted && Frame.OnErr == OnError::Return)
    return nullptr;

  for (std::size_t I = 0; I < Registries.size(); ++I)
    if (Registries[I].Bridge)
      Registries[I].Bridge(*Storages[I]);

  auto Ctx = std::make_unique<OptionsContext>();
  Ctx->setActiveSubCommand(Frame.ActiveSubCommandName);
  for (std::size_t I = 0; I < Registries.size(); ++I)
    Ctx->addRawView(Registries[I].RegAddr, Storages[I].release(),
                    Registries[I].Destroy, Registries[I].Clone);
  publishDynamicStorages(Frame, *Ctx);
  return Ctx;
}

std::unique_ptr<OptionsContext>
OptionParser::parse(int argc, const char *const *argv, StringRef Overview,
                    raw_ostream *Errs, StringRef VersionString,
                    raw_ostream *HelpOS,
                    std::function<void(raw_ostream &)> VersionPrinter) {
  Storages.clear();
  Storages.reserve(Registries.size());
  std::vector<OptionEntry> Entries;
  std::vector<AliasEntry> Aliases;
  std::vector<SubCommandSpec> SubSpecs;

  // Size the entry vector up front.  Growing it one push_back at a time costs
  // ~33% of the build time at `opt` scale (~3.4k options).
  {
    std::size_t Expected = DynamicEntries.size() + 8 /* builtins */;
    for (const auto &R : Registries)
      Expected += R.NumOptions;
    Entries.reserve(Expected);
  }

  for (auto &R : Registries) {
    Storages.push_back(R.MakeStorage());
    R.BuildInto(*Storages.back(), Entries, Aliases, SubSpecs);
  }

  ParseFrame Frame;
  Frame.AllowedCategories = AllowedCategories;
  Frame.HideUnrelated = HideUnrelated;
  Frame.HideAllRegistered = HideAllRegistered;
  Frame.OnErr =
      ErrorPolicy.value_or(Errs ? OnError::Return : OnError::ExitProcess);

  // Expand @file response files before parsing.
  BumpPtrAllocator ResponseFileAlloc;
  SmallVector<const char *, 20> ExpandedArgv;
  if (!expandArgs(Frame.OnErr, argc, argv, ResponseFileAlloc, ExpandedArgv,
                  Errs))
    return nullptr;
  int ExpandedArgc = static_cast<int>(ExpandedArgv.size());
  const char *const *ExpandedArgvPtr = ExpandedArgv.data();

  StringRef ProgName =
      ExpandedArgc > 0 ? llvm::sys::path::filename(ExpandedArgvPtr[0]) : "";
  const std::size_t RegistryCount = Entries.size();
  Frame.DefaultedFirst =
      buildBuiltinEntries(Entries, Overview, ProgName, VersionString, HelpOS,
                          ExtraHelp_, std::move(VersionPrinter), Errs, Frame);
  Frame.DefaultedCount = RegistryCount;
  resolveAliases(Entries, Aliases, Frame, Errs);

  Entries.insert(Entries.end(), DynamicEntries.begin(), DynamicEntries.end());

  // Apply per-option hide/show overrides from hideOptions()/showOptions().
  for (auto &E : Entries) {
    for (StringRef HN : HiddenNames)
      if (E.name() == HN && E.HiddenFlag == NotHidden)
        E.HiddenFlag = Hidden;
  }

  // Store ShownNames in Frame so runParser and applyHideUnrelatedFilter
  // can apply them (including to dynamically-drained entries).
  Frame.ShownNames = ShownNames;

  bool Ok = runParser(Entries, SubSpecs, ExpandedArgc, ExpandedArgvPtr, Errs,
                      Frame, DrainGlobalDynamic);

  if (!Ok)
    return nullptr;
  if (Frame.HelpPrinted && Frame.OnErr == OnError::Return)
    return nullptr;

  for (std::size_t I = 0; I < Registries.size(); ++I) {
    if (Registries[I].Bridge)
      Registries[I].Bridge(*Storages[I]);
  }

  auto Ctx = std::make_unique<OptionsContext>();
  Ctx->setActiveSubCommand(Frame.ActiveSubCommandName);
  for (std::size_t I = 0; I < Registries.size(); ++I) {
    Ctx->addRawView(Registries[I].RegAddr, Storages[I].release(),
                    Registries[I].Destroy, Registries[I].Clone);
  }
  // Hand this parse's dynamic storages to the context.  Done after the static
  // registries so that a registry contributed both ways keeps the static view.
  publishDynamicStorages(Frame, *Ctx);
  return Ctx;
}

//===----------------------------------------------------------------------===//
// Runtime subcommand registry
//===----------------------------------------------------------------------===//

static RegistrationList<RuntimeSubCommandEntry> &getRuntimeSubcmdRegistry() {
  static RegistrationList<RuntimeSubCommandEntry> Registry;
  return Registry;
}

std::vector<const RuntimeSubCommandEntry *> clv2::getRuntimeSubcommands() {
  std::vector<RuntimeSubCommandEntry *> Snap =
      getRuntimeSubcmdRegistry().snapshot();
  return {Snap.begin(), Snap.end()};
}

void clv2::registerRuntimeSubcommand(RuntimeSubCommandEntry E) {
  assert(DrainsInFlight.load(std::memory_order_relaxed) == 0 &&
         "runtime subcommand registered while a parse was in flight; that "
         "parse has already snapshotted the list and will not see it -- "
         "register during static init instead");
  getRuntimeSubcmdRegistry().append(std::move(E));
}

//===----------------------------------------------------------------------===//
// cl:: version/help printing entry points
//===----------------------------------------------------------------------===//

// Function-local rather than namespace-scope: a std::function and a
// std::vector at namespace scope need a dynamic initialiser and a global
// destructor, which -Wglobal-constructors rejects (and which would run at an
// unspecified point relative to other translation units).
static std::function<void(raw_ostream &)> &overrideVersionPrinter() {
  static std::function<void(raw_ostream &)> P;
  return P;
}

static std::vector<std::function<void(raw_ostream &)>> &extraVersionPrinters() {
  static std::vector<std::function<void(raw_ostream &)>> V;
  return V;
}

// Mirrors the build-mode detection the historical version printer used, so
// --version output is unchanged.
#if defined(__GNUC__)
#if defined(__OPTIMIZE__)
#define CLV2_IS_DEBUG_BUILD 0
#else
#define CLV2_IS_DEBUG_BUILD 1
#endif
#elif defined(_MSC_VER)
#if defined(_DEBUG)
#define CLV2_IS_DEBUG_BUILD 1
#else
#define CLV2_IS_DEBUG_BUILD 0
#endif
#else
#define CLV2_IS_DEBUG_BUILD 0
#endif

void cl::PrintVersionMessage(raw_ostream &OS) {
  if (overrideVersionPrinter()) {
    overrideVersionPrinter()(OS);
    for (auto &Fn : extraVersionPrinters())
      Fn(OS);
    return;
  }
#ifdef PACKAGE_VENDOR
  OS << PACKAGE_VENDOR << " ";
#else
  OS << "LLVM (http://llvm.org/):\n  ";
#endif
  OS << PACKAGE_NAME << " version " << PACKAGE_VERSION << "\n  ";
#if CLV2_IS_DEBUG_BUILD
  OS << "DEBUG build";
#else
  OS << "Optimized build";
#endif
#ifndef NDEBUG
  OS << " with assertions";
#endif
  OS << ".\n";
  for (auto &Fn : extraVersionPrinters())
    Fn(OS);
}

void cl::PrintVersionMessage() { PrintVersionMessage(llvm::outs()); }

void cl::SetVersionPrinter(std::function<void(raw_ostream &)> Fn) {
  overrideVersionPrinter() = std::move(Fn);
}

void cl::AddExtraVersionPrinter(std::function<void(raw_ostream &)> Fn) {
  extraVersionPrinters().push_back(std::move(Fn));
}

// C API — OptionParser::parse() automatically picks up global dynamic entries.

static inline clv2::OptionsContext *unwrap(LLVMOptionsContextRef P) {
  return reinterpret_cast<clv2::OptionsContext *>(P);
}
static inline LLVMOptionsContextRef wrap(clv2::OptionsContext *P) {
  return reinterpret_cast<LLVMOptionsContextRef>(P);
}

LLVMOptionsContextRef LLVMParseCommandLineOptions2(int argc,
                                                   const char *const *argv,
                                                   const char *Overview) {
  OptionParser P;
  P.enableGlobalDynamicEntries();
  // Passing a non-null Errs selects OnError::Return, so a malformed option
  // yields a null context instead of terminating the host process.  A C entry
  // point is reached from language bindings and embedders, which cannot
  // survive the library calling exit().  nulls() rather than errs() matches
  // LLVMParseCommandLineOptions, which passes &nulls() for the same reason.
  // HelpOS is a separate stream, so --help and --version still reach stdout;
  // they also return null, since their result is not meant to be acted on.
  auto Ctx = P.parse(argc, argv, Overview ? StringRef(Overview) : StringRef(),
                     &llvm::nulls());
  return wrap(Ctx.release());
}

void LLVMDisposeOptionsContext(LLVMOptionsContextRef Ctx) {
  delete unwrap(Ctx);
}

// LLVMParseCommandLineOptions keeps its historical cl:: implementation while
// both parsers coexist; LLVMParseCommandLineOptions2 above is the clv2 entry
// point.
