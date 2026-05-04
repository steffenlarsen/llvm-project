//===- llvm/Support/CommandLineV2.h - Compile-time CLI option interface ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a v2 command-line option interface for LLVM. Unlike
// CommandLine.h, option descriptors are constexpr (zero global-constructor
// overhead), and parsing produces an owned ParsedOptions value rather than
// writing into global singletons, enabling parallel compilation jobs.
//
// Typical usage:
//
//   namespace MyLib {
//     // Declare constexpr descriptors at namespace scope.
//     // 'inline constexpr' gives static storage duration with no global ctor.
//     inline constexpr clv2::OptionInfo<int> Verbose{
//         "verbose", "Verbosity level", clv2::Init{0}};
//     inline constexpr clv2::OptionInfo<bool> Quiet{
//         "quiet", "Suppress output"};
//
//     // Assemble a registry.  The pointer NTTPs encode membership in the type.
//     inline constexpr clv2::OptionsRegistry<&Verbose, &Quiet> MyRegistry;
//   }
//
//   // In main() (or a per-job entry point):
//   clv2::OptionParser P;
//   P.add<&MyLib::MyRegistry>();
//   auto Ctx = P.parse(argc, argv, "My tool");
//
//   // Typed access via OptionsContext.
//   int V = clv2::getOptValOr<&MyLib::Verbose>(*Ctx, 0);
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_COMMANDLINEV2_H
#define LLVM_SUPPORT_COMMANDLINEV2_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/BoolOrDefault.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/TypeSize.h"
#include "llvm/Support/raw_ostream.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

namespace llvm {
namespace clv2 {

//===----------------------------------------------------------------------===//
// Enumerations (identical semantics to CommandLine.h counterparts)
//===----------------------------------------------------------------------===//

enum OptionNumOccurrencesFlag {
  Optional = 0x00,     ///< Zero or one occurrence
  ZeroOrMore = 0x01,   ///< Zero or more occurrences
  Required = 0x02,     ///< Exactly one occurrence required
  OneOrMore = 0x03,    ///< One or more occurrences required
  ConsumeAfter = 0x04, ///< Consume all args after last required positional
};

/// What a parse does when it fails, or after it prints --help/--version.
///
/// This used to be implied by whether an error stream was supplied -- a null
/// Errs meant "terminate the process" -- which is a surprising thing for a
/// library entry point to decide, and fatal for a tool embedded in a
/// long-running process (see CompiledParser, where a bad option in one job
/// must not take down the others).
enum class OnError {
  ExitProcess, ///< std::exit(): the historical cl:: behaviour for tools
  Return,      ///< return nullptr/false; never terminates the process
};

enum OptionValueExpected {
  ValueOptional = 0x01,   ///< Value may be present or absent
  ValueRequired = 0x02,   ///< Value must be present
  ValueDisallowed = 0x03, ///< No value allowed (flag-only)
};

enum OptionHidden {
  NotHidden = 0x00,   ///< Shown in -help and -help-hidden
  Hidden = 0x01,      ///< Shown only in -help-hidden
  ReallyHidden = 0x02 ///< Never shown in help
};

/// Formatting modifiers (Prefix / AlwaysPrefix replace the old
/// FormattingFlags).
enum FormattingFlags {
  NormalFormat = 0x00,      ///< Standard -opt or -opt=val
  PrefixFormat = 0x01,      ///< -optval (value glued to name)
  AlwaysPrefixFormat = 0x02 ///< Always -opt=val
};

enum MiscFlags : unsigned {
  CommaSeparated = 1 << 0, ///< Split values on commas (list options)
  Sink = 1 << 1,           ///< Accept unknown options (like cl::Sink)
  DefaultOption = 1 << 2,  ///< Applied before explicit args
  Grouping = 1 << 3,       ///< -abc groups -a -b -c (single-char flags)
  PositionalEatsArgs = 1
      << 4, ///< Positional list eats all remaining args including -flags
  CaseInsensitiveValues = 1 << 5, ///< Match enum value names case-insensitively
};

//===----------------------------------------------------------------------===//
// Modifier tag types
//
// These are passed as variadic arguments to option descriptor constructors.
// The HandleArg() overload set in CommonOptionInfo / OptionInfo dispatches on
// the concrete tag type, setting the appropriate descriptor field.
//===----------------------------------------------------------------------===//

/// Default-value modifier.  Example: Init{42}, Init{true}, Init{"hello"}.
template <typename T> struct Init {
  T Value;
  constexpr explicit Init(T V) : Value(V) {}
};
template <typename T> Init(T) -> Init<T>;

namespace detail {
struct ParseDiag; // defined below; validators report through it.
} // namespace detail

/// Post-parse value validation.  \p Fn is called once per occurrence, right
/// after the value is parsed into the slot, and returns false to reject it.
///
/// Rejections travel back through ParseDiag like any other parse error, so
/// they honour the parser's OnError policy.  That matters: a validator that
/// called exit() directly would kill the host process even when the caller
/// asked for OnError::ReturnFailure.
template <typename T> struct Validate {
  bool (*Fn)(const T &, llvm::StringRef, detail::ParseDiag &);
  constexpr explicit Validate(bool (*F)(const T &, llvm::StringRef,
                                        detail::ParseDiag &))
      : Fn(F) {}
};
template <typename T>
Validate(bool (*)(const T &, llvm::StringRef, detail::ParseDiag &))
    -> Validate<T>;

/// Marks the option as positional (no leading dash on the command line).
struct Positional {};

/// Category association modifier.  Use clv2::cat(MyCat) in the descriptor.
struct OptionCategory; // forward-declared; full definition below
struct CatTag {
  const OptionCategory *Cat;
};

/// Value-description modifier.  Controls the placeholder shown in help output.
/// Example: value_desc("filename") → -o=<filename> instead of -o=<value>.
struct ValDesc {
  const char *Text;
};
constexpr ValDesc value_desc(const char *S) { return {S}; }

/// Suppress the value placeholder in help output.
/// Use for options that are technically ValueOptional but should display as
/// flags.
struct SuppressValPlaceholder {};
constexpr SuppressValPlaceholder suppress_val_placeholder{};

/// Per-value callback modifier.  Invoked after each successful parse.
/// Must be a plain function pointer so the descriptor remains constexpr.
template <typename T> struct Callback {
  void (*Fn)(const T &);
  constexpr explicit Callback(void (*F)(const T &)) : Fn(F) {}
};
template <typename T> Callback(void (*)(const T &)) -> Callback<T>;

/// Per-value callback carrying a caller-supplied context, for cases where the
/// action needs state a plain function pointer cannot reach.  The context is a
/// runtime pointer, so a descriptor using this generally cannot be constexpr —
/// it is meant for runtime-constructed OptionInfo objects (see the comment on
/// OptionInfo).  Invoked after Callback, if both are present.
///
/// Unlike Callback, this returns bool: returning false marks the value as
/// invalid and fails the parse, which lets a callback replace a hand-written
/// parser that validated its input.
template <typename T> struct CtxCallback {
  bool (*Fn)(void *Ctx, const T &);
  void *Ctx;
  constexpr CtxCallback(bool (*F)(void *, const T &), void *C)
      : Fn(F), Ctx(C) {}
};

//===----------------------------------------------------------------------===//
// OptionCategory
//
// Purely a compile-time metadata tag — no registration, no global ctor.
// Store inline constexpr at namespace scope to refer to it from descriptors.
//===----------------------------------------------------------------------===//

struct OptionCategory {
  const char *Name;
  const char *Desc;
  constexpr OptionCategory(const char *N, const char *D = "")
      : Name(N), Desc(D) {}
};

/// Modifier factory function: clv2::cat(MyCat)
constexpr CatTag cat(const OptionCategory &C) { return {&C}; }

/// The built-in category for help and version options.  Entries assigned to
/// this category are injected automatically by the parser and printed under
/// the "Generic Options" section of --help output.
LLVM_ABI extern const OptionCategory GenericOptionsCategory;

/// Category for the --color option.  Defined in SupportOptionsOptInfos.inc;
/// include SupportOptionsOptInfos.h to get it.

//===----------------------------------------------------------------------===//
// Enum value table support
//
// For enum options, declare a values table alongside the descriptor:
//
//   inline constexpr clv2::EnumVal<OptLvl> LevelVals[] = {
//       {"O0", OptLvl::O0, "No optimization"},
//       {"O2", OptLvl::O2, "Default optimization"},
//   };
//   inline constexpr auto LvlOpt = clv2::makeEnumOptionInfo<OptLvl>(
//       "opt-level", "Optimization level", LevelVals, clv2::Init{OptLvl::O0});
//
//===----------------------------------------------------------------------===//

template <typename EnumT> struct EnumVal {
  const char *Name;
  EnumT Value;
  const char *Desc = "";
};

/// A tag carrying a pointer+size to a static EnumVal array.
/// The array must have static storage duration (e.g. inline constexpr []).
template <typename EnumT> struct ValuesRef {
  const EnumVal<EnumT> *Vals;
  std::size_t NumVals;

  template <std::size_t N>
  constexpr ValuesRef(const EnumVal<EnumT> (&Arr)[N]) : Vals(Arr), NumVals(N) {}

  constexpr ValuesRef(const EnumVal<EnumT> *V, std::size_t N)
      : Vals(V), NumVals(N) {}

  constexpr const EnumVal<EnumT> *begin() const { return Vals; }
  constexpr const EnumVal<EnumT> *end() const { return Vals + NumVals; }
};

//===----------------------------------------------------------------------===//
// DescriptorDefault trait
//
// For std::string options the stored default is const char* (so the descriptor
// itself is constexpr-friendly); the parser converts to std::string at
// parse time.
//===----------------------------------------------------------------------===//

template <typename T> struct DescriptorDefault {
  using Type = T;
};
template <> struct DescriptorDefault<std::string> {
  using Type = const char *;
};

//===----------------------------------------------------------------------===//
// CommonOptionInfo
//
// Base for all option descriptor types.  Holds the common metadata fields
// and the HandleArg() dispatch mechanism.
//===----------------------------------------------------------------------===//

class CommonOptionInfo {
public:
  const char *CLIName;
  const char *CLIDescription;
  bool IsPositional = false;
  OptionNumOccurrencesFlag NumOccurrencesFlag = Optional;
  OptionValueExpected ValueExpected = ValueOptional;
  bool ValueExplicitlySet = false; ///< True when ValueExpected was set by user
  const char *ValueDesc = nullptr; ///< Custom placeholder text for help output
  bool NoValPlaceholder = false;   ///< Suppress value placeholder in help
  OptionHidden OptionHiddenFlag = NotHidden;
  FormattingFlags FormattingFlag = NormalFormat;
  unsigned MiscFlagsBits = 0;
  const OptionCategory *Category = nullptr;

protected:
  // Fallback: fires a compile-time error for unrecognised modifier types.
  // sizeof(ArgT) == 0 is value-dependent so the assert only fires on
  // instantiation, not on template definition (avoiding NDR in C++17).
  template <typename ArgT> constexpr void HandleArg(ArgT) {
    static_assert(sizeof(ArgT) == 0,
                  "Unexpected argument type for option modifier");
  }

  constexpr void HandleArg(Positional) {
    IsPositional = true;
    FormattingFlag =
        NormalFormat; // positional-ness is tracked via IsPositional
  }
  constexpr void HandleArg(OptionNumOccurrencesFlag F) {
    NumOccurrencesFlag = F;
  }
  constexpr void HandleArg(OptionValueExpected V) {
    ValueExpected = V;
    ValueExplicitlySet = true;
  }
  constexpr void HandleArg(OptionHidden H) { OptionHiddenFlag = H; }
  constexpr void HandleArg(FormattingFlags F) { FormattingFlag = F; }
  constexpr void HandleArg(MiscFlags F) {
    MiscFlagsBits |= static_cast<unsigned>(F);
  }
  constexpr void HandleArg(CatTag C) { Category = C.Cat; }
  constexpr void HandleArg(ValDesc V) { ValueDesc = V.Text; }
  constexpr void HandleArg(SuppressValPlaceholder) { NoValPlaceholder = true; }

  constexpr CommonOptionInfo(const char *Name, const char *Desc)
      : CLIName(Name), CLIDescription(Desc) {}
};

//===----------------------------------------------------------------------===//
// OptionInfo<ValueType> — scalar option descriptor
//
// A scalar option.  Declare inline constexpr at namespace scope.
//===----------------------------------------------------------------------===//

template <typename ValueType> class OptionInfo : public CommonOptionInfo {
public:
  using ValueT = ValueType;
  using DefaultT = typename DescriptorDefault<ValueType>::Type;

  DefaultT DefaultValue{};
  bool HasDefault = false;

  // Enum values table (null if not an enum option)
  const EnumVal<ValueType> *EnumVals = nullptr;
  std::size_t NumEnumVals = 0;

  void (*CallbackFn)(const ValueType &) = nullptr;
  bool (*CtxCallbackFn)(void *, const ValueType &) = nullptr;
  void *CallbackCtx = nullptr;
  /// See the Validate modifier.  Null when the option has no constraint.
  bool (*ValidateFn)(const ValueType &, llvm::StringRef,
                     detail::ParseDiag &) = nullptr;

protected:
  using CommonOptionInfo::HandleArg;

  constexpr void HandleArg(CtxCallback<ValueType> C) {
    CtxCallbackFn = C.Fn;
    CallbackCtx = C.Ctx;
  }
  constexpr void HandleArg(Init<DefaultT> I) {
    DefaultValue = I.Value;
    HasDefault = true;
  }
  // Accept Init<U> when U is implicitly convertible to DefaultT.
  template <typename U>
  constexpr std::enable_if_t<std::is_convertible_v<U, DefaultT> &&
                             !std::is_same_v<U, DefaultT>>
  HandleArg(Init<U> I) {
    DefaultValue = static_cast<DefaultT>(I.Value);
    HasDefault = true;
  }
  // std::string is not a literal type in C++17, so Init{std::string(...)}
  // cannot appear in a constexpr OptionInfo.  Without this overload the
  // failure surfaces as the generic "Unexpected argument type" fallback,
  // which points at the wrong thing entirely.
  // Taken by reference, not by value: a constexpr function's parameter types
  // must be literal, and clang enforces that on the *declaration* even though
  // this overload is never callable.  A reference type is literal.
  template <typename U = ValueType>
  constexpr void HandleArg(const Init<std::string> &) {
    static_assert(sizeof(U) == 0,
                  "Init{std::string(...)} cannot be used in a constexpr "
                  "OptionInfo: std::string is not a literal type in C++17. "
                  "Use a string literal instead, e.g. Init{\"text\"}.");
  }

  constexpr void HandleArg(ValuesRef<ValueType> V) {
    EnumVals = V.Vals;
    NumEnumVals = V.NumVals;
  }
  constexpr void HandleArg(Callback<ValueType> C) { CallbackFn = C.Fn; }
  constexpr void HandleArg(Validate<ValueType> V) { ValidateFn = V.Fn; }

public:
  template <typename... Args>
  constexpr OptionInfo(const char *Name, const char *Desc, Args &&...args)
      : CommonOptionInfo(Name, Desc) {
    (HandleArg(args), ...);
  }
};

//===----------------------------------------------------------------------===//
// ListOptionInfo<ValueType> — multi-value option descriptor
//
// A repeatable option.  Parsed values accumulate into std::vector<T>.
//===----------------------------------------------------------------------===//

template <typename ValueType> class ListOptionInfo : public CommonOptionInfo {
public:
  using ValueT = ValueType;

  const EnumVal<ValueType> *EnumVals = nullptr;
  std::size_t NumEnumVals = 0;

  void (*CallbackFn)(const ValueType &) = nullptr;
  bool (*CtxCallbackFn)(void *, const ValueType &) = nullptr;
  void *CallbackCtx = nullptr;
  /// See the Validate modifier.  Runs on each element as it is appended, so a
  /// rejected value is named even in a comma-separated or repeated list.
  bool (*ValidateFn)(const ValueType &, llvm::StringRef,
                     detail::ParseDiag &) = nullptr;

protected:
  using CommonOptionInfo::HandleArg;
  constexpr void HandleArg(Validate<ValueType> V) { ValidateFn = V.Fn; }

  constexpr void HandleArg(ValuesRef<ValueType> V) {
    EnumVals = V.Vals;
    NumEnumVals = V.NumVals;
  }
  constexpr void HandleArg(Callback<ValueType> C) { CallbackFn = C.Fn; }
  constexpr void HandleArg(CtxCallback<ValueType> C) {
    CtxCallbackFn = C.Fn;
    CallbackCtx = C.Ctx;
  }

public:
  template <typename... Args>
  constexpr ListOptionInfo(const char *Name, const char *Desc, Args &&...args)
      : CommonOptionInfo(Name, Desc) {
    (HandleArg(args), ...);
  }
};

//===----------------------------------------------------------------------===//
// BitsOptionInfo<T> — bitmask multi-set enum option descriptor
//
// A flags option.  Each occurrence ORs a bit into an unsigned
// bitmask.  get<&Opt>() returns unsigned; callers cast to T as needed.
// The bit position of each value is its index in the EnumVals array.
//===----------------------------------------------------------------------===//

template <typename T> struct BitsOptionInfo : CommonOptionInfo {
  using ValueT = T;
  using StorageT =
      unsigned; // picked up by void_t<D::StorageT> in StorageTypeOf

  const EnumVal<T> *EnumVals = nullptr;
  std::size_t NumEnumVals = 0;

  template <std::size_t N, typename... Args>
  constexpr BitsOptionInfo(const char *Name, const char *Desc,
                           const EnumVal<T> (&Vals)[N], Args &&...args)
      : CommonOptionInfo(Name, Desc), EnumVals(Vals), NumEnumVals(N) {
    (HandleArg(args), ...);
    MiscFlagsBits |= CommaSeparated;
  }
};

//===----------------------------------------------------------------------===//
// AliasInfo — option alias descriptor
//
// An alternate spelling.  AliasFor names the target option's CLIName.
//===----------------------------------------------------------------------===//

/// Empty base class used to identify AliasInfo without relying on partial
/// specialisation of auto*... packs (which is ill-formed in C++17).
struct AliasTag {};

/// True when T is AliasInfo.
template <typename T>
inline constexpr bool IsAliasInfo_v = std::is_base_of_v<AliasTag, T>;

struct AliasInfo : AliasTag {
  const char *CLIName;
  const char *AliasFor; ///< CLIName of the target option

  /// Picked up by StorageTypeOf<D, void_t<D::StorageT>> — aliases carry no
  /// parsed value of their own; they proxy the target's slot.
  /// Aliases have no storage of their own; this is just a placeholder type.
  /// A local empty struct rather than std::monostate, so the header does not
  /// pull in <variant> for one tag type.
  struct NoStorage {};
  using StorageT = NoStorage;

  const char *Desc = nullptr;
  OptionHidden HiddenFlag = NotHidden;

  constexpr AliasInfo(const char *Name, const char *Target)
      : CLIName(Name), AliasFor(Target) {}
  constexpr AliasInfo(const char *Name, const char *Target, const char *D)
      : CLIName(Name), AliasFor(Target), Desc(D) {}
  constexpr AliasInfo(const char *Name, const char *Target, const char *D,
                      OptionHidden H)
      : CLIName(Name), AliasFor(Target), Desc(D), HiddenFlag(H) {}
};

//===----------------------------------------------------------------------===//
// StorageTypeOf trait
//
// Maps an option descriptor type to the runtime storage type held in
// ParsedOptions.  Every descriptor must define ValueT.
//===----------------------------------------------------------------------===//

// Primary: use D::ValueT (covers OptionInfo<T> and EnumOptionInfo<T>).
template <typename D, typename = void> struct StorageTypeOf {
  using Type = typename D::ValueT;
};

// If D provides a StorageT member (e.g. SubCommandInfo<...>), use that
// instead.  The void_t trick avoids partial specialisation on auto*... packs,
// which is ill-formed in C++17.
template <typename D>
struct StorageTypeOf<D, std::void_t<typename D::StorageT>> {
  using Type = typename D::StorageT;
};

// List options store a vector of their element type.  Explicit specialisation
// takes priority over the void_t overload (ListOptionInfo has ValueT, not
// StorageT).
/// Storage for a list option: the parsed values plus, alongside them, the argv
/// index each element came from.  Keeping the positions here rather than in a
/// parallel array in ParsedOptions means only list options pay for them: list
/// options are a small minority, so a per-option array would be mostly empty.
template <typename T> struct ListStorage {
  std::vector<T> Values;
  std::vector<unsigned> Positions;
};

template <typename T> struct StorageTypeOf<ListOptionInfo<T>, void> {
  using Type = ListStorage<T>;
};

// Forward declaration needed so SubCommandInfo can form its StorageT member
// as std::optional<ParsedOptions<SubOpts...>> with ParsedOptions incomplete.
// Full definition follows later in this header.
namespace detail {
/// Grants the free entry-building helpers access to ParsedOptions internals.
/// One friend instead of a friend declaration per helper.
struct POAccess;
} // namespace detail

template <auto *...Infos> class ParsedOptions;

/// Empty base class used to identify SubCommandInfo specialisations without
/// relying on partial specialisation matching of auto*... packs (which is
/// ill-formed in C++17).  Use std::is_base_of_v<SubCommandTag, T> or the
/// helper IsSubCommandInfo_v<T> instead of a trait with partial specs.
struct SubCommandTag {};

/// True when T is any SubCommandInfo<...> specialisation.
template <typename T>
inline constexpr bool IsSubCommandInfo_v = std::is_base_of_v<SubCommandTag, T>;

/// True when T is a ListOptionInfo<U>.  ListOptionInfo is an ordinary class
/// template, so partial specialisation is fine here -- unlike the auto*... pack
/// cases above, which need a tag base.
template <typename T> struct IsListOptionInfoImpl : std::false_type {};
template <typename T>
struct IsListOptionInfoImpl<ListOptionInfo<T>> : std::true_type {};
template <typename T>
inline constexpr bool IsListOptionInfo_v =
    IsListOptionInfoImpl<std::remove_const_t<T>>::value;

//===----------------------------------------------------------------------===//
// SubCommandInfo<SubOpts...> — subcommand descriptor (full definition)
//
// Declare at namespace scope as inline constexpr, then include in a registry:
//
//   inline constexpr clv2::OptionInfo<int> OptLevel{"level", "Opt level"};
//   inline constexpr clv2::SubCommandInfo<&OptLevel> OptCmd{"opt", "Optimise"};
//   inline constexpr clv2::OptionsRegistry<&GlobalOpt, &OptCmd> TopReg;
//
//   OptionParser P;
//   P.add<&TopReg>();
//   auto Ctx = P.parse(argc, argv);
//
// SubCommandInfo carries only constexpr data (Name + Desc).  The runtime
// parsing machinery lives in OptionsRegistry::buildEntries so that
// SubCommandInfo itself does not depend on the full ParsedOptions definition.
//===----------------------------------------------------------------------===//

template <auto *...SubOpts> struct SubCommandInfo : SubCommandTag {
  const char *Name;
  const char *Desc;
  constexpr SubCommandInfo(const char *N, const char *D) : Name(N), Desc(D) {}

  // StorageT is picked up by StorageTypeOf<D, void_t<D::StorageT>>.
  // Forming std::optional<ParsedOptions<...>> with an incomplete ParsedOptions
  // is valid — std::optional only requires completeness when constructed.
  using StorageT = std::optional<ParsedOptions<SubOpts...>>;
};

//===----------------------------------------------------------------------===//
// index_of_v<Needle, Haystack...>
//
// Compile-time search for Needle (a pointer NTTP) in the Haystack pack.
// Comparisons are done via const void* so that differently-typed pointers
// (e.g. OptionInfo<int>* vs OptionInfo<bool>*) compare safely.
//
// detail::indexOfImpl() returns static_cast<size_t>(-1) when Needle is absent.
// index_of_v does NOT: it fails to compile instead, so a mistyped option in a
// get<>()/specified<>() call is a build error rather than a bad index.
//===----------------------------------------------------------------------===//

namespace detail {

template <auto *Needle, auto *...Haystack> constexpr std::size_t indexOfImpl() {
  const void *NeedlePtr = static_cast<const void *>(Needle);
  // Use a C-array + loop — valid constexpr in C++17.
  const void *HaystackPtrs[] = {static_cast<const void *>(Haystack)...};
  for (std::size_t I = 0; I < sizeof...(Haystack); ++I)
    if (HaystackPtrs[I] == NeedlePtr)
      return I;
  return static_cast<std::size_t>(-1);
}

/// One option's storage slot.  Indexed by pack position so that two options
/// with the same storage type stay distinct base classes.
///
/// Value is brace-initialised so a default-constructed FlatStorage
/// value-initialises every slot, matching what std::tuple's default
/// constructor did before.  Without the braces, scalar slots would start as
/// indeterminate values.
template <std::size_t I, typename T> struct StorageSlot {
  T Value{};
};

/// Flat, non-recursive replacement for std::tuple as ParsedOptions' storage.
/// Inheriting N one-member bases in a single instantiation avoids the nested
/// _Tuple_impl chain, whose cost is quadratic in N (see ParsedOptions).
template <typename Seq, typename... Ts> struct FlatStorage;
template <std::size_t... Is, typename... Ts>
struct FlatStorage<std::index_sequence<Is...>, Ts...> : StorageSlot<Is, Ts>... {
};

} // namespace detail

/// Compile-time index of \p Needle in \p Haystack.  Triggers a static_assert
/// via an out-of-bounds std::get if Needle is not in the pack.
template <auto *Needle, auto *...Haystack>
inline constexpr std::size_t index_of_v =
    detail::indexOfImpl<Needle, Haystack...>();

//===----------------------------------------------------------------------===//
// ParsedOptionsBase — type-erased base for LLVMContext storage
//
// LLVMContext holds a std::shared_ptr<ParsedOptionsBase>, which allows
// heterogeneous registry types across subsystems without pulling concrete
// template types into the LLVMContext headers.
//===----------------------------------------------------------------------===//

class ParsedOptionsBase {
public:
  virtual ~ParsedOptionsBase() = default;

  /// RTTI-free type identity.  Each ParsedOptions<...> specialisation returns
  /// a unique static address as its type key.  Used by LLVMContext::getOptions
  /// to check the dynamic type without requiring -frtti.
  virtual const void *typeKey() const = 0;
};

//===----------------------------------------------------------------------===//
// ParsedOptions<Infos...> — typed parsed-value store
//
// Created by OptionParser::parse() internally.  Accessed via OptionsContext.
//===----------------------------------------------------------------------===//

template <auto *...Infos> class ParsedOptions : public ParsedOptionsBase {
  // Storage type for the option at pack position I.
  template <auto *Info>
  using SlotTypeOf = typename StorageTypeOf<
      std::remove_const_t<std::remove_pointer_t<decltype(Info)>>>::Type;

  // One slot per option, type derived from descriptor.  Not a std::tuple:
  // libstdc++ builds one as N nested levels, each re-instantiated with the
  // remaining type list, so compile cost is quadratic in the option count and
  // is paid by every TU that reads one of the registry's options.  N
  // independent one-member bases are linear, and index access is a
  // static_cast rather than a walk down the inheritance chain.
  using Storage =
      detail::FlatStorage<std::index_sequence_for<decltype(Infos)...>,
                          SlotTypeOf<Infos>...>;

  Storage Values;

  /// The slot for the option at pack position \p I.  \p Info must be the
  /// descriptor at that same position; every caller expands Infos... and an
  /// index_sequence together, so the two always agree.
  template <std::size_t I, auto *Info> auto &slotAt() {
    return static_cast<detail::StorageSlot<I, SlotTypeOf<Info>> &>(Values)
        .Value;
  }
  template <std::size_t I, auto *Info> const auto &slotAt() const {
    return static_cast<const detail::StorageSlot<I, SlotTypeOf<Info>> &>(Values)
        .Value;
  }

  std::array<unsigned, sizeof...(Infos)> Occurrences{};
  /// argv index of each option's last occurrence; 0 means never specified.
  std::array<unsigned, sizeof...(Infos)> Positions{};

  // Pointer to the active SubCommandInfo (nullptr = top-level).
  const void *ActiveSubCommand = nullptr;

  // Give OptionsRegistry access to fill Values, Occurrences and Positions
  // during parsing.
  template <auto *...> friend class OptionsRegistry;
  friend struct detail::POAccess;

public:
  ParsedOptions() = default;
  ParsedOptions(ParsedOptions &&) = default;
  ParsedOptions &operator=(ParsedOptions &&) = default;

  /// COPYING IS A DETACHED SNAPSHOT, NOT AN ALIAS.
  ///
  /// An OptionEntry built from this object holds raw pointers into Values,
  /// Occurrences and Positions.  A copy does not rebind them:
  /// the parser keeps writing into the *original*, and the copy freezes
  /// whatever had been parsed at the moment it was made.  Likewise, moving an
  /// object that entries were built against leaves those entries pointing at
  /// the moved-from husk.
  ///
  /// That snapshot behaviour is what OptionsContext wants (see clone() below),
  /// so the copy cannot be deleted: a registry containing subcommands stores
  /// std::optional<ParsedOptions<...>> in its own tuple, and both std::optional
  /// and std::tuple need the copy to be accessible.  Prefer clone() at
  /// deliberate copy sites so the intent is visible at the call.
  ParsedOptions(const ParsedOptions &) = default;
  ParsedOptions &operator=(const ParsedOptions &) = default;

  /// Deep copy, detached from any OptionEntry that points at this object.
  /// Spelled out so that snapshot-taking reads as deliberate.
  ParsedOptions clone() const { return ParsedOptions(*this); }

  /// Value for the option at known pack position \p Idx.
  ///
  /// Same as get<Info>(), but with the index supplied rather than searched
  /// for.  index_of_v scans the whole descriptor pack, so resolving it once
  /// per option -- which is what a registry's generated getters do -- is
  /// quadratic in the option count.  TableGen knows each option's position and
  /// emits it as OptionRegistryOf<Opt>::Index; the accessors in
  /// OptionsContext.h route through here when it is available.
  template <std::size_t Idx, auto *Info> auto &getAt() {
    static_assert(Idx < sizeof...(Infos), "option index out of range");
    if constexpr (IsListOptionInfo_v<std::remove_pointer_t<decltype(Info)>>)
      return slotAt<Idx, Info>().Values;
    else
      return slotAt<Idx, Info>();
  }
  template <std::size_t Idx, auto *Info> const auto &getAt() const {
    static_assert(Idx < sizeof...(Infos), "option index out of range");
    if constexpr (IsListOptionInfo_v<std::remove_pointer_t<decltype(Info)>>)
      return slotAt<Idx, Info>().Values;
    else
      return slotAt<Idx, Info>();
  }

  /// Occurrence count for the option at known pack position \p Idx.
  template <std::size_t Idx> unsigned occurrencesAt() const {
    static_assert(Idx < sizeof...(Infos), "option index out of range");
    return Occurrences[Idx];
  }

  /// Return a reference to the parsed value for option \p Info.
  /// The index is resolved at compile time — no runtime map lookup.
  template <auto *Info> auto &get() {
    constexpr std::size_t Idx = index_of_v<Info, Infos...>;
    static_assert(Idx != static_cast<std::size_t>(-1),
                  "Option not present in this registry");
    // A list option's slot also carries its per-element positions; callers of
    // get() want just the values.
    if constexpr (IsListOptionInfo_v<std::remove_pointer_t<decltype(Info)>>)
      return slotAt<Idx, Info>().Values;
    else
      return slotAt<Idx, Info>();
  }

  template <auto *Info> const auto &get() const {
    constexpr std::size_t Idx = index_of_v<Info, Infos...>;
    static_assert(Idx != static_cast<std::size_t>(-1),
                  "Option not present in this registry");
    if constexpr (IsListOptionInfo_v<std::remove_pointer_t<decltype(Info)>>)
      return slotAt<Idx, Info>().Values;
    else
      return slotAt<Idx, Info>();
  }

  /// Return the number of times option \p Info appeared on the command line.
  template <auto *Info> unsigned occurrences() const {
    constexpr std::size_t Idx = index_of_v<Info, Infos...>;
    static_assert(Idx != static_cast<std::size_t>(-1),
                  "Option not present in this registry");
    return Occurrences[Idx];
  }

  /// Returns true if option \p Info was specified at least once.
  template <auto *Info> bool specified() const {
    return occurrences<Info>() > 0;
  }

  /// Returns the argv index of the last occurrence of \p Info on the command
  /// line, or 0 if the option was never specified.  Useful for resolving
  /// precedence between mutually-overriding options (the higher index wins).
  template <auto *Info> unsigned position() const {
    constexpr std::size_t Idx = index_of_v<Info, Infos...>;
    static_assert(Idx != static_cast<std::size_t>(-1),
                  "Option not present in this registry");
    return Positions[Idx];
  }

  /// Returns the per-element argv indices for a list option \p Info.
  /// The i-th entry is the argv index at which the i-th element was parsed.
  /// Only list options have element positions; asking for a scalar option's is
  /// a compile error rather than a silently empty vector.
  template <auto *Info> const std::vector<unsigned> &elementPositions() const {
    constexpr std::size_t Idx = index_of_v<Info, Infos...>;
    static_assert(Idx != static_cast<std::size_t>(-1),
                  "Option not present in this registry");
    static_assert(IsListOptionInfo_v<std::remove_pointer_t<decltype(Info)>>,
                  "element positions exist only for list options");
    return slotAt<Idx, Info>().Positions;
  }

  /// Returns true when the subcommand descriptor \p Cmd was the active
  /// subcommand on the command line.  Only valid for SubCommandInfo entries.
  template <auto *Cmd> bool isActive() const {
    constexpr std::size_t Idx = index_of_v<Cmd, Infos...>;
    static_assert(Idx != static_cast<std::size_t>(-1),
                  "Subcommand not present in this registry");
    return slotAt<Idx, Cmd>().has_value();
  }

  /// Returns a reference to the parsed sub-options for subcommand \p Cmd.
  /// Only valid when isActive<Cmd>(); asserts otherwise.
  ///
  /// std::optional::value() is deliberately avoided: LLVM builds with
  /// -fno-exceptions, so the bad_optional_access it would throw becomes a
  /// std::terminate with no indication of which subcommand was at fault.
  template <auto *Cmd> auto &getSubOptions() {
    constexpr std::size_t Idx = index_of_v<Cmd, Infos...>;
    static_assert(Idx != static_cast<std::size_t>(-1),
                  "Subcommand not present in this registry");
    // Extra parens: the comma in the template argument list would otherwise
    // read as a second macro argument to assert().
    assert((slotAt<Idx, Cmd>().has_value() &&
            "getSubOptions() on a subcommand that was not selected; "
            "check isActive<Cmd>() first"));
    return *slotAt<Idx, Cmd>();
  }
  template <auto *Cmd> const auto &getSubOptions() const {
    constexpr std::size_t Idx = index_of_v<Cmd, Infos...>;
    static_assert(Idx != static_cast<std::size_t>(-1),
                  "Subcommand not present in this registry");
    // Extra parens: the comma in the template argument list would otherwise
    // read as a second macro argument to assert().
    assert((slotAt<Idx, Cmd>().has_value() &&
            "getSubOptions() on a subcommand that was not selected; "
            "check isActive<Cmd>() first"));
    return *slotAt<Idx, Cmd>();
  }

  // RTTI-free type identity: each specialisation has a unique static.
  static const void *staticTypeKey() {
    static const char Key = 0;
    return &Key;
  }
  const void *typeKey() const override { return staticTypeKey(); }
};

//===----------------------------------------------------------------------===//
// detail — parser helpers (do not use directly)
//===----------------------------------------------------------------------===//

namespace detail {

/// Diagnostic context passed to parse callbacks during parsing.
/// Replaces reads of global CurrentProgramName so parsers are re-entrant.
struct ParseDiag {
  llvm::raw_ostream &Errs;
  llvm::StringRef ProgramName;
};

//===----------------------------------------------------------------------===//
// OptionStaticInfo — the per-descriptor half of an OptionEntry
//
// Every field below is a pure function of the constexpr descriptor, so it can
// be computed once at compile time and shared by every parse instead of being
// recomputed and stored per entry.  StaticInfoFor<Opt> is one such constant per
// descriptor, living in .rodata with no dynamic initialiser.
//
// STAGE 1: defined and verified, but not yet referenced by OptionEntry.
//===----------------------------------------------------------------------===//

/// Defined in CommandLineV2.cpp.  Holds the StringMap and prefix/sink lists
/// for a fixed set of registry entries; see CompiledParser.
class BakedNameIndex;

struct OptionStaticInfo {
  llvm::StringRef Name;
  llvm::StringRef Description;
  llvm::StringRef ValueDesc;
  /// Matches OptionEntry's default; Bits options never override it.
  llvm::StringRef DefaultValueName = "value";
  bool IsPositional = false;
  bool IsPrefix = false;
  bool IsAlwaysPrefix = false;
  bool IsPositionalEatsArgs = false;
  OptionNumOccurrencesFlag OccurrencesFlag = Optional;
  OptionValueExpected ValueExpected = ValueOptional;
  /// The descriptor's hidden flag.  The entry keeps its own copy because the
  /// hide/show filters rewrite it per parse.
  OptionHidden DefaultHidden = NotHidden;
  unsigned MiscFlagsBits = 0;
  /// Likewise: help's category folding rewrites the entry's copy per parse.
  const OptionCategory *DefaultCat = nullptr;
  bool SuppressValuePlaceholder = false;
  std::size_t NumEnumVals = 0;
  // NOTE: MaxEnumUsed and ShowDualDisplay are deliberately NOT here.  Both
  // require reading the *contents* of the EnumVals table, and not every
  // descriptor's table is constexpr — llvm-debuginfo-analyzer, for one,
  // declares its tables `extern const` with the definition in a .cpp, which
  // makes them unreadable in a constant expression.  Reading NumEnumVals and
  // the table's address is fine; reading its elements is not.  Those two
  // fields are computed on demand by computeEnumMetrics instead.
  /// For unnamed enum standalone entries: group header text printed before the
  /// first member.
  llvm::StringRef EnumGroupHeader;
  /// Override the sort key for this group (empty = use alphabetically-first
  /// member name).
  llvm::StringRef GroupSortKeyOverride;
  bool IsEnumGroupMember = false;
  /// True for the parser's own options (--help, --version, --print-options,
  /// ...).  Lets hideAllRegistered skip them without matching on names, a
  /// list that silently went stale when the --print-* builtins were added.
  bool IsBuiltin = false;
  const void *Desc = nullptr;
  void (*PrintEnumVal)(const void *, llvm::raw_ostream &, std::size_t,
                       std::size_t) = nullptr;
  /// See computeEnumMetrics.  Null when this option has no enum table.
  void (*EnumMetrics)(const void *, std::size_t &, bool &) = nullptr;
  bool (*ParseFn)(const void *, void *, unsigned, llvm::StringRef,
                  ParseDiag &) = nullptr;
  void (*DefaultFn)(const void *, void *) = nullptr;
  /// Renders the slot's current value for --print-all-options.  Null for
  /// builtins and for slots with no meaningful single-value rendering.
  void (*PrintValueFn)(const void *, const void *,
                       llvm::raw_ostream &) = nullptr;
  /// Checks a freshly-parsed value; false means rejected (the validator has
  /// already reported why through ParseDiag).  Null when unconstrained.
  bool (*ValidateFn)(const void *, const void *, llvm::StringRef,
                     ParseDiag &) = nullptr;
};

/// Runtime description of a single option, built from the compile-time
/// descriptor pack.  All type information is erased into callbacks so the
/// non-template runtime parser only sees this.
struct OptionEntry {
  // Fields are grouped by alignment — pointers, then StringRefs, then the
  // small scalars — so that the narrow members share one tail word instead of
  // each opening an 8-byte hole.  Interleaving them costs 16 bytes per entry.

  /// Points into ParsedOptions::Occurrences.  Every producer sets this, but
  /// it is initialised so a producer that forgets yields a deterministic
  /// crash rather than a wild pointer.
  unsigned *OccurrenceCount = nullptr;
  unsigned *LastPosition =
      nullptr; ///< Points into ParsedOptions::Positions; updated to the argv
               ///< index of the last occurrence.
  /// For list options: pointer to the per-element position vector held in the
  /// option's own ListStorage slot.  Null for scalar options.  The parser
  /// pushes the argv index of each successfully parsed element.
  std::vector<unsigned> *ElementPositions = nullptr;

  const OptionCategory *Cat = nullptr; ///< Category for grouped help output

  void *ParseSlot = nullptr; ///< T* or std::vector<T>*

  OptionHidden HiddenFlag;
  /// An entry-specific number ParseFn may interpret however it needs
  /// (standalone enum flags use it as the index of their enum value).
  unsigned ParseAux = 0;

  /// The compile-time half of this entry: everything derived purely from the
  /// option descriptor.  Every entry has one — registry options point at the
  /// constexpr StaticInfoFor<Opt>, and the sources with no descriptor NTTP
  /// (builtins, alias proxies, standalone enum flags, RuntimeOption) point at
  /// storage owned by the ParseFrame or by the RuntimeOption itself.  It is
  /// never null; the accessors below assert that.
  const OptionStaticInfo *Static = nullptr;

  const OptionStaticInfo &info() const {
    assert(Static && "OptionEntry has no static half");
    return *Static;
  }

  // Per-descriptor data, read through the shared static half.  Accessors
  // rather than fields, so descriptor-derived data exists once per option
  // rather than once per entry per parse.
  llvm::StringRef name() const { return info().Name; }
  llvm::StringRef description() const { return info().Description; }
  llvm::StringRef valueDesc() const { return info().ValueDesc; }
  llvm::StringRef defaultValueName() const { return info().DefaultValueName; }
  bool isPositional() const { return info().IsPositional; }
  bool isPrefix() const { return info().IsPrefix; }
  bool isAlwaysPrefix() const { return info().IsAlwaysPrefix; }
  bool isPositionalEatsArgs() const { return info().IsPositionalEatsArgs; }
  OptionNumOccurrencesFlag occurrencesFlag() const {
    return info().OccurrencesFlag;
  }
  OptionValueExpected valueExpected() const { return info().ValueExpected; }
  unsigned miscFlagsBits() const { return info().MiscFlagsBits; }
  bool suppressValuePlaceholder() const {
    return info().SuppressValuePlaceholder;
  }
  std::size_t numEnumVals() const { return info().NumEnumVals; }
  const void *parseDesc() const { return info().Desc; }
  llvm::StringRef enumGroupHeader() const { return info().EnumGroupHeader; }
  llvm::StringRef groupSortKeyOverride() const {
    return info().GroupSortKeyOverride;
  }
  bool isEnumGroupMember() const { return info().IsEnumGroupMember; }

  // Enum help metrics.  Not stored: see computeEnumMetrics for why they cannot
  // live in the constexpr static half, and note that both are read only by
  // help printing, so recomputing them there is free.
  std::size_t maxEnumUsed() const {
    if (!info().EnumMetrics)
      return 0;
    std::size_t MaxUsed = 0;
    bool Dual = false;
    info().EnumMetrics(info().Desc, MaxUsed, Dual);
    return MaxUsed;
  }
  bool showDualDisplay() const {
    if (!info().EnumMetrics)
      return false;
    std::size_t MaxUsed = 0;
    bool Dual = false;
    info().EnumMetrics(info().Desc, MaxUsed, Dual);
    return Dual;
  }

  /// Prints one enum value's help sub-line; null when not an enum option.
  void printEnumVal(llvm::raw_ostream &OS, std::size_t I,
                    std::size_t Width) const {
    info().PrintEnumVal(info().Desc, OS, I, Width);
  }
  bool hasEnumPrinter() const { return info().PrintEnumVal != nullptr; }

  bool parse(llvm::StringRef Val, ParseDiag &Diag) const {
    if (!info().ParseFn(info().Desc, ParseSlot, ParseAux, Val, Diag))
      return false;
    // Per occurrence, not once at end of parse: the validator sees each value
    // as it lands, so the diagnostic names the offending one even when the
    // option is repeated or comma-separated.
    if (info().ValidateFn)
      return info().ValidateFn(info().Desc, ParseSlot, name(), Diag);
    return true;
  }

  void applyDefault() const {
    if (info().DefaultFn)
      info().DefaultFn(info().Desc, ParseSlot);
  }
};

struct AliasEntry; // forward declaration for SubCommandSpec::Aliases

// ---------------------------------------------------------------------------
/// Runtime description of a subcommand, built by OptionsRegistry::buildEntries
/// for each SubCommandInfo pointer in the registry pack.
struct SubCommandSpec {
  llvm::StringRef Name;
  llvm::StringRef Desc;

  /// Called by runParser when this subcommand's name is matched.
  /// Initialises the optional slot and returns the option entries for the
  /// subcommand so the main parse loop can handle them alongside global opts.
  std::function<std::vector<OptionEntry>()> BuildAndInit;

  /// Aliases scoped to this subcommand (resolved after BuildAndInit merges
  /// subcommand entries with global entries).
  std::vector<AliasEntry> Aliases;
};

// ---------------------------------------------------------------------------
/// Alias name/target pair collected during the first pass of buildEntries.
/// Resolved into proxy OptionEntry objects by resolveAliases().
struct AliasEntry {
  llvm::StringRef Name;   ///< The alias name as typed on the command line
  llvm::StringRef Target; ///< CLIName of the option this alias forwards to
  const char *Desc = nullptr;
  OptionHidden HiddenFlag = NotHidden;
};

// ---------------------------------------------------------------------------
/// A library-local registry registered for automatic discovery at static-init
/// time.
///
/// This descriptor deliberately carries *factories* rather than a parsed-value
/// instance.  An earlier design allocated one ParsedOptions at registration
/// time and pointed every generated OptionEntry's ParseSlot at it, which meant
/// every parse in the process wrote the same slots — two concurrent parses
/// would interleave their writes.  Registering a factory instead lets each
/// parse allocate its own storage (owned by ParseFrame::DynamicStorages), so
/// nothing mutable is shared between parses.
///
/// The descriptor itself is immutable after registration, so the global
/// registration list is safe to read concurrently once static init is done.
struct DynamicRegistration {
  /// Registry address, used as the OptionsContext view key.
  const void *RegAddr = nullptr;
  /// Allocate a fresh ParsedOptions with Init defaults applied.
  std::unique_ptr<ParsedOptionsBase> (*MakeStorage)() = nullptr;
  /// Build OptionEntries pointing into \p Storage.
  void (*BuildInto)(ParsedOptionsBase &Storage, std::vector<OptionEntry> &,
                    std::vector<AliasEntry> &,
                    std::vector<SubCommandSpec> &) = nullptr;
  /// Hand ownership of a per-parse storage to an OptionsContext.
  void (*PublishInto)(OptionsContext &,
                      std::unique_ptr<ParsedOptionsBase>) = nullptr;
  /// Optional post-parse hook, given that parse'"'"'s values.  Used to write
  /// them back into legacy global variables; such globals stay process-wide, so
  /// an apply function that writes one is incompatible with running two jobs
  /// concurrently under different values for that option.  A hook that only
  /// reads (validating, say) carries no such hazard.
  std::function<void(const ParsedOptionsBase &)> Apply;
  /// Essential registrations are drained by every parser; the rest only when
  /// the parser opts in via OptionParser::enableGlobalDynamicEntries().
  bool Essential = false;
  /// Number of descriptors, so runParser can size the entry vector up front.
  std::size_t NumOptions = 0;
};

struct ParseFrame;

// ---------------------------------------------------------------------------
/// Everything the built-in options (--help, --version, ...) need in order to
/// run.  Stored once per parse in the ParseFrame, so each builtin's ParseFn can
/// reach it through OptionEntry::ParseDesc instead of every entry carrying its
/// own std::function.
struct BuiltinOptionState {
  llvm::StringRef Overview;
  llvm::StringRef ProgName;
  llvm::StringRef ExtraHelp;
  llvm::StringRef VersionString;
  llvm::raw_ostream *HelpOS = nullptr;
  /// Null means "exit on completion" (the one-shot CLI convention).
  llvm::raw_ostream *Errs = nullptr;
  ParseFrame *Frame = nullptr;
  std::function<void(llvm::raw_ostream &)> VersionPrinter;
  /// Set by --print-all-options / --print-options.  The dump happens after
  /// parsing finishes, since that is when the values are final.
  bool PrintAllOptions = false;
  bool PrintSpecifiedOptions = false;
};

// ---------------------------------------------------------------------------
/// Per-parse frame — holds all state that was formerly in file-scope statics.
/// Constructed on the stack by each parse call; threaded through internal
/// helpers so concurrent parses share nothing mutable.
/// Slots in ParseFrame::BuiltinOccurrences and BuiltinStatics.  Naming them
/// keeps two slots from silently sharing an index and keeps the count of
/// front-inserted builtins in step with the list itself.
enum BuiltinSlot : unsigned {
  BS_Help,
  BS_HelpHidden,
  BS_HelpList,
  BS_HelpListHidden,
  BS_Version,
  BS_H,
  /// Everything from here on is appended rather than inserted at the front.
  BS_PrintAllOptions,
  BS_PrintOptions,
  BS_Count,
};

struct ParseFrame {
  llvm::StringRef ProgramName;
  unsigned CurArgPosition = 0;
  std::vector<OptionEntry> *ActiveEntries = nullptr;
  std::string ActiveSubCommandName;
  llvm::ArrayRef<const OptionCategory *> AllowedCategories;
  bool HideUnrelated = false;
  /// Set when a builtin produced terminal output (--help, --help-list,
  /// --version, ...).  runParser then skips occurrence validation, and the
  /// parse entry points return nullptr under OnError::Return so the caller
  /// does not act on a result that was never meant to be used.
  bool HelpPrinted = false;
  bool HideAllRegistered = false;
  unsigned BuiltinOccurrences[BS_Count] = {};
  llvm::ArrayRef<llvm::StringRef> ShownNames;
  /// Name/description of every subcommand visible to this parse: the
  /// compile-time SubCommandSpecs passed to runParser, plus the process-wide
  /// runtime registrations.  Help printers read this instead of the global
  /// runtime registry, so printing help neither depends on nor mutates global
  /// state.  The StringRefs point at constexpr literals (compile-time
  /// subcommands) or at the runtime registry's strings, both of which outlive
  /// the parse.
  llvm::SmallVector<std::pair<llvm::StringRef, llvm::StringRef>, 8> Subcommands;
  /// Number of global entries before subcommand entries were merged.
  /// Entries at indices < GlobalEntryCount are top-level; those >= are
  /// subcommand-specific. Used to skip top-level positionals in subcommand
  /// help.
  std::size_t GlobalEntryCount = 0;
  /// Per-parse storage for every dynamically-registered registry drained by
  /// this parse, paired with that registration's *index* in the global
  /// registration list.  Owned here so that concurrent parses never share
  /// option slots.  runParser fills this; the parse entry points hand the
  /// storages to the OptionsContext afterwards via publishDynamicStorages().
  ///
  /// An index rather than a pointer: the registration list only ever grows, so
  /// indices stay valid, whereas a later push_back can reallocate it and
  /// invalidate any pointer we kept into it.
  std::vector<std::pair<std::size_t, std::unique_ptr<ParsedOptionsBase>>>
      DynamicStorages;
  /// Shared state for the built-in options; see BuiltinOptionState.
  BuiltinOptionState Builtins;
  /// One OptionStaticInfo per builtin.  Builtins have no descriptor, so theirs
  /// are filled in directly by buildBuiltinEntries.
  OptionStaticInfo BuiltinStatics[BS_Count];

  /// Name index over the registry-entry prefix, owned by a CompiledParser and
  /// shared by every parse that uses it.  Null for a plain OptionParser parse,
  /// which builds its index lazily instead.  Immutable, so concurrent parses
  /// read it without synchronisation.
  /// Resolved from OptionParser::setErrorHandling(), or from whether an error
  /// stream was supplied when the caller did not say.
  OnError OnErr = OnError::ExitProcess;

  const BakedNameIndex *Baked = nullptr;
  /// The baked block occupies [BakedFirst, BakedFirst + BakedCount).  It is
  /// not at index 0 because buildBuiltinEntries prepends --help and friends.
  std::size_t BakedFirst = 0;
  std::size_t BakedCount = 0;

  /// Entries in [DefaultedFirst, DefaultedFirst + DefaultedCount) whose
  /// storage was already default-initialised by ErasedRegistry::MakeStorage,
  /// so runParser's applyDefault() sweep can skip them.  Applying a default is
  /// an indirect call per option and was ~12% of a parse at `opt` scale.
  std::size_t DefaultedFirst = 0;
  std::size_t DefaultedCount = 0;
  /// Static info for alias proxies, which copy their target's but with a
  /// different name.  A deque so addresses stay stable as aliases resolve.
  std::deque<OptionStaticInfo> AliasStatics;
};

// ---------------------------------------------------------------------------
// Non-template parse helpers — implemented in CommandLineV2.cpp.
// ---------------------------------------------------------------------------

/// Prefix-format fallback: find an entry where E.IsPrefix and E.Name is a
/// prefix of \p ArgName.  Returns {entry, suffix} where suffix is the value
/// glued after the option name.  Returns {nullptr, {}} when not found.
LLVM_ABI std::pair<OptionEntry *, llvm::StringRef>
findPrefixEntry(std::vector<OptionEntry> &Entries, llvm::StringRef ArgName);

/// Run the parse over \p GlobalEntries.
///
/// Overview / HelpOS / VersionString / ExtraHelp / VersionPrinter are NOT
/// parameters: buildBuiltinEntries() must already have run, and it stores them
/// in ParseFrame::Builtins where the --help and --version builtins read them.
/// They used to be passed here as well and were simply unused.
LLVM_ABI bool runParser(std::vector<OptionEntry> &GlobalEntries,
                        std::vector<SubCommandSpec> &SubCommands, int argc,
                        const char *const *argv, llvm::raw_ostream *Errs,
                        ParseFrame &Frame, bool DrainDynamic = true);

/// Both help printers treat \p Entries as read-only: they copy it internally
/// before applying the hide-unrelated filter and category normalisation, so
/// printing help is idempotent and safe to interleave with parsing.
LLVM_ABI void printHelpList(const std::vector<OptionEntry> &Entries,
                            llvm::StringRef Overview, llvm::StringRef ProgName,
                            bool ShowHidden, llvm::raw_ostream &OS,
                            const ParseFrame &Frame);

LLVM_ABI void printHelp(const std::vector<OptionEntry> &Entries,
                        llvm::StringRef Overview, llvm::StringRef ProgName,
                        bool ShowHidden, llvm::raw_ostream &OS,
                        const ParseFrame &Frame,
                        llvm::StringRef ExtraHelp = {});

/// For each AliasEntry, find the target OptionEntry by name and push a proxy
/// entry with the alias name.  Reports unresolved aliases to \p Errs (or
/// llvm::errs() if null).
LLVM_ABI void resolveAliases(std::vector<OptionEntry> &Entries,
                             const std::vector<AliasEntry> &Aliases,
                             ParseFrame &Frame,
                             llvm::raw_ostream *Errs = nullptr);

/// Append globally-registered dynamic entries to the entry list.

/// Inject built-in option entries (help, help-hidden, help-list,
/// help-list-hidden, version) at the front of \p Entries with proper
/// parse actions.  These entries carry \c Cat = \c
/// GenericOptionsCategory so they appear under the "Generic Options" section of
/// help output. Since they live in the entry vector, \c AliasInfo{"h","help"}
/// resolves normally.
/// Returns the number of entries inserted *before* the pre-existing ones, so
/// a caller holding indices into that block (CompiledParser) can rebase them.
LLVM_ABI std::size_t
buildBuiltinEntries(std::vector<OptionEntry> &Entries, llvm::StringRef Overview,
                    llvm::StringRef ProgName, llvm::StringRef VersionString,
                    llvm::raw_ostream *HelpOS, llvm::StringRef ExtraHelp,
                    std::function<void(llvm::raw_ostream &)> VersionPrinter,
                    llvm::raw_ostream *Errs, ParseFrame &Frame);

LLVM_ABI bool parseBoolArg(llvm::StringRef OptName, llvm::StringRef Val,
                           bool &Out, ParseDiag &Diag);
LLVM_ABI bool parseIntArg(llvm::StringRef OptName, llvm::StringRef Val,
                          int &Out, ParseDiag &Diag);
LLVM_ABI bool parseUIntArg(llvm::StringRef OptName, llvm::StringRef Val,
                           unsigned &Out, ParseDiag &Diag);
LLVM_ABI bool parseInt64Arg(llvm::StringRef OptName, llvm::StringRef Val,
                            int64_t &Out, ParseDiag &Diag);
LLVM_ABI bool parseUInt64Arg(llvm::StringRef OptName, llvm::StringRef Val,
                             uint64_t &Out, ParseDiag &Diag);
LLVM_ABI bool parseFloatArg(llvm::StringRef OptName, llvm::StringRef Val,
                            float &Out, ParseDiag &Diag);
LLVM_ABI bool parseDoubleArg(llvm::StringRef OptName, llvm::StringRef Val,
                             double &Out, ParseDiag &Diag);
LLVM_ABI bool parseElementCountArg(llvm::StringRef OptName, llvm::StringRef Val,
                                   llvm::ElementCount &Out, ParseDiag &Diag);

// ---------------------------------------------------------------------------
// Direct-dispatch parse functions for OptionEntry::ParseFn.
// Each is a concrete function that extracts the typed Desc and Slot from
// void pointers and delegates to the corresponding parse*Arg helper.
// The CallbackFn (if present) is read from the Desc at runtime.
// ---------------------------------------------------------------------------

/// Look up \p Val in a descriptor's EnumVals table, honouring
/// MiscFlags::CaseInsensitiveValues.  Returns nullptr and emits the standard
/// diagnostic when there is no match.  Shared by every enum-valued parser so
/// the lookup and its error message exist in exactly one place.
template <typename T, typename DescT>
const EnumVal<T> *lookupEnumValue(const DescT *Desc, llvm::StringRef Val,
                                  ParseDiag &Diag) {
  const bool CaseInsensitive =
      (Desc->MiscFlagsBits & CaseInsensitiveValues) != 0;
  for (std::size_t I = 0; I < Desc->NumEnumVals; ++I) {
    llvm::StringRef Name = Desc->EnumVals[I].Name;
    if (CaseInsensitive ? Val.equals_insensitive(Name) : Val == Name)
      return &Desc->EnumVals[I];
  }
  if (!Diag.ProgramName.empty())
    Diag.Errs << Diag.ProgramName << ": ";
  Diag.Errs << "for the --" << Desc->CLIName
            << " option: Cannot find option named '" << Val << "'!\n";
  return nullptr;
}

/// Scalar parsers — one per supported value type.
template <typename T, typename DescT>
bool directParse(const void *D, void *S, unsigned, llvm::StringRef Val,
                 ParseDiag &Diag) {
  auto *Desc = static_cast<const DescT *>(D);
  auto &Slot = *static_cast<T *>(S);
  if constexpr (std::is_same_v<T, bool>) {
    if (!parseBoolArg(Desc->CLIName, Val, Slot, Diag))
      return false;
  } else if constexpr (std::is_same_v<T, std::string>) {
    Slot = Val.str();
  } else if constexpr (std::is_same_v<T, int>) {
    if (Desc->NumEnumVals > 0) {
      const EnumVal<int> *EV = lookupEnumValue<int>(Desc, Val, Diag);
      if (!EV)
        return false;
      Slot = static_cast<int>(EV->Value);
    } else if (!parseIntArg(Desc->CLIName, Val, Slot, Diag))
      return false;
  } else if constexpr (std::is_same_v<T, unsigned>) {
    if (!parseUIntArg(Desc->CLIName, Val, Slot, Diag))
      return false;
  } else if constexpr (std::is_same_v<T, int64_t>) {
    if (!parseInt64Arg(Desc->CLIName, Val, Slot, Diag))
      return false;
  } else if constexpr (std::is_same_v<T, uint64_t>) {
    if (!parseUInt64Arg(Desc->CLIName, Val, Slot, Diag))
      return false;
  } else if constexpr (std::is_same_v<T, float>) {
    if (!parseFloatArg(Desc->CLIName, Val, Slot, Diag))
      return false;
  } else if constexpr (std::is_same_v<T, double>) {
    if (!parseDoubleArg(Desc->CLIName, Val, Slot, Diag))
      return false;
  } else if constexpr (std::is_same_v<T, llvm::ElementCount>) {
    if (!parseElementCountArg(Desc->CLIName, Val, Slot, Diag))
      return false;
  } else if constexpr (std::is_same_v<T, cl::boolOrDefault>) {
    bool Tmp{};
    if (!parseBoolArg(Desc->CLIName, Val, Tmp, Diag))
      return false;
    Slot = Tmp ? cl::boolOrDefault::BOU_TRUE : cl::boolOrDefault::BOU_FALSE;
  } else if constexpr (std::is_same_v<T, std::optional<bool>>) {
    bool Tmp{};
    if (!parseBoolArg(Desc->CLIName, Val, Tmp, Diag))
      return false;
    Slot = Tmp;
  } else if constexpr (std::is_enum_v<T>) {
    if (Val.empty() && Desc->ValueExplicitlySet &&
        Desc->ValueExpected == ValueOptional) {
      for (std::size_t I = 0; I < Desc->NumEnumVals; ++I)
        if (Desc->EnumVals[I].Name[0] == '\0') {
          Slot = Desc->EnumVals[I].Value;
          break;
        }
    } else {
      const EnumVal<T> *EV = lookupEnumValue<T>(Desc, Val, Diag);
      if (!EV)
        return false;
      Slot = EV->Value;
    }
  } else {
    static_assert(sizeof(T) == 0,
                  "No value parser for this option type. Supported: bool, "
                  "std::optional<bool>, cl::boolOrDefault, int, unsigned, "
                  "int64_t, uint64_t, float, double, std::string, "
                  "llvm::ElementCount, and enum types.");
  }
  if (Desc->CallbackFn)
    Desc->CallbackFn(Slot);
  if (Desc->CtxCallbackFn && !Desc->CtxCallbackFn(Desc->CallbackCtx, Slot))
    return false;
  return true;
}

/// List parser — appends one element per occurrence, optionally calls callback.
template <typename T, typename DescT>
bool directParseList(const void *D, void *S, unsigned, llvm::StringRef Val,
                     ParseDiag &Diag) {
  auto *Desc = static_cast<const DescT *>(D);
  auto &Slot = *static_cast<std::vector<T> *>(S);
  if constexpr (std::is_same_v<T, std::string>) {
    Slot.push_back(Val.str());
  } else if constexpr (std::is_same_v<T, int>) {
    int Tmp{};
    if (!parseIntArg(Desc->CLIName, Val, Tmp, Diag))
      return false;
    Slot.push_back(Tmp);
  } else if constexpr (std::is_same_v<T, unsigned>) {
    unsigned Tmp{};
    if (!parseUIntArg(Desc->CLIName, Val, Tmp, Diag))
      return false;
    Slot.push_back(Tmp);
  } else if constexpr (std::is_same_v<T, bool>) {
    bool Tmp{};
    if (!parseBoolArg(Desc->CLIName, Val, Tmp, Diag))
      return false;
    Slot.push_back(Tmp);
  } else if constexpr (std::is_same_v<T, uint64_t>) {
    uint64_t Tmp{};
    if (!parseUInt64Arg(Desc->CLIName, Val, Tmp, Diag))
      return false;
    Slot.push_back(Tmp);
  } else if constexpr (std::is_enum_v<T>) {
    const EnumVal<T> *EV = lookupEnumValue<T>(Desc, Val, Diag);
    if (!EV)
      return false;
    Slot.push_back(EV->Value);
  } else {
    static_assert(sizeof(T) == 0,
                  "No list value parser for this option element type. "
                  "Supported: bool, int, unsigned, uint64_t, std::string, "
                  "and enum types.");
  }
  // Every branch above that reaches here has pushed exactly one element.
  if (Desc->CallbackFn)
    Desc->CallbackFn(Slot.back());
  if (Desc->CtxCallbackFn &&
      !Desc->CtxCallbackFn(Desc->CallbackCtx, Slot.back()))
    return false;
  return true;
}

/// Bits parser — ORs (1u << index) into the unsigned slot.
template <typename T, typename DescT>
bool directParseBits(const void *D, void *S, unsigned, llvm::StringRef Val,
                     ParseDiag &Diag) {
  auto *Desc = static_cast<const DescT *>(D);
  auto &Slot = *static_cast<unsigned *>(S);
  const EnumVal<T> *EV = lookupEnumValue<T>(Desc, Val, Diag);
  if (!EV)
    return false;
  Slot |= (1u << static_cast<unsigned>(EV - Desc->EnumVals));
  return true;
}

/// List default applier — clears the vector.
template <typename T> void directClearList(const void *, void *S) {
  static_cast<std::vector<T> *>(S)->clear();
}

/// Bits default applier — zeros the unsigned slot.
inline void directClearBits(const void *, void *S) {
  *static_cast<unsigned *>(S) = 0u;
}

/// True when `OS << T{}` compiles.  Slot types that cannot be streamed (the
/// subcommand and alias placeholders, say) print a marker instead of blocking
/// the whole dump.
template <typename T, typename = void>
inline constexpr bool HasStreamOp_v = false;
template <typename T>
inline constexpr bool
    HasStreamOp_v<T, std::void_t<decltype(std::declval<llvm::raw_ostream &>()
                                          << std::declval<const T &>())>> =
        true;

/// Value printer for --print-all-options / --print-options.  One instantiation
/// Reports a rejected option value as
/// "<prog>: for the --<opt> option: <msg>".
///
/// The message is composed by the caller rather than assembled here: some
/// options quote the whole value ("'150' value must be in the range [0, 100]!")
/// while others name the offending token inside a list, and only the validator
/// knows which.  Always returns false, for `return reject(...)`.
LLVM_ABI bool rejectOptionValue(llvm::StringRef OptName, const llvm::Twine &Msg,
                                ParseDiag &Diag);

/// Shared Validate body for options taking a regular expression.  Lives here
/// rather than in a .td body because that body is inlined into a header-level
/// constexpr descriptor and so cannot include Regex.h.
LLVM_ABI bool validateRegexOption(llvm::StringRef Pattern,
                                  llvm::StringRef OptName, ParseDiag &Diag);

/// Type-erased bridge to the descriptor's Validate modifier.  Instantiated per
/// (T, DescT) like the other direct* helpers.
template <typename T, typename DescT>
bool directValidate(const void *D, const void *S, llvm::StringRef Name,
                    ParseDiag &Diag) {
  auto *Desc = static_cast<const DescT *>(D);
  return Desc->ValidateFn(*static_cast<const T *>(S), Name, Diag);
}

/// List counterpart to directValidate.  directParseList appends before parse()
/// calls this, so the element under test is the last one.
template <typename T, typename DescT>
bool directValidateList(const void *D, const void *S, llvm::StringRef Name,
                        ParseDiag &Diag) {
  auto *Desc = static_cast<const DescT *>(D);
  const auto &Slot = *static_cast<const std::vector<T> *>(S);
  if (Slot.empty())
    return true;
  return Desc->ValidateFn(Slot.back(), Name, Diag);
}

/// per (T, DescT) like directParse and directApplyDefault, so a registry pays
/// per option *shape*, not per option.
template <typename T, typename DescT>
void directPrintValue(const void *D, const void *S, llvm::raw_ostream &OS) {
  const auto &Slot = *static_cast<const T *>(S);
  if constexpr (std::is_same_v<T, bool>) {
    OS << (Slot ? "true" : "false");
  } else if constexpr (std::is_enum_v<T>) {
    // Spell the enumerator, falling back to the raw value for
    // an enum whose descriptor carries no value table.
    auto *Desc = static_cast<const DescT *>(D);
    for (std::size_t I = 0; I < Desc->NumEnumVals; ++I)
      if (Desc->EnumVals[I].Value == Slot) {
        OS << Desc->EnumVals[I].Name;
        return;
      }
    OS << static_cast<long long>(Slot);
  } else if constexpr (HasStreamOp_v<T>) {
    OS << Slot;
  } else {
    OS << "*unprintable*";
  }
}

/// Default applier — applies the Init value from the descriptor.
template <typename T, typename DescT>
void directApplyDefault(const void *D, void *S) {
  auto *Desc = static_cast<const DescT *>(D);
  auto &Slot = *static_cast<T *>(S);
  if constexpr (std::is_same_v<T, std::string>) {
    if (Desc->HasDefault && Desc->DefaultValue)
      Slot = Desc->DefaultValue;
    else
      Slot.clear();
  } else {
    Slot = Desc->HasDefault ? static_cast<T>(Desc->DefaultValue) : T{};
  }
}

// ---------------------------------------------------------------------------
// Template helpers — instantiated per concrete descriptor/slot type pair.
// ---------------------------------------------------------------------------

/// Prints one enum value's help sub-line.  Referenced from OptionStaticInfo,
/// which is the single source for it, so no two copies can drift.
template <typename DescT>
void printEnumValueLine(const void *D, llvm::raw_ostream &OS, std::size_t I,
                        std::size_t CatMaxArgLen) {
  auto *Desc = static_cast<const DescT *>(D);
  llvm::StringRef EName = Desc->EnumVals[I].Name;
  bool HasDesc = Desc->EnumVals[I].Desc && *Desc->EnumVals[I].Desc;
  if (EName.empty() && !HasDesc)
    return;
  llvm::StringRef DisplayName = EName.empty() ? "<empty>" : EName;
  std::size_t Printed = 5 + DisplayName.size(); // "    =" + name
  OS << "    =" << DisplayName;
  if (HasDesc) {
    std::size_t Target = (CatMaxArgLen > 3) ? CatMaxArgLen - 3 : 0;
    if (Target > Printed)
      OS.indent(Target - Printed);
    OS << " -   ";
    llvm::StringRef D = Desc->EnumVals[I].Desc;
    bool First = true;
    while (!D.empty()) {
      auto [Line, Rest] = D.split('\n');
      if (!First)
        OS.indent(CatMaxArgLen + 2);
      OS << Line << "\n";
      D = Rest;
      First = false;
      if (Line.empty() && Rest.empty())
        break;
    }
    if (First)
      OS << "\n";
  } else {
    OS << "\n";
  }
}

/// Compute the two enum help metrics that cannot be stored in
/// OptionStaticInfo: the maximum "Used" column width across the enum values,
/// and whether this is a ValueOptional enum carrying an empty-name value.
///
/// Both need the *contents* of the EnumVals table, which is not
/// constexpr-readable for every descriptor (llvm-debuginfo-analyzer declares
/// its tables `extern const` with the definition in a .cpp).  Taking this
/// function's address is constexpr-legal even though calling it is not, so
/// OptionStaticInfo stores the pointer and help printing calls it on demand.
/// Both values are read only by help output, so recomputing costs nothing.
template <typename DescT>
void computeEnumMetrics(const void *D, std::size_t &MaxUsed,
                        bool &DualDisplay) {
  const auto *Desc = static_cast<const DescT *>(D);
  MaxUsed = 0;
  DualDisplay = false;
  if (!Desc->EnumVals || Desc->NumEnumVals == 0)
    return;
  bool HasEmptyName = false;
  for (std::size_t I = 0; I < Desc->NumEnumVals; ++I) {
    llvm::StringRef EName = Desc->EnumVals[I].Name;
    std::size_t W = EName.size() + 8;
    if (W > MaxUsed)
      MaxUsed = W;
    if (EName.empty())
      HasEmptyName = true;
  }
  if (HasEmptyName && Desc->ValueExpected == ValueOptional &&
      Desc->ValueExplicitlySet)
    DualDisplay = true;
}

/// Map a value type to its help-output placeholder name.
template <typename T> constexpr llvm::StringRef defaultValueName() {
  if constexpr (std::is_same_v<T, std::string>)
    return "string";
  else if constexpr (std::is_same_v<T, char>)
    return "char";
  else if constexpr (std::is_same_v<T, unsigned>)
    return "uint";
  else if constexpr (std::is_same_v<T, unsigned long> ||
                     std::is_same_v<T, unsigned long long>)
    return "ulong";
  else if constexpr (std::is_same_v<T, int>)
    return "int";
  else if constexpr (std::is_same_v<T, long> || std::is_same_v<T, long long>)
    return "long";
  else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>)
    return "number";
  else if constexpr (std::is_same_v<T, llvm::ElementCount>)
    return "ElementCount";
  else
    return "value";
}

/// Flatten a descriptor into its compile-time half.  Mirrors buildSingleEntry;
/// Stage 3 asserts the two agree field by field on the real option set.
template <typename DescT>
constexpr OptionStaticInfo makeStaticInfoFrom(const DescT *Opt) {
  using T = typename DescT::ValueT;
  constexpr bool IsList = std::is_same_v<DescT, ListOptionInfo<T>>;
  constexpr bool IsBits = std::is_same_v<DescT, BitsOptionInfo<T>>;

  OptionStaticInfo SI{};
  SI.Name = Opt->CLIName;
  SI.Description = Opt->CLIDescription;
  SI.ValueDesc =
      Opt->ValueDesc ? llvm::StringRef(Opt->ValueDesc) : llvm::StringRef("");
  SI.MiscFlagsBits = Opt->MiscFlagsBits;
  SI.DefaultHidden = Opt->OptionHiddenFlag;
  SI.DefaultCat = Opt->Category;
  SI.Desc = Opt;

  if constexpr (IsBits) {
    // Bits options are always a named, value-taking, repeatable flag.
    SI.IsPositional = false;
    SI.IsPrefix = false;
    SI.OccurrencesFlag = ZeroOrMore;
    SI.ValueExpected = ValueRequired;
    SI.ParseFn = directParseBits<T, DescT>;
    SI.DefaultFn = directClearBits;
    SI.PrintValueFn = directPrintValue<T, DescT>;
  } else {
    SI.IsPrefix = (Opt->FormattingFlag == PrefixFormat ||
                   Opt->FormattingFlag == AlwaysPrefixFormat);
    SI.IsAlwaysPrefix = (Opt->FormattingFlag == AlwaysPrefixFormat);
    SI.IsPositionalEatsArgs = (Opt->MiscFlagsBits & PositionalEatsArgs) != 0;
    SI.DefaultValueName = defaultValueName<T>();

    if constexpr (IsList) {
      SI.IsPositional =
          Opt->IsPositional || Opt->NumOccurrencesFlag == ConsumeAfter;
      SI.OccurrencesFlag = (Opt->NumOccurrencesFlag == Optional)
                               ? ZeroOrMore
                               : Opt->NumOccurrencesFlag;
      SI.ValueExpected =
          (Opt->ValueExpected == ValueOptional && !Opt->ValueExplicitlySet)
              ? ValueRequired
              : Opt->ValueExpected;
      if constexpr (std::is_same_v<T, bool>) {
        SI.ValueExpected = ValueOptional;
        SI.SuppressValuePlaceholder = true;
      }
      SI.ParseFn = directParseList<T, DescT>;
      SI.DefaultFn = directClearList<T>;
      // A list slot is a ListStorage<T>, not a single value; skip it.
      SI.PrintValueFn = nullptr;
      if (Opt->ValidateFn)
        SI.ValidateFn = directValidateList<T, DescT>;
    } else {
      SI.IsPositional = Opt->IsPositional;
      SI.OccurrencesFlag = Opt->NumOccurrencesFlag;
      if (Opt->ValueExplicitlySet) {
        SI.ValueExpected = Opt->ValueExpected;
      } else if constexpr (std::is_same_v<T, bool> ||
                           std::is_same_v<T, std::optional<bool>> ||
                           std::is_same_v<T, cl::boolOrDefault>) {
        SI.ValueExpected = ValueOptional;
        SI.SuppressValuePlaceholder = true;
      } else {
        SI.ValueExpected = ValueRequired;
      }
      SI.ParseFn = directParse<T, DescT>;
      SI.DefaultFn = directApplyDefault<T, DescT>;
      SI.PrintValueFn = directPrintValue<T, DescT>;
      if (Opt->ValidateFn)
        SI.ValidateFn = directValidate<T, DescT>;
    }
    if (Opt->NoValPlaceholder)
      SI.SuppressValuePlaceholder = true;
  }

  // Enum metadata, for the descriptor kinds that carry a table.
  if constexpr (IsBits || IsList || std::is_enum_v<T> ||
                std::is_same_v<T, int>) {
    SI.NumEnumVals = Opt->NumEnumVals;
    // Only the table's address is read here, never its elements.
    if (Opt->EnumVals && Opt->NumEnumVals > 0) {
      SI.PrintEnumVal = printEnumValueLine<DescT>;
      SI.EnumMetrics = computeEnumMetrics<DescT>;
    }
  }
  return SI;
}

/// One compile-time constant per descriptor.  Only usable where the descriptor
/// is a template argument; entries built from a runtime descriptor call
/// makeStaticInfoFrom directly and own the result (see RuntimeOption).
template <auto *Opt>
inline constexpr OptionStaticInfo StaticInfoFor = makeStaticInfoFrom(Opt);

/// Build one OptionEntry for a scalar OptionInfo<T>.
template <typename T>
OptionEntry buildSingleEntry(const OptionInfo<T> *Desc, T &Slot,
                             unsigned &Count, unsigned *Pos = nullptr,
                             std::vector<unsigned> * /*ElemPos*/ = nullptr) {
  // Everything derived purely from Desc now lives in the entry's
  // OptionStaticInfo, which the caller attaches.  Only the per-parse bindings
  // are set here; HiddenFlag and Cat are seeded from the descriptor because
  // the hide/show and category-folding filters rewrite them per parse.
  OptionEntry E;
  E.HiddenFlag = Desc->OptionHiddenFlag;
  E.OccurrenceCount = &Count;
  E.LastPosition = Pos;
  E.Cat = Desc->Category;
  E.ParseSlot = &Slot;
  return E;
}

/// Parse action for a standalone enum flag: Aux is the index of this flag's
/// value in the descriptor's EnumVals table.
template <typename T>
bool directParseStandaloneEnum(const void *D, void *S, unsigned Aux,
                               llvm::StringRef, ParseDiag &) {
  auto *Desc = static_cast<const OptionInfo<T> *>(D);
  auto &Slot = *static_cast<T *>(S);
  Slot = Desc->EnumVals[Aux].Value;
  if (Desc->CallbackFn)
    Desc->CallbackFn(Slot);
  return true;
}

/// One OptionStaticInfo per value of an unnamed enum option.  Built lazily at
/// runtime rather than as a constexpr array because it must read the EnumVals
/// table's *contents* (each flag takes its name from a value), and not every
/// descriptor's table is constexpr — see the note on OptionStaticInfo.
/// The function-local static gives stable addresses and thread-safe one-time
/// initialisation.
template <auto *Opt> const OptionStaticInfo *standaloneEnumStatics() {
  using DescT = std::remove_const_t<std::remove_pointer_t<decltype(Opt)>>;
  static const std::vector<OptionStaticInfo> V = [] {
    std::vector<OptionStaticInfo> Out;
    Out.reserve(Opt->NumEnumVals);
    for (std::size_t I = 0; I < Opt->NumEnumVals; ++I) {
      OptionStaticInfo SI = makeStaticInfoFrom(Opt);
      SI.Name = Opt->EnumVals[I].Name;
      SI.Description = Opt->EnumVals[I].Desc;
      SI.ValueDesc = "";
      SI.IsPositional = false;
      SI.IsPrefix = false;
      SI.IsAlwaysPrefix = false;
      SI.IsPositionalEatsArgs = false;
      SI.ValueExpected = ValueDisallowed; // standalone flag, no value
      SI.SuppressValuePlaceholder = true;
      // Each flag *is* one enum value, so it has no enum menu of its own.
      // Without this it would inherit the parent descriptor's table and help
      // would print every enum value beneath every standalone flag.
      SI.NumEnumVals = 0;
      SI.PrintEnumVal = nullptr;
      SI.EnumMetrics = nullptr;
      // Each flag is one member of the group named by the parent's
      // description; only the first member carries the header.
      SI.IsEnumGroupMember = true;
      if (I == 0 && Opt->CLIDescription && Opt->CLIDescription[0])
        SI.EnumGroupHeader = Opt->CLIDescription;
      SI.ParseFn = directParseStandaloneEnum<typename DescT::ValueT>;
      Out.push_back(SI);
    }
    return Out;
  }();
  return V.data();
}

/// Build standalone OptionEntry objects for an unnamed enum option.
/// When an enum option has an empty name, each enum value becomes a standalone
/// flag: -arm-restrict-it, -arm-default-it instead of --=arm-restrict-it.
template <typename T>
void buildStandaloneEnumEntries(const OptionInfo<T> *Desc, T &Slot,
                                unsigned &Count, unsigned *Pos,
                                std::vector<OptionEntry> &Entries) {
  static_assert(std::is_enum_v<T>, "Only valid for enum option types");
  for (std::size_t I = 0; I < Desc->NumEnumVals; ++I) {
    // The descriptor-derived half (including this flag's own name and
    // description, which come from EnumVals[I]) is in the OptionStaticInfo
    // that standaloneEnumStatics<Opt>() builds; the caller attaches it.
    OptionEntry E;
    E.HiddenFlag = Desc->OptionHiddenFlag;
    E.OccurrenceCount = &Count;
    E.LastPosition = Pos;
    E.Cat = Desc->Category;
    // Aux carries which enum value this particular flag selects.
    E.ParseAux = static_cast<unsigned>(I);
    E.ParseSlot = &Slot;
    Entries.push_back(std::move(E));
  }
}

/// Build one OptionEntry for a ListOptionInfo<T>.
template <typename T>
OptionEntry buildSingleEntry(const ListOptionInfo<T> *Desc,
                             std::vector<T> &Slot, unsigned &Count,
                             unsigned *Pos = nullptr,
                             std::vector<unsigned> *ElemPos = nullptr) {
  // See buildSingleEntry(const OptionInfo<T> *): the descriptor-derived half
  // is in OptionStaticInfo, attached by the caller.
  OptionEntry E;
  E.HiddenFlag = Desc->OptionHiddenFlag;
  E.OccurrenceCount = &Count;
  E.LastPosition = Pos;
  E.ElementPositions = ElemPos;
  E.Cat = Desc->Category;
  E.ParseSlot = &Slot;
  return E;
}

/// Build one OptionEntry for a BitsOptionInfo<T> (bitmask enum option).
/// Each parse occurrence ORs (1u << index) into the unsigned slot.
template <typename T>
OptionEntry buildSingleEntry(const BitsOptionInfo<T> *Desc, unsigned &Slot,
                             unsigned &Count, unsigned *Pos = nullptr,
                             std::vector<unsigned> * /*ElemPos*/ = nullptr) {
  // See buildSingleEntry(const OptionInfo<T> *): the descriptor-derived half
  // is in OptionStaticInfo, attached by the caller.
  OptionEntry E;
  E.HiddenFlag = Desc->OptionHiddenFlag;
  E.OccurrenceCount = &Count;
  E.LastPosition = Pos;
  E.Cat = Desc->Category;
  E.ParseSlot = &Slot;
  return E;
}

// ---------------------------------------------------------------------------
// Response file expansion — preprocessing step before runParser.
//
// Expands @file tokens in Argv in place using the platform-native tokenizer
// (TokenizeWindowsCommandLine on Windows, TokenizeGNUCommandLine elsewhere).
//
// Alloc must outlive all use of the pointers stored in Out.
// Returns true on success; on error, writes a message to *Errs (or to
// llvm::errs() + exits if Errs is null) and returns false.
// ---------------------------------------------------------------------------
LLVM_ABI bool expandArgs(OnError OnErr, int Argc, const char *const *Argv,
                         llvm::BumpPtrAllocator &Alloc,
                         llvm::SmallVectorImpl<const char *> &Out,
                         llvm::raw_ostream *Errs);

} // namespace detail

//===----------------------------------------------------------------------===//
// OptionsRegistry — template body
//
// buildEntries() is a private static of OptionsRegistry so it can access
// Opts... directly without needing a free-function deduction that would fail
// when unifying two separate auto*... packs.
//===----------------------------------------------------------------------===//

namespace detail {
struct POAccess {
  template <typename PO> static auto &values(PO &P) { return P.Values; }
  /// The storage slot at pack position \p I, whose descriptor is \p Info.
  template <std::size_t I, auto *Info, typename PO> static auto &slot(PO &P) {
    return P.template slotAt<I, Info>();
  }
  template <typename PO> static auto &occurrences(PO &P) {
    return P.Occurrences;
  }
  template <typename PO> static auto &positions(PO &P) { return P.Positions; }
};
} // namespace detail

//===----------------------------------------------------------------------===//
// Entry-building helpers
//
// These were static members of OptionsRegistry<Opts...>, but none of them use
// that pack -- they were members only for access to ParsedOptions internals.
// As members, every instantiation carried the enclosing registry's full option
// pack in its mangled name, which is a large amount of symbol-table text.  As
// free templates they mangle against their own arguments only.
//===----------------------------------------------------------------------===//

namespace detail {

// Forward declarations: these helpers call one another, which was implicit
// while they were members (a class body is a complete-class context).
template <auto *Opt> void maybeCollectAlias(std::vector<AliasEntry> &Aliases);
template <auto *...SubOpts>
void collectSubAliases(const SubCommandInfo<SubOpts...> *,
                       std::vector<AliasEntry> &Aliases);
template <auto *Opt, std::size_t I, typename SubPO>
void buildOneSubEntry(SubPO &Sub, std::vector<OptionEntry> &Entries);
template <auto *...SubOpts, std::size_t... Is>
void buildSubEntries(ParsedOptions<SubOpts...> &Sub,
                     std::vector<OptionEntry> &Entries,
                     std::index_sequence<Is...>);
/// Convenience overload: the sequence length is the sub-option count, which
/// the descriptor pack already carries.  (It used to be recovered with
/// std::tuple_size_v on the storage; ParsedOptions no longer stores a tuple.)
template <auto *...SubOpts>
void buildSubEntries(ParsedOptions<SubOpts...> &Sub,
                     std::vector<OptionEntry> &Entries);

template <typename T> void applyOneDefault(const OptionInfo<T> *Desc, T &Slot) {
  if (Desc->HasDefault) {
    if constexpr (std::is_same_v<typename OptionInfo<T>::DefaultT,
                                 const char *>)
      Slot = T(Desc->DefaultValue);
    else
      Slot = static_cast<T>(Desc->DefaultValue);
  }
}

template <typename T>
void applyOneDefault(const ListOptionInfo<T> *, ListStorage<T> &) {}

template <typename T>
void applyOneDefault(const BitsOptionInfo<T> *, unsigned &) {}

template <typename DescT, typename SlotT,
          std::enable_if_t<IsAliasInfo_v<DescT> || IsSubCommandInfo_v<DescT>,
                           int> = 0>
void applyOneDefault(const DescT *, SlotT &) {}

/// Build the entry for one ordinary (scalar or list) option.
///
/// Templated on the descriptor and slot *types* rather than on the descriptor
/// itself, so the overload resolution over buildSingleEntry happens once per
/// option shape instead of once per option.  A 285-option registry has only a
/// handful of distinct shapes, and that is about 10% of its registration TU.
template <typename DescT, typename SlotT>
void addPlainEntry(const DescT *P, const OptionStaticInfo *Static, SlotT &Slot,
                   unsigned &Count, unsigned &Pos,
                   std::vector<OptionEntry> &GlobalEntries) {
  if constexpr (IsListOptionInfo_v<DescT>)
    GlobalEntries.push_back(
        buildSingleEntry(P, Slot.Values, Count, &Pos, &Slot.Positions));
  else
    GlobalEntries.push_back(buildSingleEntry(P, Slot, Count, &Pos));
  GlobalEntries.back().Static = Static;
}

// Overload for scalar / list option descriptors (not a SubCommandInfo or
// AliasInfo).
template <
    auto *Opt, typename SlotT,
    typename DescT = std::remove_const_t<std::remove_pointer_t<decltype(Opt)>>,
    std::enable_if_t<!IsSubCommandInfo_v<DescT> && !IsAliasInfo_v<DescT>, int> =
        0>
void addOneEntry(SlotT &Slot, unsigned &Count, unsigned &Pos,
                 std::vector<detail::OptionEntry> &GlobalEntries,
                 std::vector<detail::AliasEntry> &,
                 std::vector<detail::SubCommandSpec> &) {
  constexpr auto *P = Opt;
  // Unnamed enum options: when an enum OptionInfo has an empty name,
  // register each enum value as a standalone flag.
  if constexpr (std::is_enum_v<SlotT>) {
    using InnerDescT = std::remove_const_t<DescT>;
    if constexpr (std::is_same_v<InnerDescT, OptionInfo<SlotT>>) {
      if (P->CLIName[0] == '\0' && P->NumEnumVals > 0) {
        std::size_t First = GlobalEntries.size();
        detail::buildStandaloneEnumEntries(P, Slot, Count, &Pos, GlobalEntries);
        // Each standalone flag gets the static info for its own enum value.
        const detail::OptionStaticInfo *S =
            detail::standaloneEnumStatics<Opt>();
        for (std::size_t I = First; I < GlobalEntries.size(); ++I) {
          GlobalEntries[I].Static = &S[I - First];
        }
        return;
      }
    }
  }
  detail::addPlainEntry(P, &detail::StaticInfoFor<Opt>, Slot, Count, Pos,
                        GlobalEntries);
}

// Overload for AliasInfo — collect name/target pair for later resolution.
template <
    auto *Opt, typename SlotT,
    typename DescT = std::remove_const_t<std::remove_pointer_t<decltype(Opt)>>,
    std::enable_if_t<IsAliasInfo_v<DescT>, int> = 0>
void addOneEntry(SlotT & /*Slot*/, unsigned & /*Count*/, unsigned & /*Pos*/,
                 std::vector<detail::OptionEntry> & /*GlobalEntries*/,
                 std::vector<detail::AliasEntry> &AliasEntries,
                 std::vector<detail::SubCommandSpec> & /*SubSpecs*/) {
  AliasEntries.push_back(
      {Opt->CLIName, Opt->AliasFor, Opt->Desc, Opt->HiddenFlag});
}

// Overload for SubCommandInfo (identified via the SubCommandTag base).
template <
    auto *Opt, typename SlotT,
    typename DescT = std::remove_const_t<std::remove_pointer_t<decltype(Opt)>>,
    std::enable_if_t<IsSubCommandInfo_v<DescT>, int> = 0>
void addOneEntry(SlotT &Slot, unsigned & /*Count*/, unsigned & /*Pos*/,
                 std::vector<detail::OptionEntry> & /*GlobalEntries*/,
                 std::vector<detail::AliasEntry> & /*AliasEntries*/,
                 std::vector<detail::SubCommandSpec> &SubSpecs) {
  constexpr auto *P = Opt;
  detail::SubCommandSpec Spec;
  Spec.Name = P->Name;
  Spec.Desc = P->Desc;
  Spec.BuildAndInit = [&Slot]() -> std::vector<detail::OptionEntry> {
    Slot.emplace();
    auto &Sub = *Slot;
    std::vector<detail::OptionEntry> Entries;
    buildSubEntries(Sub, Entries);
    for (auto &E : Entries)
      E.applyDefault();
    return Entries;
  };
  collectSubAliases(P, Spec.Aliases);
  SubSpecs.push_back(std::move(Spec));
}

// Extract AliasInfo entries from a SubCommandInfo's pack into a vector.
template <auto *...SubOpts>
void collectSubAliases(const SubCommandInfo<SubOpts...> *,
                       std::vector<detail::AliasEntry> &Aliases) {
  (maybeCollectAlias<SubOpts>(Aliases), ...);
}

template <auto *Opt>
void maybeCollectAlias(std::vector<detail::AliasEntry> &Aliases) {
  using DescT = std::remove_const_t<std::remove_pointer_t<decltype(Opt)>>;
  if constexpr (IsAliasInfo_v<DescT>)
    Aliases.push_back({Opt->CLIName, Opt->AliasFor});
}

// Build one subcommand entry unless it's an AliasInfo (aliases are handled
// separately via collectSubAliases).
template <auto *Opt, std::size_t I, typename SubPO>
void buildOneSubEntry(SubPO &Sub, std::vector<detail::OptionEntry> &Entries) {
  using DescT = std::remove_const_t<std::remove_pointer_t<decltype(Opt)>>;
  if constexpr (!IsAliasInfo_v<DescT>) {
    addPlainEntry(Opt, &detail::StaticInfoFor<Opt>, POAccess::slot<I, Opt>(Sub),
                  POAccess::occurrences(Sub)[I], POAccess::positions(Sub)[I],
                  Entries);
  }
}

// Populate Entries from a ParsedOptions<SubOpts...>.  AliasInfo entries are
// skipped (they carry no storage).  SubOpts and Is are expanded together so
// each call gets its descriptor as an NTTP, which is what lets
// buildOneSubEntry name StaticInfoFor<Opt> — subcommand entries need a
// static half just as much as top-level ones, since aliases inside a
// subcommand copy it to build their proxies.
template <auto *...SubOpts, std::size_t... Is>
void buildSubEntries(ParsedOptions<SubOpts...> &Sub,
                     std::vector<detail::OptionEntry> &Entries,
                     std::index_sequence<Is...>) {
  static_assert(sizeof...(SubOpts) == sizeof...(Is),
                "descriptor pack and index sequence must agree");
  (buildOneSubEntry<SubOpts, Is>(Sub, Entries), ...);
}

template <auto *...SubOpts>
void buildSubEntries(ParsedOptions<SubOpts...> &Sub,
                     std::vector<detail::OptionEntry> &Entries) {
  buildSubEntries(Sub, Entries, std::make_index_sequence<sizeof...(SubOpts)>{});
}

} // namespace detail

template <auto *...Opts> class OptionsRegistry {
public:
  constexpr OptionsRegistry() = default;
  explicit constexpr OptionsRegistry(llvm::StringRef ExtraHelp)
      : ExtraHelp_(ExtraHelp) {}

  using ParsedOptionsT = ParsedOptions<Opts...>;

  static constexpr std::size_t size() { return sizeof...(Opts); }

  /// Returns true if this registry contains the given option descriptor.
  template <auto *Opt> static constexpr bool containsOpt() {
    return detail::indexOfImpl<Opt, Opts...>() != static_cast<std::size_t>(-1);
  }

  /// Populate \p GlobalEntries, \p AliasEntries, and \p SubSpecs from this
  /// registry using \p Result as storage for slot references.  Called by
  /// OptionParser to build a unified entry list across multiple registries.
  void buildInto(ParsedOptionsT &Result,
                 std::vector<detail::OptionEntry> &GlobalEntries,
                 std::vector<detail::AliasEntry> &AliasEntries,
                 std::vector<detail::SubCommandSpec> &SubSpecs) const {
    buildEntries(Result, GlobalEntries, AliasEntries, SubSpecs,
                 std::make_index_sequence<sizeof...(Opts)>{});
  }

  /// Return a formatted help string for all visible options.
  /// Pass ShowHidden=true to include Hidden options.
  std::string helpText(bool ShowHidden = false) const {
    ParsedOptionsT Result;
    std::vector<detail::OptionEntry> GlobalEntries;
    std::vector<detail::AliasEntry> AliasEntries;
    std::vector<detail::SubCommandSpec> SubSpecs;
    GlobalEntries.reserve(sizeof...(Opts));
    buildEntries(Result, GlobalEntries, AliasEntries, SubSpecs,
                 std::make_index_sequence<sizeof...(Opts)>{});
    // The frame owns the alias proxies' static info, so it must outlive both
    // alias resolution and printing.
    detail::ParseFrame Frame;
    detail::resolveAliases(GlobalEntries, AliasEntries, Frame);
    std::string Buf;
    llvm::raw_string_ostream OS(Buf);
    detail::printHelp(GlobalEntries, /*Overview=*/"", /*ProgName=*/"",
                      ShowHidden, OS, Frame, ExtraHelp_);
    return Buf;
  }

  /// Create a ParsedOptionsT with all Init defaults applied.
  ParsedOptionsT makeDefaults() const {
    ParsedOptionsT Storage;
    applyDefaultsToStorage(&Storage,
                           std::make_index_sequence<sizeof...(Opts)>{});
    return Storage;
  }

  /// Apply defaults to an externally-owned ParsedOptions.
  static void applyDefaultsTo(ParsedOptionsT &Storage) {
    applyDefaultsToStorage(&Storage,
                           std::make_index_sequence<sizeof...(Opts)>{});
  }

  /// True when every option that names *this* registry in OptionRegistryOf
  /// also agrees with its position in this pack.
  ///
  /// Only the option's declared registry is checked.  A descriptor may
  /// legitimately appear in more than one registry -- MCSchedule.cpp builds a
  /// private one-option registry from a descriptor declared in MCOptsReg --
  /// and its emitted index refers to the declared one, so requiring agreement
  /// everywhere would reject correct code.  That is also why the read path
  /// only trusts the index when the registry matches (hasPackIndexIn).
  template <const auto *Self> static constexpr bool packIndicesAgree() {
    return packIndicesAgreeImpl<Self>(
        std::make_index_sequence<sizeof...(Opts)>{});
  }

  /// Build OptionEntries into external vectors from an externally-owned
  /// ParsedOptions. Used by ErasedRegistry for type-erased entry building.
  static void staticBuildInto(ParsedOptionsT &Storage,
                              std::vector<detail::OptionEntry> &Entries,
                              std::vector<detail::AliasEntry> &Aliases,
                              std::vector<detail::SubCommandSpec> &SubSpecs) {
    buildEntries(Storage, Entries, Aliases, SubSpecs,
                 std::make_index_sequence<sizeof...(Opts)>{});
  }

private:
  llvm::StringRef ExtraHelp_;

  template <const auto *Self, std::size_t... Is>
  static constexpr bool packIndicesAgreeImpl(std::index_sequence<Is...>) {
    return (... &&
            (!hasPackIndexIn<Self, Opts>() || packIndexOrNPos<Opts>() == Is));
  }

  template <std::size_t... Is>
  static void applyDefaultsToStorage(ParsedOptionsT *Storage,
                                     std::index_sequence<Is...>) {
    // Opts and Is are expanded together (legal: same length), as in
    // buildEntries below.  Selecting the descriptor with
    // std::get<Is>(std::make_tuple(Opts...)) instead instantiates an
    // N-element tuple once per option -- O(N^2) template work per registry,
    // for no benefit: the descriptor is already available as a pack element.
    (detail::applyOneDefault(Opts, Storage->template slotAt<Is, Opts>()), ...);
  }

  // Pair each pointer in Opts... with its ParsedOptions slot using the index
  // sequence.  Dispatches via overloaded addOneEntry helpers so that
  // if constexpr does not need to suppress type-checking across branches
  // (which would fail when the condition is non-dependent in C++17).
  template <std::size_t... Is>
  static void buildEntries(ParsedOptionsT &Result,
                           std::vector<detail::OptionEntry> &GlobalEntries,
                           std::vector<detail::AliasEntry> &AliasEntries,
                           std::vector<detail::SubCommandSpec> &SubSpecs,
                           std::index_sequence<Is...>) {
    // Opts and Is are expanded together (legal: same length), so each call
    // gets its descriptor as a template argument rather than a runtime tuple
    // element.  That is what lets addOneEntry name StaticInfoFor<Opt>, which
    // needs the descriptor as an NTTP.
    ((detail::addOneEntry<Opts>(Result.template slotAt<Is, Opts>(),
                                Result.Occurrences[Is], Result.Positions[Is],
                                GlobalEntries, AliasEntries, SubSpecs)),
     ...);
  }
};

namespace detail {

template <auto *Opt, const auto *FirstReg, const auto *...RestRegs>
constexpr std::size_t findRegIndexForOpt() {
  if constexpr (std::remove_pointer_t<decltype(FirstReg)>::template containsOpt<
                    Opt>())
    return 0;
  else if constexpr (sizeof...(RestRegs) > 0)
    return 1 + findRegIndexForOpt<Opt, RestRegs...>();
  else
    return static_cast<std::size_t>(-1);
}

} // namespace detail

//===----------------------------------------------------------------------===//
// ErasedRegistry — type-erased registry for runtime composition
//===----------------------------------------------------------------------===//

/// Type-erased handle to an OptionsRegistry. Carries function pointers for
/// creating storage, building OptionEntries, and calling the bridge function.
/// No global mutable state — values live in per-parse storages.
struct ErasedRegistry {
  const void *RegAddr;
  std::unique_ptr<ParsedOptionsBase> (*MakeStorage)();
  void (*BuildInto)(ParsedOptionsBase &, std::vector<detail::OptionEntry> &,
                    std::vector<detail::AliasEntry> &,
                    std::vector<detail::SubCommandSpec> &);
  void (*Bridge)(const ParsedOptionsBase &);
  /// Deep-copy a storage, so views handed to an OptionsContext remain
  /// copyable via OptionsContext::copyViewsFrom().
  void *(*Clone)(const void *);
  /// Delete a storage through its concrete type.
  void (*Destroy)(const void *);
  /// Number of descriptors in the registry, so callers can size the entry
  /// vector up front instead of growing it one push_back at a time.
  std::size_t NumOptions;
};

/// Test a function-pointer template argument for null.
///
/// Comparing such an argument against nullptr directly makes GCC emit
/// -Waddress ("the address of F will never be NULL") at every instantiation
/// that supplies a real function, because at the point of comparison it sees a
/// named function.  Passing the pointer through a parameter keeps the test a
/// constant expression without the false positive.
namespace detail {
template <typename FnT> constexpr bool isNullFnPtr(FnT F) {
  return F == nullptr;
}
} // namespace detail

/// Create an ErasedRegistry from a compile-time OptionsRegistry pointer.
///
/// Deliberately split from the BridgeFn overload below.  A defaulted
/// `void (*BridgeFn)(const ParsedOptionsT &) = nullptr` template parameter
/// mangles that entire function-pointer type -- and with it the registry's full
/// option pack -- into *every* instantiation, even the ones passing nullptr.
/// Most registrations have no bridge, so that is a large amount of symbol name
/// for nothing.
template <const auto *Reg> ErasedRegistry eraseRegistry() {
  using RegT = std::remove_pointer_t<decltype(Reg)>;
  // A TableGen-emitted index that disagreed with the real pack order would
  // silently read a different option.  Checked here rather than at each read:
  // this is the one place that has both the registry address and its pack, and
  // every registry handed to an OptionParser passes through it.
  static_assert(RegT::template packIndicesAgree<Reg>(),
                "emitted option index disagrees with registry pack order");
  using PO = typename RegT::ParsedOptionsT;
  ErasedRegistry R;
  R.RegAddr = static_cast<const void *>(Reg);
  R.NumOptions = RegT::size();
  R.MakeStorage = +[]() -> std::unique_ptr<ParsedOptionsBase> {
    auto S = std::make_unique<PO>();
    RegT::applyDefaultsTo(*S);
    return S;
  };
  R.BuildInto =
      +[](ParsedOptionsBase &Base, std::vector<detail::OptionEntry> &E,
          std::vector<detail::AliasEntry> &A,
          std::vector<detail::SubCommandSpec> &S) {
        RegT::staticBuildInto(static_cast<PO &>(Base), E, A, S);
      };
  R.Clone = +[](const void *P) -> void * {
    return new PO(static_cast<const PO *>(P)->clone());
  };
  R.Destroy = +[](const void *P) { delete static_cast<const PO *>(P); };
  R.Bridge = nullptr;
  return R;
}

/// As above, but with a bridge function called after parsing to apply side
/// effects (e.g. setting legacy globals).  Only instantiated where a bridge is
/// actually supplied, so the long function-pointer type is confined to those.
template <const auto *Reg,
          void (*BridgeFn)(const typename std::remove_pointer_t<
                           decltype(Reg)>::ParsedOptionsT &)>
ErasedRegistry eraseRegistryWithBridge() {
  using PO = typename std::remove_pointer_t<decltype(Reg)>::ParsedOptionsT;
  ErasedRegistry R = eraseRegistry<Reg>();
  R.Bridge = +[](const ParsedOptionsBase &Base) {
    BridgeFn(static_cast<const PO &>(Base));
  };
  return R;
}

/// Register an ErasedRegistry for automatic discovery during parsing.
/// Called at static-init time by library/target Options.cpp files.

/// Register a dynamically-built option into a global list that
/// OptionParser::parse() drains automatically. Prefer P.addDynamicEntry()
/// when an OptionParser is available; this global form exists only for
/// static-init sites that have no parser in scope (e.g. test binaries).
LLVM_ABI void registerDynamicEntry(detail::OptionEntry E);
LLVM_ABI void registerEssentialDynamicEntry(detail::OptionEntry E);
LLVM_ABI void registerDynamicPostParseCallback(std::function<void()> Cb);
LLVM_ABI void
registerEssentialDynamicPostParseCallback(std::function<void()> Cb);

namespace detail {
/// Hand each per-parse dynamic storage collected in \p Frame to \p Ctx,
/// transferring ownership.  Called by the parse entry points once the context
/// exists.  Leaves Frame.DynamicStorages empty.
LLVM_ABI void publishDynamicStorages(ParseFrame &Frame, OptionsContext &Ctx);
} // namespace detail

/// An option whose name and description are only known at runtime — e.g. one
/// flag per entry in a plugin/pass/backend registry.
///
/// `OptionInfo` stores `const char *`, so the strings must be owned somewhere
/// stable; this owns them alongside the descriptor and the parse destination.
/// The parser keeps a pointer to the descriptor, so a RuntimeOption must
/// outlive the parse and must not move — hold it in a container with stable
/// addresses (std::deque, or a vector of unique_ptr), not a plain std::vector.
///
/// Prefer a constexpr `OptionInfo` whenever the name is a literal; this exists
/// only for the genuinely dynamic cases.
/// Build a parse entry for a compile-time descriptor.
///
/// Takes the descriptor as a template argument rather than a pointer so it can
/// attach StaticInfoFor<Opt> — the entry's compile-time half, which every
/// entry must have.  Extra arguments are forwarded to buildSingleEntry (slot,
/// occurrence counter, and optionally the position/element-position slots).
///
/// Use this rather than detail::buildSingleEntry, which cannot name the static
/// info because it only ever sees a runtime pointer.  For options whose name
/// or description is only known at runtime, use RuntimeOption instead.
template <auto *Opt, typename... Args>
detail::OptionEntry makeEntry(Args &&...As) {
  detail::OptionEntry E =
      detail::buildSingleEntry(Opt, std::forward<Args>(As)...);
  E.Static = &detail::StaticInfoFor<Opt>;
  return E;
}

template <typename T> class RuntimeOption {
  std::string NameStr;
  std::string DescStr;
  std::optional<OptionInfo<T>> Info;
  /// This option's compile-time half, computed at runtime because the
  /// descriptor is.  Owned here so it outlives the parse.
  detail::OptionStaticInfo StaticInfo;
  T Slot{};
  unsigned Count = 0;

public:
  /// Extra \p Args are forwarded to the OptionInfo constructor, so the usual
  /// modifiers (ValueDisallowed, ZeroOrMore, CtxCallback, ...) all work.
  template <typename... Args>
  RuntimeOption(llvm::StringRef Name, llvm::StringRef Desc, Args &&...As)
      : NameStr(Name.str()), DescStr(Desc.str()) {
    Info.emplace(NameStr.c_str(), DescStr.c_str(), std::forward<Args>(As)...);
    StaticInfo = detail::makeStaticInfoFrom(&*Info);
  }

  // Addresses of the members are handed to the parser; copying or moving would
  // leave it pointing at the wrong object.
  RuntimeOption(const RuntimeOption &) = delete;
  RuntimeOption &operator=(const RuntimeOption &) = delete;

  /// Build the parse entry, using this option's own occurrence counter.
  detail::OptionEntry makeEntry() {
    detail::OptionEntry E = detail::buildSingleEntry(&*Info, Slot, Count);
    E.Static = &StaticInfo;
    return E;
  }
  /// Build the parse entry sharing \p SharedCount with sibling options, so that
  /// a single occurrence across the group satisfies OneOrMore.
  detail::OptionEntry makeEntry(unsigned &SharedCount) {
    detail::OptionEntry E = detail::buildSingleEntry(&*Info, Slot, SharedCount);
    E.Static = &StaticInfo;
    return E;
  }

  /// Mutable access for the help-only fields that have no descriptor spelling
  /// (enum group headers).  Callers set these on the entry today; once those
  /// fields move into OptionStaticInfo they set them here instead.
  detail::OptionStaticInfo &staticInfo() { return StaticInfo; }

  const T &value() const { return Slot; }
  unsigned occurrences() const { return Count; }
};

/// Register a DynamicRegistration for automatic discovery during parsing.
LLVM_ABI void registerDynamicRegistration(detail::DynamicRegistration R);

/// Build a DynamicRegistration for \p Reg.  Exposed so both the essential and
/// non-essential entry points share one implementation.
template <const auto *Reg>
detail::DynamicRegistration makeDynamicRegistration(
    std::function<void(
        const typename std::remove_pointer_t<decltype(Reg)>::ParsedOptionsT &)>
        ApplyFn,
    bool Essential) {
  using RegT = std::remove_pointer_t<decltype(Reg)>;
  using ParsedT = typename RegT::ParsedOptionsT;

  detail::DynamicRegistration R;
  R.RegAddr = static_cast<const void *>(Reg);
  R.Essential = Essential;
  R.NumOptions = RegT::size();
  R.MakeStorage = +[]() -> std::unique_ptr<ParsedOptionsBase> {
    auto S = std::make_unique<ParsedT>();
    RegT::applyDefaultsTo(*S);
    return S;
  };
  R.BuildInto =
      +[](ParsedOptionsBase &Base, std::vector<detail::OptionEntry> &E,
          std::vector<detail::AliasEntry> &A,
          std::vector<detail::SubCommandSpec> &S) {
        RegT::staticBuildInto(static_cast<ParsedT &>(Base), E, A, S);
      };
  R.PublishInto =
      +[](OptionsContext &Ctx, std::unique_ptr<ParsedOptionsBase> Storage) {
        // Skip when a composed/static parser already contributed this registry;
        // that view holds the values the parser actually wrote.
        if (Ctx.hasRaw(static_cast<const void *>(Reg)))
          return;
        Ctx.addRawView(
            static_cast<const void *>(Reg), Storage.release(),
            +[](const void *P) { delete static_cast<const ParsedT *>(P); },
            +[](const void *P) -> void * {
              return new ParsedT(static_cast<const ParsedT *>(P)->clone());
            });
      };
  if (ApplyFn)
    R.Apply = [ApplyFn](const ParsedOptionsBase &Base) {
      ApplyFn(static_cast<const ParsedT &>(Base));
    };
  return R;
}

/// Register all options from an OptionsRegistry for automatic discovery, so a
/// tool picks up library-local options it did not add to its OptionParser.
///
/// Only a factory is recorded: each parse allocates its own ParsedOptions, so
/// concurrent parses observe independent values.
///
/// If ApplyFn is provided, it is called after parsing with that parse's values
/// to write them back into legacy global variables.  Note that such globals
/// remain process-wide — an apply function is inherently incompatible with
/// running two jobs concurrently under different values for that option.
///
/// Usage at static-init time:
///   static const int Reg = [] {
///     clv2::registerDynamicRegistry<&MyOptsReg>(applyMyOpts);
///     return 0;
///   }();
///
/// That idiom is a global constructor, which descriptors are otherwise
/// designed to avoid: they are constexpr and live in .rodata.  It is the price
/// of registering a registry whose address is only known at link time, so keep
/// it to one per library rather than one per option, and prefer adding the
/// registry to an OptionParser directly where the tool can name it.
template <const auto *Reg>
void registerDynamicRegistry(
    std::function<void(
        const typename std::remove_pointer_t<decltype(Reg)>::ParsedOptionsT &)>
        ApplyFn = nullptr) {
  registerDynamicRegistration(
      makeDynamicRegistration<Reg>(std::move(ApplyFn), /*Essential=*/false));
}

/// Like registerDynamicRegistry but registers into the "essential" list
/// drained by ALL parsers. Use for options that every tool needs (e.g.
/// --color).
template <const auto *Reg>
void registerEssentialDynamicRegistry(
    std::function<void(
        const typename std::remove_pointer_t<decltype(Reg)>::ParsedOptionsT &)>
        ApplyFn = nullptr) {
  registerDynamicRegistration(
      makeDynamicRegistration<Reg>(std::move(ApplyFn), /*Essential=*/true));
}

class CompiledParser;

/// Instance-scoped option parser. Holds registries, dynamic entries, and
/// hide-filter state. parse() uses only instance state + a stack-local
/// ParseFrame, touching no global mutable state. Two OptionParser instances
/// may parse concurrently on different threads.
class LLVM_ABI OptionParser {
public:
  OptionParser() = default;
  OptionParser(OptionParser &&) = default;
  OptionParser &operator=(OptionParser &&) = default;
  OptionParser(const OptionParser &) = delete;
  OptionParser &operator=(const OptionParser &) = delete;

  /// Add a statically-known registry with an optional bridge function.
  template <const auto *Reg> OptionParser &add() {
    Registries.push_back(eraseRegistry<Reg>());
    return *this;
  }

  /// Overload taking a bridge function; see eraseRegistryWithBridge.
  template <const auto *Reg,
            void (*BridgeFn)(const typename std::remove_pointer_t<
                             decltype(Reg)>::ParsedOptionsT &)>
  OptionParser &add() {
    Registries.push_back(eraseRegistryWithBridge<Reg, BridgeFn>());
    return *this;
  }

  /// Add an already-erased registry (e.g. from a plugin).
  OptionParser &addErased(ErasedRegistry R) {
    Registries.push_back(std::move(R));
    return *this;
  }

  /// Add a dynamically-built option entry (names known only at runtime).
  OptionParser &addDynamicEntry(detail::OptionEntry E) {
    assert(E.Static && "entry has no static half; build it with "
                       "clv2::makeEntry<&Opt>() or RuntimeOption::makeEntry()");
    DynamicEntries.push_back(std::move(E));
    return *this;
  }

  /// Choose what a failed parse does.  Unset means the historical behaviour:
  /// terminate when no error stream was supplied, return otherwise.
  OptionParser &setErrorHandling(OnError E) {
    ErrorPolicy = E;
    return *this;
  }

  /// Freeze this configuration into a CompiledParser, indexing the registry
  /// options once.  Worth it when the same option set is parsed repeatedly --
  /// especially from several threads; see CompiledParser.
  CompiledParser compile() const;

  /// Print the same help --help would, without parsing.  For tools that detect
  /// a usage error after the parse and want to show usage alongside it.
  ///
  /// Category filters set by hideUnrelatedOptions()/hideAllDynamicEntries()
  /// apply, so the output matches what the user would see from --help.
  LLVM_ABI void printHelp(llvm::raw_ostream &OS, llvm::StringRef Overview = {},
                          llvm::StringRef ProgName = {},
                          bool ShowHidden = false) const;

  /// Mark all options whose category is NOT in Cats as ReallyHidden.
  void hideUnrelatedOptions(llvm::ArrayRef<const OptionCategory *> Cats) {
    AllowedCategories.assign(Cats.begin(), Cats.end());
    HideUnrelated = true;
  }

  /// Enable draining globally-registered dynamic entries during parse.
  void enableGlobalDynamicEntries() { DrainGlobalDynamic = true; }

  /// Mark all registered (non-builtin) options as Hidden. Used by
  /// RegisterCommonLLVMOptionsHidden so tools can selectively reveal
  /// only the options they need via showOptions().
  void hideAllDynamicEntries() { HideAllRegistered = true; }

  /// Mark specific options as Hidden by name. Applied during parse.
  void hideOptions(std::initializer_list<llvm::StringRef> Names) {
    HiddenNames.insert(HiddenNames.end(), Names.begin(), Names.end());
  }

  /// Mark specific Hidden options as visible (NotHidden) by name.
  void showOptions(std::initializer_list<llvm::StringRef> Names) {
    ShownNames.insert(ShownNames.end(), Names.begin(), Names.end());
  }

  /// Set extra help text appended after option listing.
  void setExtraHelp(llvm::StringRef Text) { ExtraHelp_ = Text.str(); }

  /// Parse argv against all registered registries. Returns an OptionsContext.
  /// Touches no global mutable state — concurrent parses are safe.
  std::unique_ptr<OptionsContext>
  parse(int argc, const char *const *argv, llvm::StringRef Overview = {},
        llvm::raw_ostream *Errs = nullptr, llvm::StringRef VersionString = {},
        llvm::raw_ostream *HelpOS = nullptr,
        std::function<void(llvm::raw_ostream &)> VersionPrinter = {});

private:
  std::vector<ErasedRegistry> Registries;
  std::vector<detail::OptionEntry> DynamicEntries;
  std::vector<std::unique_ptr<ParsedOptionsBase>> Storages;
  llvm::SmallVector<const OptionCategory *, 8> AllowedCategories;
  std::string ExtraHelp_;
  llvm::SmallVector<llvm::StringRef, 8> HiddenNames;
  llvm::SmallVector<llvm::StringRef, 8> ShownNames;
  bool HideUnrelated = false;
  bool DrainGlobalDynamic = false;
  bool HideAllRegistered = false;
  std::optional<OnError> ErrorPolicy;

  friend class CompiledParser;
};

//===----------------------------------------------------------------------===//
// CompiledParser
//
// An OptionParser whose configuration is frozen.  Freezing lets the parser do
// once what a plain OptionParser redoes on every parse: index the registry
// options by name.  A lazily-indexed parse also pays for linear scans before
// it decides the index is worth building; a compiled parser removes both.
//
// parse() is const and touches no member state, so ONE CompiledParser may be
// shared by any number of threads parsing different command lines
// concurrently.  That is the point: it makes running the same tool in-process,
// in parallel, with different options cheap.
//
//   clv2::OptionParser P;              // configure
//   P.add<&MyOptsReg>();
//   const clv2::CompiledParser CP = P.compile();   // freeze once
//   parallelFor([&](int I) {                       // share across threads
//     auto Ctx = CP.parse(Argc[I], Argv[I], "tool");
//   });
//
// Prefer OptionParser::parse() for a one-shot tool: it indexes lazily and so
// avoids the up-front build for short command lines.
//===----------------------------------------------------------------------===//

class LLVM_ABI CompiledParser {
public:
  CompiledParser(CompiledParser &&);
  CompiledParser &operator=(CompiledParser &&);
  CompiledParser(const CompiledParser &) = delete;
  CompiledParser &operator=(const CompiledParser &) = delete;
  ~CompiledParser();

  /// Parse \p argv.  Safe to call concurrently on one CompiledParser *when
  /// isShareable()* — which is the common case, and the point of compiling.
  ///
  /// It is not safe when the parser carries dynamic entries (addDynamicEntry),
  /// which point at caller-owned storage, or a registry with a bridge
  /// function, which writes into process-wide variables.  compile() records
  /// that in isShareable() rather than rejecting such a parser: it is still
  /// perfectly usable, just from one thread at a time.  Assert builds detect
  /// the misuse; release builds do not.
  std::unique_ptr<OptionsContext>
  parse(int argc, const char *const *argv, llvm::StringRef Overview = {},
        llvm::raw_ostream *Errs = nullptr, llvm::StringRef VersionString = {},
        llvm::raw_ostream *HelpOS = nullptr,
        std::function<void(llvm::raw_ostream &)> VersionPrinter = {}) const;

  /// False when concurrent parses would race: either the parser carries
  /// dynamic entries whose value slots are caller-owned, or one of its
  /// registries has a bridge function, which writes parsed values into
  /// process-wide variables.  Such a parser is still usable, just not from
  /// more than one thread at a time.
  bool isShareable() const { return Shareable; }

  /// Number of registry options covered by the baked index.
  std::size_t indexedOptions() const { return BakedCount; }

private:
  friend class OptionParser;
  CompiledParser();

  std::vector<ErasedRegistry> Registries;
  std::vector<detail::OptionEntry> DynamicEntries;
  llvm::SmallVector<const OptionCategory *, 8> AllowedCategories;
  std::string ExtraHelp_;
  llvm::SmallVector<llvm::StringRef, 8> HiddenNames;
  llvm::SmallVector<llvm::StringRef, 8> ShownNames;
  bool HideUnrelated = false;
  bool DrainGlobalDynamic = false;
  bool HideAllRegistered = false;
  bool Shareable = true;
#ifndef NDEBUG
  /// Concurrent parses in flight, for the shareability assert in parse().
  /// Mutable because parse() is const by design.  Not carried across a move:
  /// std::atomic is immovable, and moving a parser mid-parse is not a thing
  /// callers can legitimately do, so a fresh counter is correct.
  struct InFlightCounter {
    std::atomic<unsigned> N{0};
    InFlightCounter() = default;
    InFlightCounter(InFlightCounter &&) noexcept {}
    InFlightCounter &operator=(InFlightCounter &&) noexcept { return *this; }
  };
  mutable InFlightCounter ParsesInFlight;
#endif
  std::optional<OnError> ErrorPolicy;

  /// Built once over the registry-entry prefix. shared_ptr so the incomplete
  /// type is fine here: the deleter is bound where it is created, in the .cpp.
  std::shared_ptr<const detail::BakedNameIndex> Baked;
  std::size_t BakedCount = 0;
};
//===----------------------------------------------------------------------===//
// Convenience factory for enum OptionInfo
//
// Because EnumVal tables must have static storage duration, the canonical
// pattern is:
//
//   inline constexpr clv2::EnumVal<MyEnum> MyVals[] = {
//       {"a", MyEnum::A, "First"}, {"b", MyEnum::B, "Second"}};
//
//   inline constexpr auto MyOpt =
//       clv2::makeEnumOption<MyEnum>("my-opt", "desc", MyVals,
//                                    clv2::Init{MyEnum::A});
//
//===----------------------------------------------------------------------===//

template <typename EnumT, std::size_t N, typename... Args>
constexpr OptionInfo<EnumT> makeEnumOption(const char *Name, const char *Desc,
                                           const EnumVal<EnumT> (&Vals)[N],
                                           Args &&...args) {
  return OptionInfo<EnumT>(Name, Desc, ValuesRef<EnumT>(Vals),
                           std::forward<Args>(args)...);
}

template <typename EnumT, std::size_t N, typename... Args>
constexpr ListOptionInfo<EnumT>
makeEnumListOption(const char *Name, const char *Desc,
                   const EnumVal<EnumT> (&Vals)[N], Args &&...args) {
  return ListOptionInfo<EnumT>(Name, Desc, ValuesRef<EnumT>(Vals),
                               std::forward<Args>(args)...);
}

/// A runtime-registered subcommand entry.
/// Carries the subcommand name, description, and the full set of options
/// scoped to that subcommand.
struct RuntimeSubCommandEntry {
  std::string Name;
  std::string Desc;
  /// Options scoped to this subcommand.
  std::vector<detail::OptionEntry> Options;
  /// Owned storage for option name/description strings referenced by Options.
  std::vector<std::string> OwnedStrings;
  // NOTE: this deliberately carries no "activated" flag.  Which subcommand was
  // selected is a property of a single parse, not of the process-wide
  // registry; ask the parse's OptionsContext via getActiveSubCommand().
};

/// Register a runtime subcommand directly.
LLVM_ABI void registerRuntimeSubcommand(RuntimeSubCommandEntry E);

/// Return a read-only view of all currently registered runtime subcommands.
/// Snapshot of the registered runtime subcommands.  Returns pointers rather
/// than a range over the container: the registry is a deque appended under a
/// mutex, so the pointees stay valid, but there is no contiguous buffer to
/// hand back an ArrayRef over.
LLVM_ABI std::vector<const RuntimeSubCommandEntry *> getRuntimeSubcommands();

} // namespace clv2
} // namespace llvm

#endif // LLVM_SUPPORT_COMMANDLINEV2_H
