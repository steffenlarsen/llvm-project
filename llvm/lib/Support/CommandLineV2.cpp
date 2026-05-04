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
  // "for the --<opt> option: ...".
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

// The category under which help/version options are grouped in --help output.
const OptionCategory clv2::GenericOptionsCategory{"Generic Options"};

// The category for options with no explicit category, matching cl's default
// "General options" section.
static const OptionCategory GeneralOptionsCategory{"General options"};

// (BuiltinOccurrences moved into ParseFrame - no file-scope static needed.)

// Forward declarations - defined in the runtime subcommand registry section
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

namespace {
const BuiltinOptionState &builtinState(const void *D) {
  return *static_cast<const BuiltinOptionState *>(D);
}

/// Shared body for every help-printing builtin.
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
  S.Frame->HelpPrinted = true;
  if (S.Frame->OnErr == OnError::ExitProcess)
    std::exit(0);
  return true;
}

/// --print-all-options / --print-options. These only record the request since
/// the values are not final until parsing completes, so runParser does the
/// printing on its way out.
bool builtinPrintAllOptions(const void *, void *Slot, unsigned, StringRef,
                            ParseDiag &) {
  static_cast<BuiltinOptionState *>(Slot)->PrintAllOptions = true;
  return true;
}

bool builtinPrintOptions(const void *, void *Slot, unsigned, StringRef,
                         ParseDiag &) {
  static_cast<BuiltinOptionState *>(Slot)->PrintSpecifiedOptions = true;
  return true;
}

/// OptionStaticInfo for a builtin, differing from its defaults only in the
/// fields every builtin sets explicitly; everything else keeps
/// OptionStaticInfo's own default member initializers.
struct BuiltinOptionInfo : OptionStaticInfo {
  BuiltinOptionInfo(StringRef Name, StringRef Description, OptionHidden Hidden,
                    const void *D,
                    bool (*Action)(const void *, void *, unsigned, StringRef,
                                   ParseDiag &)) {
    this->Name = Name;
    this->Description = Description;
    this->ValueExpected = ValueDisallowed;
    this->DefaultHidden = Hidden;
    this->DefaultCat = &clv2::GenericOptionsCategory;
    this->Desc = D;
    this->ParseFn = Action;
    this->IsBuiltin = true;
  }
};
} // namespace

namespace llvm::clv2::detail {
/// Append globally-registered dynamic entries to the entry list.
/// Inject built-in option entries (help, help-hidden, help-list,
/// help-list-hidden, version) at the front of \p Entries with proper parse
/// actions. These entries carry \c Cat = \c GenericOptionsCategory so they
/// appear under the "Generic Options" section of help output. Since they live
/// in the entry vector, \c AliasInfo{"h","help"} resolves normally.
/// Returns the number of entries inserted before the pre-existing ones, so a
/// caller holding indices into that block can rebase them.
LLVM_ABI std::size_t
buildBuiltinEntries(std::vector<OptionEntry> &Entries, StringRef Overview,
                    StringRef ProgName, StringRef VersionString,
                    raw_ostream *HelpOS, StringRef ExtraHelp,
                    std::function<void(raw_ostream &)> VersionPrinter,
                    raw_ostream *Errs, ParseFrame &Frame) {

  // Helper to build one built-in entry.
  // Fill the per-parse state the builtin actions read through ParseDesc.
  Frame.Builtins.Overview = Overview;
  Frame.Builtins.ExtraHelp = ExtraHelp;
  Frame.Builtins.VersionString = VersionString;
  Frame.Builtins.Errs = Errs;
  Frame.Builtins.Frame = &Frame;
  Frame.Builtins.VersionPrinter = std::move(VersionPrinter);

  // Slot identifies which BuiltinOccurrences/BuiltinStatics element belongs
  // to this builtin; sharing one would make a builtin's occurrence count
  // observable through another's.
  auto makeBuiltin = [&](StringRef Name, StringRef Desc, OptionHidden Hidden,
                         BuiltinSlot Slot,
                         bool (*Action)(const void *, void *, unsigned,
                                        StringRef, ParseDiag &),
                         void *ParseSlot = nullptr) -> OptionEntry {
    OptionStaticInfo &S = Frame.BuiltinStatics[Slot];
    S = BuiltinOptionInfo(Name, Desc, Hidden, &Frame.Builtins, Action);

    return OptionEntry(Hidden, &Frame.BuiltinOccurrences[Slot],
                       &clv2::GenericOptionsCategory, ParseSlot,
                       /*LastPosition=*/nullptr, /*ElementPositions=*/nullptr,
                       &S);
  };

  Frame.Builtins.HelpOS = HelpOS ? HelpOS : &llvm::outs();
  Frame.Builtins.ProgName =
      ProgName.empty() ? "" : sys::path::filename(ProgName);

  Entries.reserve(Entries.size() + BS_Count);

  // The front builtins are collected first and spliced in with a single
  // insert.
  llvm::SmallVector<OptionEntry, 6> Front;
  Front.push_back(
      makeBuiltin("h", "Alias for --help", Hidden, BS_H, &builtinHelp));
  Front.push_back(
      makeBuiltin("help", "Display available options (--help-hidden for more)",
                  NotHidden, BS_Help, &builtinHelp));
  Front.push_back(makeBuiltin("help-hidden", "Display all available options",
                              Hidden, BS_HelpHidden, &builtinHelpHidden));
  Front.push_back(makeBuiltin(
      "help-list",
      "Display list of available options (--help-list-hidden for more)",
      NotHidden, BS_HelpList, &builtinHelpList));
  Front.push_back(makeBuiltin("help-list-hidden",
                              "Display list of all available options", Hidden,
                              BS_HelpListHidden, &builtinHelpListHidden));
  Front.push_back(makeBuiltin("version", "Display the version of this program",
                              NotHidden, BS_Version, &builtinVersion));
  Entries.insert(Entries.begin(), Front.begin(), Front.end());

  // Print all / non-default option values after parsing. Hidden, so they
  // show up only under --help-hidden.
  Entries.push_back(makeBuiltin(
      "print-all-options", "Print all option values after command line parsing",
      Hidden, BS_PrintAllOptions, &builtinPrintAllOptions, &Frame.Builtins));
  Entries.push_back(makeBuiltin(
      "print-options", "Print non-default options after command line parsing",
      Hidden, BS_PrintOptions, &builtinPrintOptions, &Frame.Builtins));

  // The front-inserted builtins shifted every pre-existing entry by this much.
  return Front.size();
}
} // namespace llvm::clv2::detail

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
/// parse. No-op once a subcommand is already active, or when there are none.
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
    OS << " " << SC.first;
    OS.indent(MaxSCLen - SC.first.size());
    OS << " - " << SC.second << "\n";
  }
  OS << "\n Type \"" << ProgName
     << " <subcommand> --help\" to get more help on a specific "
        "subcommand\n\n";
}

namespace llvm::clv2 {
namespace detail {

class BakedNameIndex {
  llvm::StringMap<llvm::SmallVector<unsigned, 1>> ByName;
  llvm::SmallVector<unsigned, 4> PrefixEntries;
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
    // Longest name first, so the first match is the longest. Stable so equal
    // lengths keep declaration order, matching the linear scan this replaces.
    llvm::stable_sort(PrefixEntries, [&Entries](unsigned A, unsigned B) {
      return Entries[A].name().size() > Entries[B].name().size();
    });
  }

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
/// Reads that went through defaultOptionsContext() - i.e. through a context
/// nobody threaded. This is a diagnostic tally, not a correctness signal, and
/// contention here would distort the parallelism it exists to protect.
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

} // namespace llvm::clv2

void clv2::detail::resolveAliases(std::vector<OptionEntry> &Entries,
                                  llvm::ArrayRef<AliasEntry> Aliases,
                                  ParseFrame &Frame, raw_ostream *Errs) {
  // Nothing to resolve, and building the target index below is O(entries) --
  // which most parses would pay for no reason.
  if (Aliases.empty())
    return;

  // Resolve each alias to its target's entry index.
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
  // baked index): first occurrence wins.
  // Skipped entirely when the baked index already answered every alias.
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
  llvm::SmallVector<OptionEntry, 8> Proxies;
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
        // PS is a separate copy, not the target's own descriptor, so its
        // inherited ParseFn (which expects D == the target's descriptor)
        // needs Desc set explicitly to the target's effective descriptor
        // rather than left to default to PS's own address.
        PS.Desc = clv2::detail::effectiveDesc(*E.Static);
        PS.Name = A.Name;
        Proxy.Static = &PS;
        if (!A.Desc.empty()) {
          PS.Description = A.Desc;
          PS.SuppressValuePlaceholder = true;
        } else {
          Proxy.HiddenFlag = Hidden;
        }
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
/// PositionalEatsArgs ones which also get a body entry. Names are
/// deduplicated so an option contributed by both a tool-local and a global
/// registry is listed once.
///
/// Under a subcommand, only that subcommand's options are listed, plus the
/// generic built-ins (--help, --version, ...) which every subcommand accepts.
/// A top-level option is not part of the subcommand's interface, and where
/// both define the same name the subcommand's own descriptor is the one that
/// describes what the user gets.
static llvm::SmallVector<const OptionEntry *, 16>
collectVisibleEntries(const std::vector<OptionEntry> &Entries, bool ShowHidden,
                      bool InSubCmd, std::size_t GlobalEntryCount) {
  auto IsShadowedGlobal = [&](std::size_t I, const OptionEntry &E) {
    return InSubCmd && I < GlobalEntryCount &&
           E.Cat != &clv2::GenericOptionsCategory;
  };

  llvm::SmallVector<const OptionEntry *, 16> Visible;
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

void clv2::detail::printHelpList(llvm::ArrayRef<OptionEntry> EntriesIn,
                                 StringRef Overview, StringRef ProgName,
                                 bool ShowHidden, raw_ostream &OS,
                                 const ParseFrame &Frame) {
  const bool InSubCmd = !Frame.ActiveSubCommandName.empty();
  // Work on a private copy. applyHideUnrelatedFilter rewrites HiddenFlag, and
  // callers may print help more than once or keep parsing afterwards, so those
  // rewrites must not escape into the caller's entry list.
  std::vector<OptionEntry> Entries(EntriesIn.begin(), EntriesIn.end());
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

  llvm::SmallVector<const OptionEntry *, 16> Visible = collectVisibleEntries(
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
      OS << " -" << E->name() << Ph;
    else
      OS << " --" << E->name() << Ph;
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

/// A ValueOptional option's help width is under-counted by 2, so the
/// placeholder prints as "[=<val>]" but only three characters are charged. Enum
/// options are exempt. The column computation and the per-line padding must
/// agree on this, or the columns do not line up.
static bool hasValueOptionalWidthQuirk(const OptionEntry &E) {
  return E.valueExpected() == ValueOptional && !E.hasEnumPrinter();
}

void clv2::detail::printHelp(llvm::ArrayRef<OptionEntry> EntriesIn,
                             StringRef Overview, StringRef ProgName,
                             bool ShowHidden, raw_ostream &OS,
                             const ParseFrame &Frame, StringRef ExtraHelp) {
  // Work on a private copy as both applyHideUnrelatedFilter (HiddenFlag) and
  // the category normalisation below rewrite entries.
  std::vector<OptionEntry> Entries(EntriesIn.begin(), EntriesIn.end());
  applyHideUnrelatedFilter(Entries, Frame);

  const bool InSubCmd = !Frame.ActiveSubCommandName.empty();
  llvm::SmallVector<const OptionEntry *, 16> Visible = collectVisibleEntries(
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

  // Header. If the overview text itself ends with '\n', the result is a blank
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
        OS << " " << E->enumGroupHeader() << "\n";
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
  llvm::SmallVector<const OptionCategory *, 8> Cats;
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
      if (!Cat->Desc.empty())
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

namespace {
/// Adaptive name-to-entry lookup for one parse.
/// Holds indices into the entry list, so it must only be used after every
/// mutation of that list (dynamic drain, subcommand merge, alias resolution).
class EntryIndex {
  const std::vector<OptionEntry> &Entries;

  /// When supplied, a pre-built registry-prefix index covering
  /// [0, BakedCount). The lazy map below then only has to cover the tail
  /// (builtins, alias proxies, dynamic entries), which is small for most
  /// tools, so in practice it is never built at all.
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

  /// Lookups to allow before paying to build the hash map.
  static constexpr unsigned BuildThreshold = 16;

  static bool isNameAddressable(const OptionEntry &E) {
    return !E.isPositional() || E.isPositionalEatsArgs();
  }

  void buildNameMap() {
    ByName = llvm::StringMap<llvm::SmallVector<unsigned, 1>>(Entries.size() -
                                                             BakedCount);
    for (unsigned I = bakedEnd(), N = Entries.size(); I < N; ++I)
      if (isNameAddressable(Entries[I]))
        ByName[Entries[I].name()].push_back(I);
#ifndef NDEBUG
    // Name resolution is first-wins, so a duplicate silently makes the later
    // option unreachable.
    bool AnyDuplicate = false;
    for (const auto &KV : ByName) {
      if (KV.second.size() < 2)
        continue;
      AnyDuplicate = true;
      llvm::errs() << "clv2: duplicate command line option '" << KV.first()
                   << "' - only the first is reachable:\n";
      for (unsigned I : KV.second)
        llvm::errs() << " " << Entries[I].name() << ": "
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
    // Stable, so equal-length ties keep declaration order: the first entry
    // whose name is a prefix of the arg wins unless a strictly longer prefix
    // also matches.
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

  /// Find an OptionEntry by name. When SubStart > 0, subcommand entries
  /// (indices >= SubStart) shadow global ones.
  ///
  /// \p Es must be the same vector this index was built over - the stored
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
    // after it.
    const std::size_t Limit =
        SubStart ? std::min(SubStart, Es.size()) : Es.size();
    if (OptionEntry *E = scan(Es, Name, 0, std::min(BakedFirst, Limit)))
      return E;
    if (Baked) {
      // Ascending by construction (BakedNameIndex pushes indices in
      // increasing order), so if the smallest index is >= Limit, every other
      // index for this name is too -- only the first entry needs checking.
      llvm::ArrayRef<unsigned> Idxs = Baked->lookup(Name);
      if (!Idxs.empty() && BakedFirst + Idxs.front() < Limit)
        return &Es[BakedFirst + Idxs.front()];
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

  /// Search the entries after the baked block. With no baked index that is
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

/// Report an error to *Errs, or to llvm::errs() when the caller supplied none.
static void reportError(StringRef Msg, raw_ostream *Errs) {
  raw_ostream &OS = Errs ? *Errs : llvm::errs();
  OS << Msg;
}

/// Writers to every process-wide registration list share one mutex. It is
/// taken only to append, and by readers only to copy out element addresses --
/// never while running registry or user code.
static std::mutex &getRegistrationMutex() {
  static std::mutex M;
  return M;
}

/// Append-only registration list.
template <typename T> class RegistrationList {
  std::deque<T> Items;

public:
  void append(T V) {
    std::lock_guard<std::mutex> Lock(getRegistrationMutex());
    Items.push_back(std::move(V));
  }

  /// Addresses of the elements present now. Safe to use after the lock is
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
static RegistrationList<clv2::detail::DynamicRegistration> &
getDynamicRegistrations();

/// Number of parses currently walking a registration list.
static std::atomic<unsigned> DrainsInFlight{0};

namespace {
struct DrainScope {
  DrainScope() { ++DrainsInFlight; }
  ~DrainScope() { --DrainsInFlight; }
};
} // namespace

/// Dump option values after parsing, for --print-all-options / --print-options.
///
/// One line per option, name column padded to the widest name, hidden options
/// included. With \p AllOptions false only the options that actually appeared
/// on the command line are listed.
static void printOptionValues(const std::vector<OptionEntry> &Entries,
                              bool AllOptions, raw_ostream &OS) {
  llvm::SmallVector<const OptionEntry *, 16> Sorted;
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
    OS << " --" << E->Static->Name;
    OS.indent(MaxNameLen - E->Static->Name.size());
    OS << " = ";
    E->Static->PrintValueFn(clv2::detail::effectiveDesc(*E->Static),
                            E->ParseSlot, OS);
    OS << '\n';
  }
}

/// An argv token like "-name", "--name=value" split into its option name and
/// optional inline value.
struct SplitArg {
  StringRef FullName;  ///< Name with '-'/'--' stripped, before any '=' split.
  StringRef Name;      ///< FullName up to the first '='.
  StringRef InlineVal; ///< Text after the first '=', if any.
  bool HasInlineVal;
};

static SplitArg splitOptionArg(StringRef Arg) {
  StringRef FullName = Arg.ltrim('-');
  StringRef Name = FullName;
  StringRef InlineVal;
  auto EqPos = Name.find('=');
  bool HasInlineVal = (EqPos != StringRef::npos);
  if (HasInlineVal) {
    InlineVal = Name.substr(EqPos + 1);
    Name = Name.substr(0, EqPos);
  }
  return {FullName, Name, InlineVal, HasInlineVal};
}

/// dlopen any -load arguments before dynamic registrations are snapshotted.
/// No-op unless this parse actually has a -load option, so a stray -load on a
/// tool without one still reports as an unknown argument.
static void preloadPlugins(int argc, const char *const *argv) {
  if (!llvm::pluginLoaderOptionRegistered())
    return;
  for (int I = 1; I < argc; ++I) {
    StringRef Arg = argv[I];
    if (!Arg.starts_with("-"))
      continue;
    SplitArg Split = splitOptionArg(Arg);
    if (Split.Name != "load")
      continue;
    StringRef File;
    if (Split.HasInlineVal)
      File = Split.InlineVal;
    else if (I + 1 < argc)
      File = argv[++I];
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
  // -load brings in a shared object whose static initialisers register more
  // options. Those initialisers run at dlopen, which the option's own callback
  // performs part-way through the parse, after the snapshots below have been
  // taken, so the plugin's options would be missing from this parse entirely.
  // Load them up front instead, and only when the tool actually offers -load.
  preloadPlugins(argc, argv);

  // Instantiate per-parse storage for every dynamically-registered registry.
  std::vector<OptionEntry *> GlobalDynSnap =
      DrainDynamic ? getDynamicEntries().snapshot()
                   : std::vector<OptionEntry *>();

  // --color is always drained, independent of DrainDynamic. WithColor.cpp is
  // unconditionally part of this library, so this is always a valid reference.
  {
    const DynamicRegistration &Color = getColorDynamicRegistration();
    GlobalEntries.reserve(GlobalEntries.size() + Color.NumOptions);
    std::unique_ptr<ParsedOptionsBase> Storage = Color.MakeStorage();
    std::vector<AliasEntry> IgnoredAliases;
    std::vector<SubCommandSpec> IgnoredSubSpecs;
    Color.BuildInto(*Storage, GlobalEntries, IgnoredAliases, IgnoredSubSpecs);
    Frame.ColorStorage = std::move(Storage);
  }

  if (DrainDynamic) {
    DrainScope Draining;
    std::vector<DynamicRegistration *> Registrations =
        getDynamicRegistrations().snapshot();
    // Reserve for everything appended below: the dynamic registries plus the
    // hand-built global entries.
    {
      std::size_t Expected = GlobalEntries.size();
      for (const DynamicRegistration *R : Registrations)
        Expected += R->NumOptions;
      Expected += GlobalDynSnap.size();
      GlobalEntries.reserve(Expected);
    }
    for (std::size_t I = 0, N = Registrations.size(); I < N; ++I) {
      const DynamicRegistration &R = *Registrations[I];
      std::unique_ptr<ParsedOptionsBase> Storage = R.MakeStorage();
      // TODO: aliases and subcommands declared by a dynamically-registered
      // registry are discarded. Wiring them up would change --help output,
      // so it is left for a separate change.
      std::vector<AliasEntry> IgnoredAliases;
      std::vector<SubCommandSpec> IgnoredSubSpecs;
      R.BuildInto(*Storage, GlobalEntries, IgnoredAliases, IgnoredSubSpecs);
      Frame.DynamicStorages.emplace_back(I, std::move(Storage));
    }
  }

  // Hand-built dynamic entries own their own slots (the caller allocated them),
  // so they are inherently process-global. They are appended as-is.
  for (const OptionEntry *E : GlobalDynSnap)
    GlobalEntries.push_back(*E);

  // If HideAllRegistered, mark all drained entries as Hidden too, UNLESS they
  // have their own OptionCategory which should be preserved for proper category
  // display.
  if (Frame.HideAllRegistered) {
    for (auto &E : GlobalEntries) {
      // --color is a registered option rather than a builtin, but tools that
      // hide everything still expect it to stay visible.
      if (E.HiddenFlag == NotHidden && !E.name().empty() && !E.Cat &&
          !E.isPositional() && !E.info().IsBuiltin && E.name() != "color")
        E.HiddenFlag = Hidden;
    }
  }

  // showOptions re-reveals named entries. This applies whether or not
  // everything else was just hidden - tools using hideUnrelatedOptions need
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

  // if argv[1] names a known subcommand, activate it and merge its option
  // entries into the working set.
  std::vector<OptionEntry> *ActiveEntries = &GlobalEntries;
  Frame.ActiveEntries = &GlobalEntries;
  Frame.ActiveSubCommandName.clear();
  std::vector<OptionEntry> MergedEntries;

  // Snapshot rather than binding a reference.
  std::vector<RuntimeSubCommandEntry *> RuntimeSubs =
      getRuntimeSubcmdRegistry().snapshot();
  std::vector<std::vector<OptionEntry>> RuntimeSubEntries(RuntimeSubs.size());

  // Publish the subcommands visible to this parse into the frame so the help
  // printers can list them without consulting (or extending) the global
  // registry. Compile-time specs take precedence over an identically-named
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
        // Don't set GlobalEntryCount for runtime subcommands as they share the
        // global option scope and only add a few subcommand-specific options.
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
    // Some tools have both subcommands and positional input files, so an
    // unmatched first arg should be treated as a positional.
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

  // Apply defaults for every option in scope before parsing. This must iterate
  // ActiveEntries rather than GlobalEntries as runtime subcommand entries are
  // merged in above and would otherwise keep whatever their shared storage last
  // held.
  {
    // Registry storages come out of MakeStorage already default-initialised.
    // Everything else still needs it, since their storage may hold whatever a
    // previous parse left behind.
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

  // Index the finalised entry list. Everything above may still append to it.
  EntryIndex Index(*ActiveEntries, Frame.Baked, Frame.BakedFirst,
                   Frame.BakedCount);

  // Collect positional entries in declaration order for later distribution.
  // When a subcommand is active, skip top-level positionals so subcommand
  // positionals receive the args.
  llvm::SmallVector<OptionEntry *, 8> Positionals;
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

  // Positional values collected during the main loop.
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

  // Parse a value into an entry, handling CommaSeparated splitting, and record
  // occurrence count, position, and element positions.
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
    // args (including -flags) are consumed by it, unless the arg matches
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

    // Strip leading '-'/'--' and split on the first '=' to separate the
    // option name from an inline value.
    SplitArg Split = splitOptionArg(Arg);
    StringRef FullArgName = Split.FullName;
    StringRef ArgName = Split.Name;
    StringRef InlineVal = Split.InlineVal;
    bool HasInlineVal = Split.HasInlineVal;

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
    // after the option name in the token - including any '='.
    // e.g. -D=10 -> ArgName="D", but InlineVal must be "=10", not "10".
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
      // Grouping: -lp -> -l -p. Only applies when:
      //  (1) no '=' inline value,
      //  (2) single dash (not '--'),
      //  (3) >=2 chars,
      //  (4) every char maps to a single-char grouping option.
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

      // Check for a Sink entry - it collects all unrecognized options.
      // The sink receives the full original token (dashes and any =val
      // included) and never consumes the following argument. An unrecognized
      // option carries no information about whether it takes a separate value,
      // so anything that follows must stay available to the normal
      // option/positional handling below.
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
                          "'. Try: '" + PN + " --help'\n";
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
      Val = {}; // bool flags with ValueDisallowed pass empty -> true
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

  std::size_t PosIdx = 0; // index into PositionalArgs

  auto ParseOnePositional = [&](OptionEntry *PE, StringRef Val, int ArgI) {
    Frame.CurArgPosition = static_cast<unsigned>(ArgI);
    ParseAndRecord(PE, Val, ArgI);
  };

  // ConsumeAfter distribution.
  if (ConsumeAfterEntry) {
    // Give values to required positionals first.
    for (OptionEntry *PE : Positionals) {
      if (PosIdx >= PositionalArgs.size())
        break;
      if (PE->occurrencesFlag() == Required ||
          PE->occurrencesFlag() == OneOrMore) {
        auto [Val, ArgI] = PositionalArgs[PosIdx++];
        ParseOnePositional(PE, Val, ArgI);
      }
    }
    // If there is exactly one positional option, it's optional, and no values
    // were assigned yet, give it the first value.
    if (Positionals.size() == 1 && PosIdx == 0 &&
        PosIdx < PositionalArgs.size()) {
      auto [Val, ArgI] = PositionalArgs[PosIdx++];
      ParseOnePositional(Positionals[0], Val, ArgI);
    }
    // Give all remaining values to ConsumeAfter.
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
                          "'. Try: '" + PN + " --help'\n";
        reportError(Msg, Errs);
        HadError = true;
      }
    }
  }

  // Validate occurrence constraints (skip when help was printed).
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

  // Write parsed values back into legacy globals, using this parse's
  // storage.
  if (Frame.ColorStorage)
    if (const auto &Apply = getColorDynamicRegistration().Apply)
      Apply(*Frame.ColorStorage);
  if (!Frame.DynamicStorages.empty()) {
    // The list is append-only, so an index taken during the drain still names
    // the same registration here.
    std::vector<DynamicRegistration *> Registrations =
        getDynamicRegistrations().snapshot();
    for (auto &[RegIdx, Storage] : Frame.DynamicStorages)
      if (const auto &Apply = Registrations[RegIdx]->Apply)
        Apply(*Storage);
  }

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
         "has already snapshotted the list and will not see it - register "
         "during static init instead");
  getDynamicRegistrations().append(std::move(R));
}

void clv2::detail::publishDynamicStorages(ParseFrame &Frame,
                                          OptionsContext &Ctx) {
  if (Frame.ColorStorage) {
    if (const auto &PublishInto = getColorDynamicRegistration().PublishInto)
      PublishInto(Ctx, std::move(Frame.ColorStorage));
    else
      Frame.ColorStorage.reset();
  }
  if (!Frame.DynamicStorages.empty()) {
    std::vector<DynamicRegistration *> Registrations =
        getDynamicRegistrations().snapshot();
    for (auto &[RegIdx, Storage] : Frame.DynamicStorages)
      if (Registrations[RegIdx]->PublishInto && Storage)
        Registrations[RegIdx]->PublishInto(Ctx, std::move(Storage));
  }
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

void clv2::registerDynamicEntry(clv2::detail::OptionEntry E) {
  assert(DrainsInFlight.load(std::memory_order_relaxed) == 0 &&
         "option registered while a parse was in flight; that parse has "
         "already snapshotted the list and will not see it - register during "
         "static init instead");
  assert(E.Static && "entry has no static half; build it with "
                     "clv2::makeEntry<&Opt>() or RuntimeOption::makeEntry()");
  getDynamicEntries().append(std::move(E));
}

void clv2::registerDynamicPostParseCallback(std::function<void()> Cb) {
  assert(DrainsInFlight.load(std::memory_order_relaxed) == 0 &&
         "option registered while a parse was in flight; that parse has "
         "already snapshotted the list and will not see it - register during "
         "static init instead");
  getDynamicPostParseCallbacks().append(std::move(Cb));
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

void OptionParser::printHelp(raw_ostream &OS, StringRef Overview,
                             StringRef ProgName, bool ShowHidden) const {
  // Mirrors the entry assembly in parse(), minus the parse itself. The
  // storages are local, so nothing here writes into the parser's own.
  llvm::SmallVector<std::unique_ptr<ParsedOptionsBase>, 8> LocalStorages;
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

  // Size the entry vector up front.
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
  Frame.OnErr = Errs ? OnError::Return : OnError::ExitProcess;

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
  // Hand this parse's dynamic storages to the context. Done after the static
  // registries so that a registry contributed both ways keeps the static view.
  publishDynamicStorages(Frame, *Ctx);
  return Ctx;
}

static RegistrationList<RuntimeSubCommandEntry> &getRuntimeSubcmdRegistry() {
  static RegistrationList<RuntimeSubCommandEntry> Registry;
  return Registry;
}

void clv2::registerRuntimeSubcommand(RuntimeSubCommandEntry E) {
  assert(DrainsInFlight.load(std::memory_order_relaxed) == 0 &&
         "runtime subcommand registered while a parse was in flight; that "
         "parse has already snapshotted the list and will not see it - "
         "register during static init instead");
  getRuntimeSubcmdRegistry().append(std::move(E));
}

static std::function<void(raw_ostream &)> &overrideVersionPrinter() {
  static std::function<void(raw_ostream &)> P;
  return P;
}

static std::vector<std::function<void(raw_ostream &)>> &extraVersionPrinters() {
  static std::vector<std::function<void(raw_ostream &)>> V;
  return V;
}

// Detects whether this is a debug or optimized build, for the "DEBUG build" /
// "Optimized build" line in --version output.
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
  OS << "LLVM (http://llvm.org/):\n ";
#endif
  OS << PACKAGE_NAME << " version " << PACKAGE_VERSION << "\n ";
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

// C API wrappers for OptionsContext and OptionParser.

static inline clv2::OptionsContext *unwrap(LLVMOptionsContextRef P) {
  return reinterpret_cast<clv2::OptionsContext *>(P);
}
static inline LLVMOptionsContextRef wrap(clv2::OptionsContext *P) {
  return reinterpret_cast<LLVMOptionsContextRef>(P);
}

LLVMOptionsContextRef LLVMParseCommandLineOptionsV2(int argc,
                                                    const char *const *argv,
                                                    const char *Overview) {
  OptionParser P;
  P.enableGlobalDynamicEntries();
  // Passing a non-null Errs selects OnError::Return, so a malformed option
  // yields a null context instead of terminating the host process. A C entry
  // point is reached from language bindings and embedders, which cannot
  // survive the library calling exit(). nulls() rather than errs() matches
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
