//===- llvm/Support/OptionsContext.h - Per-session option state --*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// OptionsContext holds parsed clv2 option views for one compilation session.
// It replaces global option state, enabling multiple compilations with
// independent option values in the same process.
//
// Typical usage:
//   clv2::OptionParser P;
//   P.add<&MyReg>();
//   auto Ctx = P.parse(argc, argv);        // owns the parsed views
//   LLVMContext LCtx(*Ctx);
//
// Library code:
//   auto &Ctx = F.getContext().getOptionsContext();
//   bool Flag = clv2::getOptValOrDefault<&MY_Option>(Ctx);
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_OPTIONSCONTEXT_H
#define LLVM_SUPPORT_OPTIONSCONTEXT_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>

namespace llvm {
namespace clv2 {

/// Per-session option state. Stores type-erased ParsedOptions views
/// keyed by registry address. Libraries retrieve their view via
/// getView<&Registry>() using compile-time type deduction.
///
/// Storage is a SmallVector of views, indexed by the hash table below.
///
/// The inline capacities do not cover a fully-populated tool context, so both
/// vectors spill there.  That is deliberate: sizing them to fit would put
/// several kilobytes inline, and a process builds very few contexts, so the
/// trade is small either way.  Measure before retuning.
///
/// Thread safety: reads are safe from multiple threads. Mutations
/// (via mutable getViewPtr) must be externally synchronized.
/// Different compilations use different contexts.
class LLVM_ABI OptionsContext {
  struct ViewEntry {
    const void *Key;
    void *Data;
    void (*Deleter)(const void *);
    /// Deep-copies Data.  Null for raw views added without a cloner, which
    /// copyViewsFrom() therefore cannot duplicate.
    void *(*Cloner)(const void *) = nullptr;
  };
  SmallVector<ViewEntry, 32> Views;
  std::string ActiveSubCommand;
  bool IsDefaultContext = false;

  /// Open-addressed index over Views, keyed by registry address.
  ///
  /// Every option read is a lookup here, and an -O3 run performs tens of
  /// millions of them; a linear scan over the views is too slow.  The table is
  /// rebuilt on insert and never mutated by a lookup, so a context stays safe
  /// to share across threads -- which a lookup-side cache would not be.
  ///
  /// Sized to at least 2x the entry count and always a power of two, so the
  /// mask is cheap and the load factor stays under 0.5.
  SmallVector<uint32_t, 128> Index; ///< 1-based slot into Views; 0 = empty

  static size_t hashKey(const void *Key) {
    // Registry addresses are at least 8-byte aligned; drop the dead low bits
    // then mix, or every entry lands in the same few buckets.
    uint64_t H = reinterpret_cast<uintptr_t>(Key) >> 3;
    H *= 0x9E3779B97F4A7C15ULL;
    return static_cast<size_t>(H >> 32);
  }

  void rebuildIndex() {
    size_t Cap = 16;
    while (Cap < Views.size() * 2)
      Cap *= 2;
    Index.assign(Cap, 0);
    for (size_t I = 0; I < Views.size(); ++I) {
      size_t B = hashKey(Views[I].Key) & (Cap - 1);
      while (Index[B])
        B = (B + 1) & (Cap - 1);
      Index[B] = static_cast<uint32_t>(I + 1);
    }
  }

  const void *findData(const void *Key) const {
    if (Index.empty())
      return nullptr;
    size_t Mask = Index.size() - 1;
    // rebuildIndex keeps the load factor below 0.5, so an empty slot is always
    // reachable and this cannot wrap.  Assert it so the invariant is checked
    // if that sizing ever changes.
    [[maybe_unused]] size_t Probes = 0;
    for (size_t B = hashKey(Key) & Mask;; B = (B + 1) & Mask) {
      assert(++Probes <= Index.size() && "open-addressed index is full");
      uint32_t Slot = Index[B];
      if (!Slot)
        return nullptr;
      if (Views[Slot - 1].Key == Key)
        return Views[Slot - 1].Data;
    }
  }

  void *findMutableData(const void *Key) {
    return const_cast<void *>(findData(Key));
  }

public:
  /// Tag for constructing the one shared empty context returned by
  /// defaultOptionsContext().
  struct DefaultTag {};

  OptionsContext() = default;
  explicit OptionsContext(DefaultTag) : IsDefaultContext(true) {}

  /// True only for the shared context returned by defaultOptionsContext().
  /// checkThreaded() tests this instead of comparing against
  /// defaultOptionsContext(), whose function-local static costs a call plus a
  /// guard check on every option read in an assert build (0.27% of an -O3 run).
  bool isDefaultContext() const { return IsDefaultContext; }
  ~OptionsContext() {
    for (auto &E : Views)
      if (E.Deleter)
        E.Deleter(E.Data);
  }

  OptionsContext(const OptionsContext &) = delete;
  OptionsContext &operator=(const OptionsContext &) = delete;
  OptionsContext(OptionsContext &&) = default;
  OptionsContext &operator=(OptionsContext &&) = default;

  /// Store a copy of a library's ParsedOptions, keyed by registry address.
  template <const auto *Reg>
  void
  addView(const typename std::remove_pointer_t<decltype(Reg)>::ParsedOptionsT
              &View) {
    using ParsedT =
        typename std::remove_pointer_t<decltype(Reg)>::ParsedOptionsT;
    auto *Copy = new ParsedT(View.clone());
    Views.push_back(
        {static_cast<const void *>(Reg), Copy,
         [](const void *P) { delete static_cast<const ParsedT *>(P); },
         [](const void *P) -> void * {
           return new ParsedT(*static_cast<const ParsedT *>(P));
         }});
    rebuildIndex();
  }

  /// Retrieve a library's ParsedOptions view by registry address.
  /// Returns nullptr if the registry wasn't added.
  template <const auto *Reg>
  const typename std::remove_pointer_t<decltype(Reg)>::ParsedOptionsT *
  getViewPtr() const {
    using ParsedT =
        typename std::remove_pointer_t<decltype(Reg)>::ParsedOptionsT;
    return static_cast<const ParsedT *>(
        findData(static_cast<const void *>(Reg)));
  }

  /// Mutable view access for runtime option adjustments.
  template <const auto *Reg>
  typename std::remove_pointer_t<decltype(Reg)>::ParsedOptionsT *getViewPtr() {
    using ParsedT =
        typename std::remove_pointer_t<decltype(Reg)>::ParsedOptionsT;
    return static_cast<ParsedT *>(
        findMutableData(static_cast<const void *>(Reg)));
  }

  /// Check if any views are registered.
  bool empty() const { return Views.empty(); }

  /// Add a type-erased view by runtime key, taking ownership of \p Data.
  /// Supplying \p Cloner makes the view copyable by copyViewsFrom().
  void addRawView(const void *Key, void *Data, void (*Deleter)(const void *),
                  void *(*Cloner)(const void *) = nullptr) {
    Views.push_back({Key, Data, Deleter, Cloner});
    rebuildIndex();
  }

  /// Check if a type-erased view exists for a given key.
  bool hasRaw(const void *Key) const { return findData(Key) != nullptr; }

  /// Get raw data pointer for a runtime key. Returns nullptr if not found.
  const void *getRawViewPtr(const void *Key) const { return findData(Key); }

  /// Name of the subcommand selected on the command line for this parse, or
  /// empty if none.  Per-parse rather than a process-wide flag, so concurrent
  /// parses can select different subcommands.
  llvm::StringRef getActiveSubCommand() const { return ActiveSubCommand; }
  void setActiveSubCommand(llvm::StringRef Name) {
    ActiveSubCommand = Name.str();
  }

  /// Deep-copy every clonable view of \p Other that this context does not
  /// already have.  Used to seed a longer-lived context (e.g. a tool's global
  /// one) from the context produced by a parse.  Views added via addRawView
  /// without a cloner are skipped.  Returns the number of views copied.
  unsigned copyViewsFrom(const OptionsContext &Other) {
    unsigned Copied = 0;
    // The index is stale while appending, so dedup against it plus the keys
    // added so far in this call, and rebuild once at the end.
    llvm::SmallPtrSet<const void *, 16> Pending;
    for (const auto &E : Other.Views) {
      if (!E.Cloner || findData(E.Key) || !Pending.insert(E.Key).second)
        continue;
      Views.push_back({E.Key, E.Cloner(E.Data), E.Deleter, E.Cloner});
      ++Copied;
    }
    if (Copied)
      rebuildIndex();
    return Copied;
  }
};

/// The single shared, permanently empty OptionsContext.
///
/// It holds no views, so every lookup through it yields the option's
/// compile-time default.  That is the same value a null context used to
/// produce, with one difference that is the whole point: naming this function
/// is how a call site *declares* that no session options are threaded to it.
/// A null pointer made that admission invisible; this makes it greppable, so
/// the set of unthreaded paths can be counted and driven down.
///
/// Prefer threading a real context.  Reach for this only where there is
/// genuinely nothing to thread — the C API entry points, and tests that do not
/// exercise options.
LLVM_ABI const OptionsContext &defaultOptionsContext();

//===----------------------------------------------------------------------===//
// Unthreaded-read detection (assert builds only)
//===----------------------------------------------------------------------===//
//
// Naming defaultOptionsContext() makes an opt-out visible in the source, but a
// read through it at run time looks exactly like a correct read of a populated
// context: both yield compile-time defaults.  So, in assert builds, count those
// reads and optionally abort on them.  Compiled out under NDEBUG; when enabled
// it costs one pointer compare per read.
//
// Modes (LLVM_OPTIONS_CONTEXT_STRICT):
//   unset / "0"  count only; totals available via unthreadedReadCount()
//   "1"          report_fatal_error on the first unthreaded read
//
// Strict mode is opt-in: reading defaults is legitimate for the C API entry
// points and for tests that never parse options.  Point it at one tool at a
// time.
#ifndef NDEBUG
LLVM_ABI void noteUnthreadedRead();
LLVM_ABI uint64_t unthreadedReadCount();

inline void checkThreaded(const OptionsContext &Ctx) {
  if (Ctx.isDefaultContext())
    noteUnthreadedRead();
}
#else
inline void checkThreaded(const OptionsContext &) {}
#endif

//===----------------------------------------------------------------------===//
// Generic view accessors — replace per-namespace getOverride/viewFrom
// boilerplate
//===----------------------------------------------------------------------===//

/// Get a registry's parsed view from an OptionsContext.
///
/// Returns nullptr when this registry was not parsed into \p Ctx.  That
/// nullability is real and is here to stay: a context can be fully populated
/// for one library while carrying nothing for another.  It is a *different*
/// axis from the context itself being absent, which taking a reference rules
/// out — do not collapse the two.
template <const auto *Reg>
const typename std::remove_pointer_t<decltype(Reg)>::ParsedOptionsT *
getView(const OptionsContext &Ctx) {
  checkThreaded(Ctx);
  return Ctx.getViewPtr<Reg>();
}

//===----------------------------------------------------------------------===//
// OptionRegistryOf trait — maps option → registry at compile time.
// Specializations are generated by the clv2-options TableGen backend.
// Primary template is undefined; using it without a specialization is a
// compile error, guiding the user to specify the registry explicitly.
//===----------------------------------------------------------------------===//

template <const auto *Opt> struct OptionRegistryOf;

//===----------------------------------------------------------------------===//
// Pack-index fast path
//===----------------------------------------------------------------------===//
//
// ParsedOptions::get<Opt>() resolves Opt's position by scanning the registry's
// whole descriptor pack (index_of_v).  One scan is cheap; doing one per option
// is quadratic in the option count, which is exactly the shape of a registry's
// generated getters.  TableGen knows each option's position and emits it
// alongside the registry in the OptionRegistryOf specialization; the accessors
// below use it when present.
//
// Detection is deliberate rather than mandatory: hand-written registries in
// tools, utils and unit tests have no OptionRegistryOf specialization, and must
// keep working via the scanning path.

template <const auto *Opt, typename = void> struct OptionPackIndex {
  static constexpr bool HasIndex = false;
};
template <const auto *Opt>
struct OptionPackIndex<Opt,
                       std::void_t<decltype(OptionRegistryOf<Opt>::Index)>> {
  static constexpr bool HasIndex = true;
  static constexpr std::size_t Index = OptionRegistryOf<Opt>::Index;
};

/// Opt's emitted pack index, or size_t(-1) when TableGen supplied none.
/// Naming OptionPackIndex<Opt>::Index directly inside a `||` is ill-formed
/// when it does not exist -- short-circuiting applies to evaluation, not to
/// name lookup -- so callers that must tolerate both cases come through here.
template <const auto *Opt> constexpr std::size_t packIndexOrNPos() {
  if constexpr (OptionPackIndex<Opt>::HasIndex)
    return OptionPackIndex<Opt>::Index;
  else
    return static_cast<std::size_t>(-1);
}

/// True when \p Idx is known to be Opt's position in \p Reg's pack.  The
/// registry has to match: a caller naming a registry other than the option's
/// own would otherwise index into the wrong pack.
template <const auto *Reg, auto *Opt> constexpr bool hasPackIndexIn() {
  if constexpr (OptionPackIndex<Opt>::HasIndex)
    // Through void*: the two registries are different OptionsRegistry<...>
    // specializations whenever the option is shared, and comparing unrelated
    // pointer types directly is ill-formed.  Sharing is normal -- lli's
    // registry embeds the CommandFlags CG_* descriptors, for instance.
    return static_cast<const void *>(Reg) ==
           static_cast<const void *>(OptionRegistryOf<Opt>::Reg);
  else
    return false;
}

/// getOptValOr with the pack index supplied.
template <const auto *Reg, std::size_t Idx, auto *Opt, typename DefaultT>
auto getOptValOrAt(const OptionsContext &Ctx, DefaultT Default) {
  checkThreaded(Ctx);
  using ParsedT = typename std::remove_pointer_t<decltype(Reg)>::ParsedOptionsT;
  using RetT = std::remove_cv_t<std::remove_reference_t<
      decltype(std::declval<ParsedT>().template getAt<Idx, Opt>())>>;
  if (auto *V = Ctx.getViewPtr<Reg>())
    return static_cast<RetT>(V->template getAt<Idx, Opt>());
  return static_cast<RetT>(std::forward<DefaultT>(Default));
}

/// getOptValIfSpecified with the pack index supplied.
template <const auto *Reg, std::size_t Idx, auto *Opt, typename DefaultT>
auto getOptValIfSpecifiedAt(const OptionsContext &Ctx, DefaultT Default) {
  checkThreaded(Ctx);
  using ParsedT = typename std::remove_pointer_t<decltype(Reg)>::ParsedOptionsT;
  using RetT = std::remove_cv_t<std::remove_reference_t<
      decltype(std::declval<ParsedT>().template getAt<Idx, Opt>())>>;
  if (auto *V = Ctx.getViewPtr<Reg>())
    if (V->template occurrencesAt<Idx>() > 0)
      return static_cast<RetT>(V->template getAt<Idx, Opt>());
  return static_cast<RetT>(std::forward<DefaultT>(Default));
}

/// wasOptSpecified with the pack index supplied.
template <const auto *Reg, std::size_t Idx>
bool wasOptSpecifiedAt(const OptionsContext &Ctx) {
  checkThreaded(Ctx);
  if (auto *V = Ctx.getViewPtr<Reg>())
    return V->template occurrencesAt<Idx>() > 0;
  return false;
}

//===----------------------------------------------------------------------===//
// Free-function option accessors
//===----------------------------------------------------------------------===//
//
// These take the context by reference: having one is the normal case, and the
// signature says so.  Reads still fall back to the option's compile-time
// default when the *registry* is absent from the context — see getView above
// for why that axis is genuinely different.
//
// The pointer-taking escape hatches for not-yet-threaded callers live at the
// bottom of this header.

/// Get an option value, or \p Default if the registry is not present.
template <const auto *Reg, auto *Opt, typename DefaultT>
auto getOptValOr(const OptionsContext &Ctx, DefaultT Default) {
  // The scanning body lives in the discarded branch so that naming get<Opt>()
  // -- and through it index_of_v -- does not happen on the fast path.
  if constexpr (hasPackIndexIn<Reg, Opt>()) {
    return getOptValOrAt<Reg, OptionPackIndex<Opt>::Index, Opt>(
        Ctx, std::forward<DefaultT>(Default));
  } else {
    checkThreaded(Ctx);
    using ParsedT =
        typename std::remove_pointer_t<decltype(Reg)>::ParsedOptionsT;
    using RetT = std::remove_cv_t<std::remove_reference_t<
        decltype(std::declval<ParsedT>().template get<Opt>())>>;
    if (auto *V = Ctx.getViewPtr<Reg>())
      return static_cast<RetT>(V->template get<Opt>());
    return static_cast<RetT>(std::forward<DefaultT>(Default));
  }
}

/// Get an option value only if it was explicitly specified on the command
/// line.  Returns \p Default if the registry is missing or the option was not
/// specified.
template <const auto *Reg, auto *Opt, typename DefaultT>
auto getOptValIfSpecified(const OptionsContext &Ctx, DefaultT Default) {
  if constexpr (hasPackIndexIn<Reg, Opt>()) {
    return getOptValIfSpecifiedAt<Reg, OptionPackIndex<Opt>::Index, Opt>(
        Ctx, std::forward<DefaultT>(Default));
  } else {
    checkThreaded(Ctx);
    using ParsedT =
        typename std::remove_pointer_t<decltype(Reg)>::ParsedOptionsT;
    using RetT = std::remove_cv_t<std::remove_reference_t<
        decltype(std::declval<ParsedT>().template get<Opt>())>>;
    if (auto *V = Ctx.getViewPtr<Reg>())
      if (V->template specified<Opt>())
        return static_cast<RetT>(V->template get<Opt>());
    return static_cast<RetT>(std::forward<DefaultT>(Default));
  }
}

/// Check whether an option was explicitly specified.  Returns false if the
/// registry is not present.
template <const auto *Reg, auto *Opt>
bool wasOptSpecified(const OptionsContext &Ctx) {
  if constexpr (hasPackIndexIn<Reg, Opt>()) {
    return wasOptSpecifiedAt<Reg, OptionPackIndex<Opt>::Index>(Ctx);
  } else {
    checkThreaded(Ctx);
    if (auto *V = Ctx.getViewPtr<Reg>())
      return V->template specified<Opt>();
    return false;
  }
}

//===----------------------------------------------------------------------===//
// Single-argument accessor overloads — deduce registry from trait
//===----------------------------------------------------------------------===//
//
// The registry is named in a defaulted template parameter rather than in a
// trailing return type.  Spelling the return type as
// decltype(getOptValOr<Reg, Opt>(...)) would name ParsedOptions::get<Opt>()
// and so instantiate index_of_v in the signature, defeating the fast path
// below.  The defaulted parameter keeps the same SFINAE behaviour: an option
// with no OptionRegistryOf specialization still removes the overload.

template <auto *Opt, typename DefaultT,
          const auto *Reg = OptionRegistryOf<Opt>::Reg>
auto getOptValOr(const OptionsContext &Ctx, DefaultT Default) {
  if constexpr (OptionPackIndex<Opt>::HasIndex)
    return getOptValOrAt<Reg, OptionPackIndex<Opt>::Index, Opt>(
        Ctx, std::forward<DefaultT>(Default));
  else
    return getOptValOr<Reg, Opt>(Ctx, std::forward<DefaultT>(Default));
}

template <auto *Opt, typename DefaultT,
          const auto *Reg = OptionRegistryOf<Opt>::Reg>
auto getOptValIfSpecified(const OptionsContext &Ctx, DefaultT Default) {
  if constexpr (OptionPackIndex<Opt>::HasIndex)
    return getOptValIfSpecifiedAt<Reg, OptionPackIndex<Opt>::Index, Opt>(
        Ctx, std::forward<DefaultT>(Default));
  else
    return getOptValIfSpecified<Reg, Opt>(Ctx, std::forward<DefaultT>(Default));
}

// Unlike the two above, this one keeps a single template parameter: its
// two-argument sibling also takes two *values*, so a defaulted second
// parameter here would make wasOptSpecified<&Reg, &Opt>(Ctx) match both
// overloads.  (getOptValOr is safe because its second parameter is a type.)
template <auto *Opt> bool wasOptSpecified(const OptionsContext &Ctx) {
  constexpr const auto *Reg = OptionRegistryOf<Opt>::Reg;
  if constexpr (OptionPackIndex<Opt>::HasIndex)
    return wasOptSpecifiedAt<Reg, OptionPackIndex<Opt>::Index>(Ctx);
  else
    return wasOptSpecified<Reg, Opt>(Ctx);
}

/// The value \p Opt stands for when it was not given: its Init default if the
/// descriptor has one, else a value-initialised T.
///
/// Unlike getOptValOrDefault this is not SFINAE-gated, so it can be used
/// uniformly over a mixed set of descriptors -- including list options, which
/// carry no Init at all.  Pair it with getOptValOr to reproduce "unspecified
/// means the declared default" for a whole family of getters.
template <typename T, typename = void>
struct HasInitDefault : std::false_type {};
template <typename T>
struct HasInitDefault<T, std::void_t<decltype(T::HasDefault)>>
    : std::true_type {};

template <auto *Opt, typename T> constexpr T descriptorDefault() {
  using OptT = std::remove_cv_t<std::remove_reference_t<decltype(*Opt)>>;
  if constexpr (HasInitDefault<OptT>::value) {
    if constexpr (Opt->HasDefault)
      return T(Opt->DefaultValue);
    else
      return T{};
  } else {
    return T{};
  }
}

/// Get an option value from an OptionsContext using the option's compile-time
/// default (from Init{} in the .td definition).  Only participates in
/// overload resolution when the option has an explicit Init; calling it on
/// an option without Init is a compile error.
template <auto *Opt, std::enable_if_t<Opt->HasDefault, int> = 0>
auto getOptValOrDefault(const OptionsContext &Ctx) {
  return getOptValOr<Opt>(Ctx, Opt->DefaultValue);
}

} // namespace clv2
} // namespace llvm

#endif // LLVM_SUPPORT_OPTIONSCONTEXT_H
