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
//     // Assemble a registry. The pointer NTTPs encode membership in the type.
//     inline constexpr clv2::OptionsRegistry<&Verbose, &Quiet> MyRegistry;
//   }
//
//   // In main() (or a per-job entry point):
//   clv2::OptionParser P;
//   P.add<&MyLib::MyRegistry>();
//   auto Ctx = P.parse(argc, argv, "My tool");
//
//   // View into the registry.
//   const auto *Opts = Ctx->getViewPtr<&MyLib::MyRegistry>();
//
//   // Access the Verbose option.
//   int V = Opts->get<&MyLib::Verbose>();
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

namespace llvm::clv2 {

enum OptionNumOccurrencesFlag {
  Optional = 0x00,     ///< Zero or one occurrence
  ZeroOrMore = 0x01,   ///< Zero or more occurrences
  Required = 0x02,     ///< Exactly one occurrence required
  OneOrMore = 0x03,    ///< One or more occurrences required
  ConsumeAfter = 0x04, ///< Consume all args after last required positional
};

/// What a parse does when it fails, or after it prints --help/--version.
enum class OnError {
  ExitProcess, ///< std::exit(): the default for a standalone tool
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

/// Formatting modifiers.
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

/// Default-value modifier. Example: Init{42}, Init{true}, Init{"hello"}.
template <typename T> struct Init {
  T Value;
  constexpr explicit Init(T V) : Value(V) {}
};
template <typename T> Init(T) -> Init<T>;

namespace detail {
struct ParseDiag; // defined below; validators report through it.
} // namespace detail

/// Post-parse value validation. \p Fn is called once per occurrence, right
/// after the value is parsed into the slot, and returns false to reject it.
///
/// Rejections travel back through ParseDiag like any other parse error, so
/// they honour the parser's OnError policy.
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

/// Category association modifier.
struct OptionCategory;
struct CatTag {
  const OptionCategory *Cat;
};

/// Value-description modifier. Controls the placeholder shown in help output.
/// Example: value_desc("filename") -> -o=<filename> instead of -o=<value>.
struct ValDesc {
  llvm::StringRef Text;
};
constexpr ValDesc value_desc(llvm::StringRef S) { return {S}; }

/// Suppress the value placeholder in help output.
/// Use for options that are technically ValueOptional but should display as
/// flags.
struct SuppressValPlaceholder {};
constexpr SuppressValPlaceholder suppress_val_placeholder{};

/// Marks this option as one member of a menu of independent flags that
/// together express one logical choice (e.g. "-execute"/"-printline"/...).
/// --help groups consecutive members under one shared header instead of
/// repeating it, and sorts the group as a unit (see printHelp's
/// GroupSortKey in CommandLineV2.cpp). Set Header on exactly the first
/// member of the group -- only the first is read.
struct EnumGroup {
  llvm::StringRef Header = "";
};

/// Per-value callback modifier. Invoked after each successful parse.
/// Must be a plain function pointer so the descriptor remains constexpr.
template <typename T> struct Callback {
  void (*Fn)(const T &);
  constexpr explicit Callback(void (*F)(const T &)) : Fn(F) {}
};
template <typename T> Callback(void (*)(const T &)) -> Callback<T>;

/// Per-value callback carrying a caller-supplied context, for cases where the
/// action needs state a plain function pointer cannot reach. The context is a
/// runtime pointer, so a descriptor using this generally cannot be constexpr.
/// It is meant for runtime-constructed OptionInfo objects. Invoked after
/// Callback, if both are present.
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

/// Category tag for grouping options in help output.
struct OptionCategory {
  llvm::StringRef Name;
  llvm::StringRef Desc;
  constexpr OptionCategory(llvm::StringRef N, llvm::StringRef D = "")
      : Name(N), Desc(D) {}
};

/// OptionCategory factory function.
constexpr CatTag Cat(const OptionCategory &C) { return {&C}; }

/// The built-in category for help and version options. Entries assigned to
/// this category are injected automatically by the parser and printed under
/// the "Generic Options" section of --help output.
/// Category for the --color option defined in SupportOptionsOptInfos.inc.
/// include SupportOptionsOptInfos.h to get it.
LLVM_ABI extern const OptionCategory GenericOptionsCategory;

/// Enum value table entry. Each entry is a name/value pair, with an optional
/// description for help output.
template <typename EnumT> struct EnumVal {
  llvm::StringRef Name;
  EnumT Value;
  llvm::StringRef Desc = "";
};

/// A tag carrying a pointer and size to a static EnumVal array.
/// Arrays must have static storage duration (e.g. inline constexpr []).
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

template <typename T> struct DescriptorDefault {
  using Type = T;
};
template <> struct DescriptorDefault<std::string> {
  using Type = llvm::StringRef;
};

namespace detail {

// Forward declarations of the type-erased helpers each descriptor
// constructor below wires up into its own ParseFn/DefaultFn/etc. Defined
// later in this namespace, once DescT (the descriptor itself) is complete.
template <typename T, typename DescT>
bool directParse(const void *D, void *S, unsigned, llvm::StringRef Val,
                 ParseDiag &Diag);
template <typename T, typename DescT>
bool directParseList(const void *D, void *S, unsigned, llvm::StringRef Val,
                     ParseDiag &Diag);
template <typename T, typename DescT>
bool directParseBits(const void *D, void *S, unsigned, llvm::StringRef Val,
                     ParseDiag &Diag);
template <typename T> void directClearList(const void *, void *S);
void directClearBits(const void *, void *S);
template <typename T, typename DescT>
bool directValidate(const void *D, const void *S, llvm::StringRef Name,
                    ParseDiag &Diag);
template <typename T, typename DescT>
bool directValidateList(const void *D, const void *S, llvm::StringRef Name,
                        ParseDiag &Diag);
template <typename T, typename DescT>
void directPrintValue(const void *D, const void *S, llvm::raw_ostream &OS);
template <typename T, typename DescT>
void directApplyDefault(const void *D, void *S);
template <typename T> constexpr llvm::StringRef defaultValueName();
template <typename DescT>
void printEnumValueLine(const void *D, llvm::raw_ostream &OS, std::size_t I,
                        std::size_t CatMaxArgLen);
template <typename DescT>
void computeEnumMetrics(const void *D, std::size_t &MaxUsed, bool &DualDisplay);

/// Common static information about an option descriptor, shared by all
/// descriptor types.
struct OptionStaticInfo {
  llvm::StringRef Name;
  llvm::StringRef Description;
  llvm::StringRef ValueDesc;
  llvm::StringRef DefaultValueName = "value";
  bool IsPositional = false;
  bool IsPrefix = false;
  bool IsAlwaysPrefix = false;
  bool IsPositionalEatsArgs = false;
  OptionNumOccurrencesFlag OccurrencesFlag = Optional;
  OptionValueExpected ValueExpected = ValueOptional;
  OptionHidden DefaultHidden = NotHidden;
  unsigned MiscFlagsBits = 0;
  const OptionCategory *DefaultCat = nullptr;
  bool SuppressValuePlaceholder = false;
  std::size_t NumEnumVals = 0;
  llvm::StringRef EnumGroupHeader;
  /// Override the sort key for this group (empty = use alphabetically-first
  /// member name).
  llvm::StringRef GroupSortKeyOverride;
  bool IsEnumGroupMember = false;
  /// True for the parser's own options (--help, --version, --print-options,
  /// ...). Lets hideAllRegistered skip them without matching on names, a
  /// list that silently went stale when the --print-* builtins were added.
  bool IsBuiltin = false;
  /// The pointer to pass as `D` to ParseFn/ValidateFn/DefaultFn/PrintEnumVal/
  /// EnumMetrics. Null unless this OptionStaticInfo's own address is *not*
  /// the right value to pass -- i.e. for aliases (whose static info is a
  /// separate copy but whose inherited ParseFn expects the alias target's
  /// descriptor) and standalone-enum flags (whose static info is one of N
  /// independent copies but whose ParseFn expects the parent descriptor).
  const void *Desc = nullptr;
  void (*PrintEnumVal)(const void *, llvm::raw_ostream &, std::size_t,
                       std::size_t) = nullptr;
  /// See computeEnumMetrics. Null when this option has no enum table.
  void (*EnumMetrics)(const void *, std::size_t &, bool &) = nullptr;
  bool (*ParseFn)(const void *, void *, unsigned, llvm::StringRef,
                  ParseDiag &) = nullptr;
  void (*DefaultFn)(const void *, void *) = nullptr;
  /// Renders the slot's current value for --print-all-options. Null for
  /// builtins and for slots with no meaningful single-value rendering.
  void (*PrintValueFn)(const void *, const void *,
                       llvm::raw_ostream &) = nullptr;
  /// Checks a freshly-parsed value; false means rejected (the validator has
  /// already reported why through ParseDiag). Null when unconstrained.
  bool (*ValidateFn)(const void *, const void *, llvm::StringRef,
                     ParseDiag &) = nullptr;

protected:
  /// Sets the fields shared verbatim by every descriptor kind into this object,
  /// the descriptor's own inherited OptionStaticInfo base subobject.
  template <typename DescT> constexpr void finalizeCommon() {
    auto *Opt = static_cast<DescT *>(this);
    Opt->DefaultHidden = Opt->OptionHiddenFlag;
    Opt->DefaultCat = Opt->Category;
  }

  /// Resolves the enum-value-table metadata shared by every descriptor kind
  /// that carries a table.
  template <typename DescT> constexpr void finalizeEnumMetadata() {
    auto *Opt = static_cast<DescT *>(this);
    Opt->NumEnumVals = Opt->RawNumEnumVals;
    // Only the table's address is read here, never its elements.
    if (Opt->EnumVals && Opt->RawNumEnumVals > 0) {
      Opt->PrintEnumVal = printEnumValueLine<DescT>;
      Opt->EnumMetrics = computeEnumMetrics<DescT>;
    }
  }
};

/// The pointer to pass as `D` to SI's type-erased function pointers: SI.Desc
/// when explicitly set, otherwise SI's own address.
constexpr const void *effectiveDesc(const OptionStaticInfo &SI) {
  return SI.Desc ? SI.Desc : static_cast<const void *>(&SI);
}

} // namespace detail

// Base for all option descriptor types. Holds the common metadata fields
// and the modifier argument handling.
template <typename ValueType>
class CommonOptionInfo : public detail::OptionStaticInfo {
public:
  bool RawIsPositional = false;
  OptionNumOccurrencesFlag NumOccurrencesFlag = Optional;
  OptionValueExpected RawValueExpected = ValueOptional;
  bool ValueExplicitlySet = false; ///< True when ValueExpected was set by user
  bool NoValPlaceholder = false;   ///< Suppress value placeholder in help
  OptionHidden OptionHiddenFlag = NotHidden;
  FormattingFlags FormattingFlag = NormalFormat;
  const OptionCategory *Category = nullptr;

  const EnumVal<ValueType> *EnumVals = nullptr;
  std::size_t RawNumEnumVals = 0;

  bool (*TypedValidateFn)(const ValueType &, llvm::StringRef,
                          detail::ParseDiag &) = nullptr;
  void (*CallbackFn)(const ValueType &) = nullptr;
  bool (*CtxCallbackFn)(void *, const ValueType &) = nullptr;
  void *CallbackCtx = nullptr;

protected:
  // Fallback: fires a compile-time error for unrecognised modifier types.
  // sizeof(ArgT) == 0 is value-dependent so the assert only fires on
  // instantiation, not on template definition (avoiding NDR in C++17).
  template <typename ArgT> constexpr void HandleArg(ArgT) {
    static_assert(sizeof(ArgT) == 0,
                  "Unexpected argument type for option modifier");
  }

  constexpr void HandleArg(Positional) {
    RawIsPositional = true;
    FormattingFlag = NormalFormat;
  }
  constexpr void HandleArg(OptionNumOccurrencesFlag F) {
    NumOccurrencesFlag = F;
  }
  constexpr void HandleArg(OptionValueExpected V) {
    RawValueExpected = V;
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
  constexpr void HandleArg(ValuesRef<ValueType> V) {
    EnumVals = V.Vals;
    RawNumEnumVals = V.NumVals;
  }
  constexpr void HandleArg(Callback<ValueType> C) { CallbackFn = C.Fn; }
  constexpr void HandleArg(CtxCallback<ValueType> C) {
    CtxCallbackFn = C.Fn;
    CallbackCtx = C.Ctx;
  }
  constexpr void HandleArg(Validate<ValueType> V) { TypedValidateFn = V.Fn; }
  constexpr void HandleArg(EnumGroup G) {
    IsEnumGroupMember = true;
    if (!G.Header.empty())
      EnumGroupHeader = G.Header;
  }

  constexpr CommonOptionInfo(llvm::StringRef Name, llvm::StringRef Desc) {
    this->Name = Name;
    this->Description = Desc;
  }
};

/// Scalar option descriptor.
template <typename ValueType>
class OptionInfo : public CommonOptionInfo<ValueType> {
  using Base = CommonOptionInfo<ValueType>;

public:
  using ValueT = ValueType;
  using DefaultT = typename DescriptorDefault<ValueType>::Type;

  DefaultT DefaultValue{};
  bool HasDefault = false;

protected:
  using Base::HandleArg;

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
  // cannot appear in a constexpr OptionInfo. Without this overload the
  // failure surfaces as the generic "Unexpected argument type" fallback,
  // which points at the wrong thing entirely.
  template <typename U = ValueType>
  constexpr void HandleArg(const Init<std::string> &) {
    static_assert(sizeof(U) == 0,
                  "Init{std::string(...)} cannot be used in a constexpr "
                  "OptionInfo as std::string is not a literal type in C++17. "
                  "Use a string literal instead, e.g. Init{\"text\"}.");
  }

public:
  template <typename... Args>
  constexpr OptionInfo(llvm::StringRef Name, llvm::StringRef Desc,
                       Args &&...args)
      : Base(Name, Desc) {
    (HandleArg(args), ...);
    this->template finalizeCommon<OptionInfo>();
    this->IsPrefix = (this->FormattingFlag == PrefixFormat ||
                      this->FormattingFlag == AlwaysPrefixFormat);
    this->IsAlwaysPrefix = (this->FormattingFlag == AlwaysPrefixFormat);
    this->IsPositionalEatsArgs =
        (this->MiscFlagsBits & PositionalEatsArgs) != 0;
    this->DefaultValueName = detail::defaultValueName<ValueType>();
    this->IsPositional = this->RawIsPositional;
    this->OccurrencesFlag = this->NumOccurrencesFlag;
    if (this->ValueExplicitlySet) {
      this->ValueExpected = this->RawValueExpected;
    } else if constexpr (std::is_same_v<ValueType, bool> ||
                         std::is_same_v<ValueType, std::optional<bool>> ||
                         std::is_same_v<ValueType, cl::boolOrDefault>) {
      this->ValueExpected = ValueOptional;
      this->SuppressValuePlaceholder = true;
    } else {
      this->ValueExpected = ValueRequired;
    }
    this->ParseFn = detail::directParse<ValueType, OptionInfo>;
    this->DefaultFn = detail::directApplyDefault<ValueType, OptionInfo>;
    this->PrintValueFn = detail::directPrintValue<ValueType, OptionInfo>;
    if (this->TypedValidateFn)
      this->ValidateFn = detail::directValidate<ValueType, OptionInfo>;
    if (this->NoValPlaceholder)
      this->SuppressValuePlaceholder = true;
    if constexpr (std::is_enum_v<ValueType> || std::is_same_v<ValueType, int>)
      this->template finalizeEnumMetadata<OptionInfo>();
  }
};

/// Multi-value option descriptor.
template <typename ValueType>
class ListOptionInfo : public CommonOptionInfo<ValueType> {
  using Base = CommonOptionInfo<ValueType>;

protected:
  using Base::HandleArg;

public:
  using ValueT = ValueType;

  template <typename... Args>
  constexpr ListOptionInfo(llvm::StringRef Name, llvm::StringRef Desc,
                           Args &&...args)
      : Base(Name, Desc) {
    (HandleArg(args), ...);
    this->template finalizeCommon<ListOptionInfo>();
    this->IsPrefix = (this->FormattingFlag == PrefixFormat ||
                      this->FormattingFlag == AlwaysPrefixFormat);
    this->IsAlwaysPrefix = (this->FormattingFlag == AlwaysPrefixFormat);
    this->IsPositionalEatsArgs =
        (this->MiscFlagsBits & PositionalEatsArgs) != 0;
    this->DefaultValueName = detail::defaultValueName<ValueType>();
    this->IsPositional =
        this->RawIsPositional || this->NumOccurrencesFlag == ConsumeAfter;
    this->OccurrencesFlag = (this->NumOccurrencesFlag == Optional)
                                ? ZeroOrMore
                                : this->NumOccurrencesFlag;
    this->ValueExpected =
        (this->RawValueExpected == ValueOptional && !this->ValueExplicitlySet)
            ? ValueRequired
            : this->RawValueExpected;
    if constexpr (std::is_same_v<ValueType, bool>) {
      this->ValueExpected = ValueOptional;
      this->SuppressValuePlaceholder = true;
    }
    this->ParseFn = detail::directParseList<ValueType, ListOptionInfo>;
    this->DefaultFn = detail::directClearList<ValueType>;
    // A list slot is a ListStorage<T>, not a single value; skip it.
    this->PrintValueFn = nullptr;
    if (this->TypedValidateFn)
      this->ValidateFn = detail::directValidateList<ValueType, ListOptionInfo>;
    if (this->NoValPlaceholder)
      this->SuppressValuePlaceholder = true;
    this->template finalizeEnumMetadata<ListOptionInfo>();
  }
};

/// Bitmask multi-set enum option descriptor. Each occurrence ORs a bit into an
/// unsigned bitmask.
template <typename ValueType>
struct BitsOptionInfo : CommonOptionInfo<ValueType> {
  using Base = CommonOptionInfo<ValueType>;
  using StorageT = unsigned;

protected:
  using Base::HandleArg;

public:
  using ValueT = ValueType;

  template <std::size_t N, typename... Args>
  constexpr BitsOptionInfo(llvm::StringRef Name, llvm::StringRef Desc,
                           const EnumVal<ValueType> (&Vals)[N], Args &&...args)
      : Base(Name, Desc) {
    this->EnumVals = Vals;
    this->RawNumEnumVals = N;
    (HandleArg(args), ...);
    this->MiscFlagsBits |= CommaSeparated;
    this->template finalizeCommon<BitsOptionInfo>();
    // Bits options are always a named, value-taking, repeatable flag.
    this->IsPositional = false;
    this->IsPrefix = false;
    this->OccurrencesFlag = ZeroOrMore;
    this->ValueExpected = ValueRequired;
    this->ParseFn = detail::directParseBits<ValueType, BitsOptionInfo>;
    this->DefaultFn = detail::directClearBits;
    this->PrintValueFn = detail::directPrintValue<ValueType, BitsOptionInfo>;
    this->template finalizeEnumMetadata<BitsOptionInfo>();
  }
};

/// Empty base class used to identify AliasInfo without relying on partial
/// specialisation of auto*... packs (which is ill-formed in C++17).
struct AliasTag {};

/// True when T is AliasInfo.
template <typename T>
inline constexpr bool IsAliasInfo_v = std::is_base_of_v<AliasTag, T>;

/// Option alias descriptor.
struct AliasInfo : AliasTag {
  llvm::StringRef CLIName;
  llvm::StringRef AliasFor;

  struct NoStorage {};
  using StorageT = NoStorage;

  llvm::StringRef Desc;

  constexpr AliasInfo(llvm::StringRef Name, llvm::StringRef Target)
      : CLIName(Name), AliasFor(Target) {}
  constexpr AliasInfo(llvm::StringRef Name, llvm::StringRef Target,
                      llvm::StringRef D)
      : CLIName(Name), AliasFor(Target), Desc(D) {}
};

/// Maps an option descriptor type to the runtime storage type held in
/// ParsedOptions. Every descriptor must define ValueT.
template <typename D, typename = void> struct StorageTypeOf {
  using Type = typename D::ValueT;
};

// If D provides a StorageT member (e.g. SubCommandInfo<...>), use that
// instead. The void_t trick avoids partial specialisation on auto*... packs,
// which is ill-formed in C++17.
template <typename D>
struct StorageTypeOf<D, std::void_t<typename D::StorageT>> {
  using Type = typename D::StorageT;
};

// List options store a vector of their element type.
template <typename T> struct ListStorage {
  std::vector<T> Values;
  std::vector<unsigned> Positions;
};

template <typename T> struct StorageTypeOf<ListOptionInfo<T>, void> {
  using Type = ListStorage<T>;
};

template <auto *...Infos> class ParsedOptions;

/// Empty base class used to identify SubCommandInfo specialisations without
/// relying on partial specialisation matching of auto*... packs (which is
/// ill-formed in C++17). Use std::is_base_of_v<SubCommandTag, T> or the
/// helper IsSubCommandInfo_v<T> instead of a trait with partial specs.
struct SubCommandTag {};

/// True when T is any SubCommandInfo<...> specialisation.
template <typename T>
inline constexpr bool IsSubCommandInfo_v = std::is_base_of_v<SubCommandTag, T>;

/// True when T is a ListOptionInfo<U>. ListOptionInfo is an ordinary class
/// template, so partial specialisation is fine here -- unlike the auto*... pack
/// cases above, which need a tag base.
template <typename T> struct IsListOptionInfoImpl : std::false_type {};
template <typename T>
struct IsListOptionInfoImpl<ListOptionInfo<T>> : std::true_type {};
template <typename T>
inline constexpr bool IsListOptionInfo_v =
    IsListOptionInfoImpl<std::remove_const_t<T>>::value;

/// Subcommand descriptor. Declare at namespace scope as inline constexpr, then
/// include in a OptionsRegistry. SubCommandInfo carries only constexpr data.
/// The runtime parsing machinery lives in OptionsRegistry::buildEntries so that
/// SubCommandInfo itself does not depend on the full ParsedOptions definition.
template <auto *...SubOpts> struct SubCommandInfo : SubCommandTag {
  llvm::StringRef Name;
  llvm::StringRef Desc;
  constexpr SubCommandInfo(llvm::StringRef N, llvm::StringRef D)
      : Name(N), Desc(D) {}

  // StorageT is picked up by StorageTypeOf<D, void_t<D::StorageT>>.
  // Forming std::optional<ParsedOptions<...>> with an incomplete ParsedOptions
  // is valid - std::optional only requires completeness when constructed.
  using StorageT = std::optional<ParsedOptions<SubOpts...>>;
};

namespace detail {

struct POAccess;

// Compile-time search for Needle (a pointer NTTP) in the Haystack pack.
// Comparisons are done via const void* so that differently-typed pointers
// compare safely. Returns static_cast<size_t>(-1) when Needle is absent.
template <auto *Needle, auto *...Haystack> constexpr std::size_t indexOfImpl() {
  const void *NeedlePtr = static_cast<const void *>(Needle);
  // Use a C-array + loop - valid constexpr in C++17.
  const void *HaystackPtrs[] = {static_cast<const void *>(Haystack)...};
  for (std::size_t I = 0; I < sizeof...(Haystack); ++I)
    if (HaystackPtrs[I] == NeedlePtr)
      return I;
  return static_cast<std::size_t>(-1);
}

/// One option's storage slot. Indexed by pack position so that two options with
/// the same storage type stay distinct base classes.
template <std::size_t I, typename T> struct StorageSlot {
  T Value{};
};

/// Flat, non-recursive type for parsed option storage.
template <typename Seq, typename... Ts> struct FlatStorage;
template <std::size_t... Is, typename... Ts>
struct FlatStorage<std::index_sequence<Is...>, Ts...> : StorageSlot<Is, Ts>... {
};

} // namespace detail

/// Compile-time index of \p Needle in \p Haystack. Triggers a static_assert
/// via an out-of-bounds std::get if Needle is not in the pack.
template <auto *Needle, auto *...Haystack>
inline constexpr std::size_t index_of_v =
    detail::indexOfImpl<Needle, Haystack...>();

/// Type-erased base class for ParsedOptions.
class ParsedOptionsBase {
public:
  virtual ~ParsedOptionsBase() = default;

  /// RTTI-free type identity. Each ParsedOptions<...> specialisation returns
  /// a unique static address as its type key. Used by LLVMContext::getOptions
  /// to check the dynamic type without requiring -frtti.
  virtual const void *typeKey() const = 0;
};

/// Typed parsed-value store created by the parser.
template <auto *...Infos> class ParsedOptions : public ParsedOptionsBase {
  // Storage type for the option at pack position I.
  template <auto *Info>
  using SlotTypeOf = typename StorageTypeOf<
      std::remove_const_t<std::remove_pointer_t<decltype(Info)>>>::Type;

  // Storage type with a slot for each option in the pack.
  using Storage =
      detail::FlatStorage<std::index_sequence_for<decltype(Infos)...>,
                          SlotTypeOf<Infos>...>;

  Storage Values;

  /// The slot for the option at pack position \p I. \p Info must be the
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

  template <auto *...> friend class OptionsRegistry;
  friend struct detail::POAccess;

public:
  ParsedOptions() = default;
  ParsedOptions(ParsedOptions &&) = default;
  ParsedOptions &operator=(ParsedOptions &&) = default;

  ParsedOptions(const ParsedOptions &) = default;
  ParsedOptions &operator=(const ParsedOptions &) = default;

  ParsedOptions clone() const { return ParsedOptions(*this); }

  /// Value for the option at known pack position \p Idx.
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
  /// The index is resolved at compile time - no runtime map lookup.
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
  /// line, or 0 if the option was never specified. Useful for resolving
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
  /// subcommand on the command line. Only valid for SubCommandInfo entries.
  template <auto *Cmd> bool isActive() const {
    constexpr std::size_t Idx = index_of_v<Cmd, Infos...>;
    static_assert(Idx != static_cast<std::size_t>(-1),
                  "Subcommand not present in this registry");
    return slotAt<Idx, Cmd>().has_value();
  }

  /// Returns a reference to the parsed sub-options for subcommand \p Cmd.
  /// Only valid when isActive<Cmd>() and asserts otherwise.
  template <auto *Cmd> auto &getSubOptions() {
    constexpr std::size_t Idx = index_of_v<Cmd, Infos...>;
    static_assert(Idx != static_cast<std::size_t>(-1),
                  "Subcommand not present in this registry");
    assert((slotAt<Idx, Cmd>().has_value() &&
            "getSubOptions() on a subcommand that was not selected; "
            "check isActive<Cmd>() first"));
    return *slotAt<Idx, Cmd>();
  }
  template <auto *Cmd> const auto &getSubOptions() const {
    constexpr std::size_t Idx = index_of_v<Cmd, Infos...>;
    static_assert(Idx != static_cast<std::size_t>(-1),
                  "Subcommand not present in this registry");
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

namespace detail {

/// Diagnostic context passed to parse callbacks during parsing.
/// Replaces reads of global CurrentProgramName so parsers are re-entrant.
struct ParseDiag {
  llvm::raw_ostream &Errs;
  llvm::StringRef ProgramName;
};

/// Runtime description of a single option, built from the compile-time
/// descriptor pack. All type information is erased into callbacks so the
/// non-template runtime parser only sees this.
struct OptionEntry {
  // Fields are grouped by alignment - pointers, then StringRefs, then the
  // small scalars - so that the narrow members share one tail word instead of
  // each opening an 8-byte hole. Interleaving them costs 16 bytes per entry.

  /// Points into ParsedOptions::Occurrences. Every producer sets this, but
  /// it is initialised so a producer that forgets yields a deterministic
  /// crash rather than a wild pointer.
  unsigned *OccurrenceCount = nullptr;
  /// Points into ParsedOptions::Positions. Updated to the argv index of the
  /// last occurrence.
  unsigned *LastPosition = nullptr;

  /// Pointer to the per-element position vector held in the option's own
  /// ListStorage slot. Null for scalar options. The parser pushes the argv
  /// index of each successfully parsed element.
  std::vector<unsigned> *ElementPositions = nullptr;

  /// Category for grouped help output.
  const OptionCategory *Cat = nullptr;

  /// T* or std::vector<T>*.
  void *ParseSlot = nullptr;

  /// The option's hidden flag. The parser rewrites this per parse to implement
  /// hideAllRegistered and hideAllUnregistered.
  OptionHidden HiddenFlag;

  /// An entry-specific number ParseFn may interpret however it needs
  /// (standalone enum flags use it as the index of their enum value).
  unsigned ParseAux = 0;

  OptionEntry() = default;

  /// Fills the fields every producer sets; the rarer ones (LastPosition,
  /// ElementPositions, Static) default to null and are filled in by the
  /// caller afterwards when needed -- see buildSingleEntry's comments for
  /// why Static in particular is usually attached later.
  OptionEntry(OptionHidden HiddenFlag, unsigned *OccurrenceCount,
              const OptionCategory *Cat, void *ParseSlot,
              unsigned *LastPosition = nullptr,
              std::vector<unsigned> *ElementPositions = nullptr,
              const OptionStaticInfo *Static = nullptr)
      : OccurrenceCount(OccurrenceCount), LastPosition(LastPosition),
        ElementPositions(ElementPositions), Cat(Cat), ParseSlot(ParseSlot),
        HiddenFlag(HiddenFlag), Static(Static) {}

  /// Static half of the entry, shared by every parse. For a compile-time
  /// descriptor, points directly at its own OptionStaticInfo base subobject
  /// (still a constexpr, .rodata object - no separate global). Otherwise
  /// points at a manually-built/sliced OptionStaticInfo (builtins, aliases,
  /// standalone-enum flags, RuntimeOption).
  const OptionStaticInfo *Static = nullptr;

  const OptionStaticInfo &info() const {
    assert(Static && "OptionEntry has no static half");
    return *Static;
  }

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
  const void *parseDesc() const { return detail::effectiveDesc(info()); }
  llvm::StringRef enumGroupHeader() const { return info().EnumGroupHeader; }
  llvm::StringRef groupSortKeyOverride() const {
    return info().GroupSortKeyOverride;
  }
  bool isEnumGroupMember() const { return info().IsEnumGroupMember; }

  std::size_t maxEnumUsed() const {
    if (!info().EnumMetrics)
      return 0;
    std::size_t MaxUsed = 0;
    bool Dual = false;
    info().EnumMetrics(effectiveDesc(info()), MaxUsed, Dual);
    return MaxUsed;
  }
  bool showDualDisplay() const {
    if (!info().EnumMetrics)
      return false;
    std::size_t MaxUsed = 0;
    bool Dual = false;
    info().EnumMetrics(effectiveDesc(info()), MaxUsed, Dual);
    return Dual;
  }

  void printEnumVal(llvm::raw_ostream &OS, std::size_t I,
                    std::size_t Width) const {
    info().PrintEnumVal(effectiveDesc(info()), OS, I, Width);
  }
  bool hasEnumPrinter() const { return info().PrintEnumVal != nullptr; }

  bool parse(llvm::StringRef Val, ParseDiag &Diag) const {
    if (!info().ParseFn(effectiveDesc(info()), ParseSlot, ParseAux, Val, Diag))
      return false;
    if (info().ValidateFn)
      return info().ValidateFn(effectiveDesc(info()), ParseSlot, name(), Diag);
    return true;
  }

  void applyDefault() const {
    if (info().DefaultFn)
      info().DefaultFn(effectiveDesc(info()), ParseSlot);
  }
};

struct AliasEntry;

/// Runtime description of a subcommand, built by OptionsRegistry::buildEntries
/// for each SubCommandInfo pointer in the registry pack.
struct SubCommandSpec {
  llvm::StringRef Name;
  llvm::StringRef Desc;

  /// Called by the parser when this subcommand's name is matched. Initialises
  /// the optional slot and returns the option entries for the subcommand so the
  /// main parse loop can handle them alongside global options.
  std::function<std::vector<OptionEntry>()> BuildAndInit;

  /// Aliases scoped to this subcommand.
  std::vector<AliasEntry> Aliases;
};

/// Alias name/target pair collected during the first pass of buildEntries.
struct AliasEntry {
  llvm::StringRef Name;
  llvm::StringRef Target;
  llvm::StringRef Desc;
};

/// A library-local registry registered for automatic discovery at static-init
/// time. The descriptor itself is immutable after registration, so the global
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
  /// Optional post-parse hook, given that parse's values. Writing to global
  /// variables should be avoided.
  std::function<void(const ParsedOptionsBase &)> Apply;
  /// Number of descriptors, so runParser can size the entry vector up front.
  std::size_t NumOptions = 0;
};

/// The single DynamicRegistration for --color, built once and drained by every
/// parse regardless of whether the callers enable dynamic entries.
LLVM_ABI const DynamicRegistration &getColorDynamicRegistration();

struct ParseFrame;

/// Everything the built-in options (--help, --version, ...) need in order to
/// run. Stored once per parse in the ParseFrame, so each builtin's ParseFn can
/// reach it instead of every entry carrying its own std::function.
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
  /// Set by --print-all-options / --print-options. The dump happens after
  /// parsing finishes, since that is when the values are final.
  bool PrintAllOptions = false;
  bool PrintSpecifiedOptions = false;
};

/// Builtin option slots, used to identify common options.
enum BuiltinSlot : unsigned {
  BS_Help,
  BS_HelpHidden,
  BS_HelpList,
  BS_HelpListHidden,
  BS_Version,
  BS_H,
  BS_PrintAllOptions,
  BS_PrintOptions,
  BS_Count,
};

/// Holds the StringMap and prefix/sink lists for a fixed set of registry
/// entries, pre-built once and shared by every parse that reuses that entry
/// layout.
class BakedNameIndex;

struct ParseFrame {
  llvm::StringRef ProgramName;
  unsigned CurArgPosition = 0;
  std::vector<OptionEntry> *ActiveEntries = nullptr;
  std::string ActiveSubCommandName;
  llvm::ArrayRef<const OptionCategory *> AllowedCategories;
  bool HideUnrelated = false;
  /// Set when a builtin produced terminal output (--help, --help-list,
  /// --version, ...). The parser then skips occurrence validation, and the
  /// parse entry points return nullptr under OnError::Return so the caller
  /// does not act on a result that was never meant to be used.
  bool HelpPrinted = false;
  bool HideAllRegistered = false;
  unsigned BuiltinOccurrences[BS_Count] = {};
  llvm::ArrayRef<llvm::StringRef> ShownNames;
  /// Name/description of every subcommand visible to this parse.
  llvm::SmallVector<std::pair<llvm::StringRef, llvm::StringRef>, 8> Subcommands;
  /// Number of global entries before subcommand entries were merged.
  std::size_t GlobalEntryCount = 0;
  /// Per-parse storage for every dynamically-registered registry drained by
  /// this parse because the parser opted in, paired with that
  /// registration's index in the global registration list.
  /// The registration list only ever grows, so indices stay valid.
  std::vector<std::pair<std::size_t, std::unique_ptr<ParsedOptionsBase>>>
      DynamicStorages;
  /// Per-parse storage for --color's DynamicRegistration, which every parse
  /// drains regardless of their use of dynamic entries.
  std::unique_ptr<ParsedOptionsBase> ColorStorage;
  /// Shared state for the built-in options.
  BuiltinOptionState Builtins;
  /// One OptionStaticInfo per builtin. Builtins have no descriptor, so theirs
  /// are filled in directly.
  OptionStaticInfo BuiltinStatics[BS_Count];

  /// Resolved from whether an error stream was supplied to the parser.
  OnError OnErr = OnError::ExitProcess;

  /// Name index over the registry-entry prefix, pre-built once and shared by
  /// every parse that reuses it. Null when the index is instead built lazily
  /// for this parse.
  const BakedNameIndex *Baked = nullptr;
  /// The baked block occupies [BakedFirst, BakedFirst + BakedCount). It is
  /// not at index 0 because buildBuiltinEntries prepends --help and friends.
  std::size_t BakedFirst = 0;
  std::size_t BakedCount = 0;

  /// Entries in [DefaultedFirst, DefaultedFirst + DefaultedCount) whose storage
  /// was already default-initialised, so the parser's default value application
  /// sweep can skip them.
  std::size_t DefaultedFirst = 0;
  std::size_t DefaultedCount = 0;
  /// Static info for alias proxies, which copy their target's but with a
  /// different name. A deque so addresses stay stable as aliases resolve.
  std::deque<OptionStaticInfo> AliasStatics;
};

/// Append globally-registered dynamic entries to the entry list.
/// Inject built-in option entries (help, help-hidden, help-list,
/// help-list-hidden, version) at the front of \p Entries with proper parse
/// actions. These entries carry \c Cat = \c GenericOptionsCategory so they
/// appear under the "Generic Options" section of help output. Since they live
/// in the entry vector, \c AliasInfo{"h","help"} resolves normally.
/// Returns the number of entries inserted before the pre-existing ones, so a
/// caller holding indices into that block can rebase them.
LLVM_ABI std::size_t
buildBuiltinEntries(std::vector<OptionEntry> &Entries, llvm::StringRef Overview,
                    llvm::StringRef ProgName, llvm::StringRef VersionString,
                    llvm::raw_ostream *HelpOS, llvm::StringRef ExtraHelp,
                    std::function<void(llvm::raw_ostream &)> VersionPrinter,
                    llvm::raw_ostream *Errs, ParseFrame &Frame);

/// Run the parse over \p GlobalEntries.
///
/// Builtin entries must already have been set up, and it stores builtins in
/// ParseFrame::Builtins where the --help and --version builtins read them.
LLVM_ABI bool runParser(std::vector<OptionEntry> &GlobalEntries,
                        std::vector<SubCommandSpec> &SubCommands, int argc,
                        const char *const *argv, llvm::raw_ostream *Errs,
                        ParseFrame &Frame, bool DrainDynamic = true);

LLVM_ABI void printHelpList(llvm::ArrayRef<OptionEntry> Entries,
                            llvm::StringRef Overview, llvm::StringRef ProgName,
                            bool ShowHidden, llvm::raw_ostream &OS,
                            const ParseFrame &Frame);

LLVM_ABI void printHelp(llvm::ArrayRef<OptionEntry> Entries,
                        llvm::StringRef Overview, llvm::StringRef ProgName,
                        bool ShowHidden, llvm::raw_ostream &OS,
                        const ParseFrame &Frame,
                        llvm::StringRef ExtraHelp = {});

/// For each AliasEntry, find the target option entry by name and push a proxy
/// entry with the alias name. Reports unresolved aliases to \p Errs (or
/// llvm::errs() if null).
LLVM_ABI void resolveAliases(std::vector<OptionEntry> &Entries,
                             llvm::ArrayRef<AliasEntry> Aliases,
                             ParseFrame &Frame,
                             llvm::raw_ostream *Errs = nullptr);

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

/// Look up \p Val in a descriptor's EnumVals table, honouring the
/// case-insensitivity flag. Returns nullptr and emits the standard diagnostic
/// when there is no match.
template <typename T, typename DescT>
const EnumVal<T> *lookupEnumValue(const DescT *Desc, llvm::StringRef Val,
                                  ParseDiag &Diag) {
  const bool CaseInsensitive =
      (Desc->MiscFlagsBits & CaseInsensitiveValues) != 0;
  for (std::size_t I = 0; I < Desc->RawNumEnumVals; ++I) {
    llvm::StringRef Name = Desc->EnumVals[I].Name;
    if (CaseInsensitive ? Val.equals_insensitive(Name) : Val == Name)
      return &Desc->EnumVals[I];
  }
  if (!Diag.ProgramName.empty())
    Diag.Errs << Diag.ProgramName << ": ";
  Diag.Errs << "for the --" << Desc->Name
            << " option: Cannot find option named '" << Val << "'!\n";
  return nullptr;
}

/// Scalar parsers - one per supported value type.
template <typename T, typename DescT>
bool directParse(const void *D, void *S, unsigned, llvm::StringRef Val,
                 ParseDiag &Diag) {
  auto *Desc = static_cast<const DescT *>(D);
  auto &Slot = *static_cast<T *>(S);
  if constexpr (std::is_same_v<T, bool>) {
    if (!parseBoolArg(Desc->Name, Val, Slot, Diag))
      return false;
  } else if constexpr (std::is_same_v<T, std::string>) {
    Slot = Val.str();
  } else if constexpr (std::is_same_v<T, int>) {
    if (Desc->RawNumEnumVals > 0) {
      const EnumVal<int> *EV = lookupEnumValue<int>(Desc, Val, Diag);
      if (!EV)
        return false;
      Slot = static_cast<int>(EV->Value);
    } else if (!parseIntArg(Desc->Name, Val, Slot, Diag))
      return false;
  } else if constexpr (std::is_same_v<T, unsigned>) {
    if (!parseUIntArg(Desc->Name, Val, Slot, Diag))
      return false;
  } else if constexpr (std::is_same_v<T, int64_t>) {
    if (!parseInt64Arg(Desc->Name, Val, Slot, Diag))
      return false;
  } else if constexpr (std::is_same_v<T, uint64_t>) {
    if (!parseUInt64Arg(Desc->Name, Val, Slot, Diag))
      return false;
  } else if constexpr (std::is_same_v<T, float>) {
    if (!parseFloatArg(Desc->Name, Val, Slot, Diag))
      return false;
  } else if constexpr (std::is_same_v<T, double>) {
    if (!parseDoubleArg(Desc->Name, Val, Slot, Diag))
      return false;
  } else if constexpr (std::is_same_v<T, llvm::ElementCount>) {
    if (!parseElementCountArg(Desc->Name, Val, Slot, Diag))
      return false;
  } else if constexpr (std::is_same_v<T, cl::boolOrDefault>) {
    bool Tmp{};
    if (!parseBoolArg(Desc->Name, Val, Tmp, Diag))
      return false;
    Slot = Tmp ? cl::boolOrDefault::BOU_TRUE : cl::boolOrDefault::BOU_FALSE;
  } else if constexpr (std::is_same_v<T, std::optional<bool>>) {
    bool Tmp{};
    if (!parseBoolArg(Desc->Name, Val, Tmp, Diag))
      return false;
    Slot = Tmp;
  } else if constexpr (std::is_enum_v<T>) {
    if (Val.empty() && Desc->ValueExplicitlySet &&
        Desc->RawValueExpected == ValueOptional) {
      for (std::size_t I = 0; I < Desc->RawNumEnumVals; ++I)
        if (Desc->EnumVals[I].Name.empty()) {
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

/// List parser - appends one element per occurrence, optionally calls callback.
template <typename T, typename DescT>
bool directParseList(const void *D, void *S, unsigned, llvm::StringRef Val,
                     ParseDiag &Diag) {
  auto *Desc = static_cast<const DescT *>(D);
  auto &Slot = *static_cast<std::vector<T> *>(S);
  if constexpr (std::is_same_v<T, std::string>) {
    Slot.push_back(Val.str());
  } else if constexpr (std::is_same_v<T, int>) {
    int Tmp{};
    if (!parseIntArg(Desc->Name, Val, Tmp, Diag))
      return false;
    Slot.push_back(Tmp);
  } else if constexpr (std::is_same_v<T, unsigned>) {
    unsigned Tmp{};
    if (!parseUIntArg(Desc->Name, Val, Tmp, Diag))
      return false;
    Slot.push_back(Tmp);
  } else if constexpr (std::is_same_v<T, bool>) {
    bool Tmp{};
    if (!parseBoolArg(Desc->Name, Val, Tmp, Diag))
      return false;
    Slot.push_back(Tmp);
  } else if constexpr (std::is_same_v<T, uint64_t>) {
    uint64_t Tmp{};
    if (!parseUInt64Arg(Desc->Name, Val, Tmp, Diag))
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

/// Bits parser - ORs (1u << index) into the unsigned slot.
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

/// List default applier - clears the vector.
template <typename T> void directClearList(const void *, void *S) {
  static_cast<std::vector<T> *>(S)->clear();
}

/// Bits default applier - zeros the unsigned slot.
inline void directClearBits(const void *, void *S) {
  *static_cast<unsigned *>(S) = 0u;
}

/// True when `OS << T{}` compiles. Slot types that cannot be streamed print a
/// marker instead of blocking the whole dump.
template <typename T, typename = void>
inline constexpr bool HasStreamOp_v = false;
template <typename T>
inline constexpr bool
    HasStreamOp_v<T, std::void_t<decltype(std::declval<llvm::raw_ostream &>()
                                          << std::declval<const T &>())>> =
        true;

/// Value printer for --print-all-options / --print-options. One instantiation
/// Reports a rejected option value as "<prog>: for the --<opt> option: <msg>".
LLVM_ABI bool rejectOptionValue(llvm::StringRef OptName, const llvm::Twine &Msg,
                                ParseDiag &Diag);

/// Shared Validate body for options taking a regular expression.
LLVM_ABI bool validateRegexOption(llvm::StringRef Pattern,
                                  llvm::StringRef OptName, ParseDiag &Diag);

/// Type-erased bridge to the descriptor's Validate modifier. Instantiated per
/// (T, DescT) like the other direct* helpers.
template <typename T, typename DescT>
bool directValidate(const void *D, const void *S, llvm::StringRef Name,
                    ParseDiag &Diag) {
  auto *Desc = static_cast<const DescT *>(D);
  return Desc->TypedValidateFn(*static_cast<const T *>(S), Name, Diag);
}

/// List counterpart to directValidate. directParseList appends before parse()
/// calls this, so the element under test is the last one.
template <typename T, typename DescT>
bool directValidateList(const void *D, const void *S, llvm::StringRef Name,
                        ParseDiag &Diag) {
  auto *Desc = static_cast<const DescT *>(D);
  const auto &Slot = *static_cast<const std::vector<T> *>(S);
  if (Slot.empty())
    return true;
  return Desc->TypedValidateFn(Slot.back(), Name, Diag);
}

/// Per (T, DescT) like directParse and directApplyDefault, so a registry pays
/// per option shape, not per option.
template <typename T, typename DescT>
void directPrintValue(const void *D, const void *S, llvm::raw_ostream &OS) {
  const auto &Slot = *static_cast<const T *>(S);
  if constexpr (std::is_same_v<T, bool>) {
    OS << (Slot ? "true" : "false");
  } else if constexpr (std::is_enum_v<T>) {
    // Spell the enumerator, falling back to the raw value for
    // an enum whose descriptor carries no value table.
    auto *Desc = static_cast<const DescT *>(D);
    for (std::size_t I = 0; I < Desc->RawNumEnumVals; ++I)
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

/// Default applier - applies the Init value from the descriptor.
template <typename T, typename DescT>
void directApplyDefault(const void *D, void *S) {
  auto *Desc = static_cast<const DescT *>(D);
  auto &Slot = *static_cast<T *>(S);
  if constexpr (std::is_same_v<T, std::string>) {
    if (Desc->HasDefault)
      Slot = Desc->DefaultValue.str();
    else
      Slot.clear();
  } else {
    Slot = Desc->HasDefault ? static_cast<T>(Desc->DefaultValue) : T{};
  }
}

/// Prints one enum value's help sub-line.
template <typename DescT>
void printEnumValueLine(const void *D, llvm::raw_ostream &OS, std::size_t I,
                        std::size_t CatMaxArgLen) {
  auto *Desc = static_cast<const DescT *>(D);
  llvm::StringRef EName = Desc->EnumVals[I].Name;
  bool HasDesc = !Desc->EnumVals[I].Desc.empty();
  if (EName.empty() && !HasDesc)
    return;
  llvm::StringRef DisplayName = EName.empty() ? "<empty>" : EName;
  constexpr llvm::StringRef Prefix = "    =";
  OS << Prefix << DisplayName;
  if (HasDesc) {
    std::size_t Target = (CatMaxArgLen > 3) ? CatMaxArgLen - 3 : 0;
    std::size_t Printed = Prefix.size() + DisplayName.size();
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

/// Compute the enum help metrics that cannot be stored in the option
/// information descriptors.
template <typename DescT>
void computeEnumMetrics(const void *D, std::size_t &MaxUsed,
                        bool &DualDisplay) {
  const auto *Desc = static_cast<const DescT *>(D);
  MaxUsed = 0;
  DualDisplay = false;
  if (!Desc->EnumVals || Desc->RawNumEnumVals == 0)
    return;
  bool HasEmptyName = false;
  for (std::size_t I = 0; I < Desc->RawNumEnumVals; ++I) {
    llvm::StringRef EName = Desc->EnumVals[I].Name;
    std::size_t W = EName.size() + 8;
    if (W > MaxUsed)
      MaxUsed = W;
    if (EName.empty())
      HasEmptyName = true;
  }
  if (HasEmptyName && Desc->RawValueExpected == ValueOptional &&
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

/// Build one OptionEntry for a scalar OptionInfo<T>.
template <typename T>
OptionEntry buildSingleEntry(const OptionInfo<T> *Desc, T &Slot,
                             unsigned &Count, unsigned *Pos = nullptr,
                             std::vector<unsigned> * /*ElemPos*/ = nullptr) {
  return OptionEntry(Desc->OptionHiddenFlag, &Count, Desc->Category, &Slot,
                     Pos);
}

/// Build one OptionEntry for a ListOptionInfo<T>.
template <typename T>
OptionEntry buildSingleEntry(const ListOptionInfo<T> *Desc,
                             std::vector<T> &Slot, unsigned &Count,
                             unsigned *Pos = nullptr,
                             std::vector<unsigned> *ElemPos = nullptr) {
  // See buildSingleEntry(const OptionInfo<T> *): the descriptor-derived half
  // is in OptionStaticInfo, attached by the caller.
  return OptionEntry(Desc->OptionHiddenFlag, &Count, Desc->Category, &Slot, Pos,
                     ElemPos);
}

/// Build one OptionEntry for a BitsOptionInfo<T> (bitmask enum option).
/// Each parse occurrence ORs (1u << index) into the unsigned slot.
template <typename T>
OptionEntry buildSingleEntry(const BitsOptionInfo<T> *Desc, unsigned &Slot,
                             unsigned &Count, unsigned *Pos = nullptr,
                             std::vector<unsigned> * /*ElemPos*/ = nullptr) {
  // See buildSingleEntry(const OptionInfo<T> *): the descriptor-derived half
  // is in OptionStaticInfo, attached by the caller.
  return OptionEntry(Desc->OptionHiddenFlag, &Count, Desc->Category, &Slot,
                     Pos);
}

// Preprocessing step before runParser. Expands @file tokens in Argv in place
// using the platform-native tokenizer.
//
// Alloc must outlive all use of the pointers stored in Out.
// Returns true on success. On error, writes a message to *Errs (or to
// llvm::errs() + exits if Errs is null) and returns false.
LLVM_ABI bool expandArgs(OnError OnErr, int Argc, const char *const *Argv,
                         llvm::BumpPtrAllocator &Alloc,
                         llvm::SmallVectorImpl<const char *> &Out,
                         llvm::raw_ostream *Errs);

} // namespace detail

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

// Forward declarations: these free functions call one another.
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
/// the descriptor pack already carries.
template <auto *...SubOpts>
void buildSubEntries(ParsedOptions<SubOpts...> &Sub,
                     std::vector<OptionEntry> &Entries);

template <typename T> void applyOneDefault(const OptionInfo<T> *Desc, T &Slot) {
  if (!Desc->HasDefault)
    return;
  if constexpr (std::is_same_v<typename OptionInfo<T>::DefaultT,
                               llvm::StringRef>)
    Slot = Desc->DefaultValue.str();
  else
    Slot = static_cast<T>(Desc->DefaultValue);
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
/// option shape instead of once per option. A 285-option registry has only a
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
  detail::addPlainEntry(P, static_cast<const detail::OptionStaticInfo *>(P),
                        Slot, Count, Pos, GlobalEntries);
}

// Overload for AliasInfo - collect name/target pair for later resolution.
template <
    auto *Opt, typename SlotT,
    typename DescT = std::remove_const_t<std::remove_pointer_t<decltype(Opt)>>,
    std::enable_if_t<IsAliasInfo_v<DescT>, int> = 0>
void addOneEntry(SlotT & /*Slot*/, unsigned & /*Count*/, unsigned & /*Pos*/,
                 std::vector<detail::OptionEntry> & /*GlobalEntries*/,
                 std::vector<detail::AliasEntry> &AliasEntries,
                 std::vector<detail::SubCommandSpec> & /*SubSpecs*/) {
  AliasEntries.push_back({Opt->CLIName, Opt->AliasFor, Opt->Desc});
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
    addPlainEntry(Opt, static_cast<const detail::OptionStaticInfo *>(Opt),
                  POAccess::slot<I, Opt>(Sub), POAccess::occurrences(Sub)[I],
                  POAccess::positions(Sub)[I], Entries);
  }
}

// Populate Entries from a ParsedOptions<SubOpts...>. AliasInfo entries are
// skipped (they carry no storage).
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

  /// Populate \p GlobalEntries, \p AliasEntries, and \p SubSpecs from this
  /// registry using \p Result as storage for slot references. Called by
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

  /// Parse argv against this registry alone, with no OptionsContext. For
  /// tools that only ever need one registry and never combine it with
  /// others -- use OptionParser instead when composing multiple registries,
  /// filtering by category, or registering dynamic/subcommand entries.
  ///
  /// Returns std::nullopt on a parse error, and also on --help/--version
  /// when \p Errs is non-null (matching OptionParser::parse()'s existing
  /// convention of collapsing both cases into "no result"). When \p Errs is
  /// null, a parse error or --help/--version exits the process directly, so
  /// this never returns std::nullopt in that mode.
  ///
  /// Unlike OptionParser, this does not write to any legacy cl::opt globals
  /// -- registries relied on for that bridging must go through
  /// OptionParser instead.
  std::optional<ParsedOptionsT>
  parse(int argc, const char *const *argv, llvm::StringRef Overview = {},
        llvm::raw_ostream *Errs = nullptr, llvm::StringRef VersionString = {},
        llvm::raw_ostream *HelpOS = nullptr,
        std::function<void(llvm::raw_ostream &)> VersionPrinter = {}) const {
    ParsedOptionsT Storage;
    std::vector<detail::OptionEntry> Entries;
    std::vector<detail::AliasEntry> Aliases;
    std::vector<detail::SubCommandSpec> SubSpecs;
    Entries.reserve(sizeof...(Opts));
    buildEntries(Storage, Entries, Aliases, SubSpecs,
                 std::make_index_sequence<sizeof...(Opts)>{});

    detail::ParseFrame Frame;
    Frame.OnErr = Errs ? OnError::Return : OnError::ExitProcess;

    llvm::BumpPtrAllocator ResponseFileAlloc;
    llvm::SmallVector<const char *, 20> ExpandedArgv;
    if (!detail::expandArgs(Frame.OnErr, argc, argv, ResponseFileAlloc,
                            ExpandedArgv, Errs))
      return std::nullopt;
    int ExpandedArgc = static_cast<int>(ExpandedArgv.size());
    const char *const *ExpandedArgvPtr = ExpandedArgv.data();
    llvm::StringRef ProgName =
        ExpandedArgc > 0 ? llvm::StringRef(ExpandedArgvPtr[0]) : llvm::StringRef();

    const std::size_t RegistryCount = Entries.size();
    Frame.DefaultedFirst = detail::buildBuiltinEntries(
        Entries, Overview, ProgName, VersionString, HelpOS, ExtraHelp_,
        std::move(VersionPrinter), Errs, Frame);
    Frame.DefaultedCount = RegistryCount;
    detail::resolveAliases(Entries, Aliases, Frame, Errs);

    bool Ok = detail::runParser(Entries, SubSpecs, ExpandedArgc,
                                ExpandedArgvPtr, Errs, Frame,
                                /*DrainDynamic=*/false);
    if (!Ok)
      return std::nullopt;
    if (Frame.HelpPrinted && Frame.OnErr == OnError::Return)
      return std::nullopt;
    return Storage;
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
  /// Only the option's declared registry is checked. A descriptor may
  /// legitimately appear in more than one registry - MCSchedule.cpp builds a
  /// private one-option registry from a descriptor declared in MCOptsReg - and
  /// its emitted index refers to the declared one, so requiring agreement
  /// everywhere would reject correct code. That is also why the read path
  /// only trusts the index when the registry matches.
  template <const auto *Self> static constexpr bool packIndicesAgree() {
    return packIndicesAgreeImpl<Self>(
        std::make_index_sequence<sizeof...(Opts)>{});
  }

  /// Build option entries into external vectors from an externally-owned
  /// ParsedOptions.
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
    (detail::applyOneDefault(Opts, Storage->template slotAt<Is, Opts>()), ...);
  }

  // Pair each pointer in Opts... with its ParsedOptions slot using the index
  // sequence.
  template <std::size_t... Is>
  static void buildEntries(ParsedOptionsT &Result,
                           std::vector<detail::OptionEntry> &GlobalEntries,
                           std::vector<detail::AliasEntry> &AliasEntries,
                           std::vector<detail::SubCommandSpec> &SubSpecs,
                           std::index_sequence<Is...>) {
    ((detail::addOneEntry<Opts>(Result.template slotAt<Is, Opts>(),
                                Result.Occurrences[Is], Result.Positions[Is],
                                GlobalEntries, AliasEntries, SubSpecs)),
     ...);
  }
};

/// Type-erased handle to an OptionsRegistry. Carries function pointers for
/// creating storage, building OptionEntries, and calling the bridge function.
struct TypeErasedRegistry {
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

/// Create a TypeErasedRegistry from a compile-time OptionsRegistry pointer.
template <const auto *Reg> TypeErasedRegistry typeEraseRegistry() {
  using RegT = std::remove_pointer_t<decltype(Reg)>;
  // A TableGen-emitted index that disagreed with the real pack order would
  // silently read a different option. Checked here rather than at each read as
  // this is the one place that has both the registry address and its pack, and
  // every registry handed to an OptionParser passes through it.
  static_assert(RegT::template packIndicesAgree<Reg>(),
                "emitted option index disagrees with registry pack order");
  using PO = typename RegT::ParsedOptionsT;
  TypeErasedRegistry R;
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
/// effects (e.g. setting legacy globals). Only instantiated where a bridge is
/// actually supplied, so the long function-pointer type is confined to those.
template <const auto *Reg,
          void (*BridgeFn)(const typename std::remove_pointer_t<
                           decltype(Reg)>::ParsedOptionsT &)>
TypeErasedRegistry typeEraseRegistryWithBridge() {
  using PO = typename std::remove_pointer_t<decltype(Reg)>::ParsedOptionsT;
  TypeErasedRegistry R = typeEraseRegistry<Reg>();
  R.Bridge = +[](const ParsedOptionsBase &Base) {
    BridgeFn(static_cast<const PO &>(Base));
  };
  return R;
}

/// Register a dynamically-built option into a global list that
/// OptionParser::parse() drains automatically.
LLVM_ABI void registerDynamicEntry(detail::OptionEntry E);
LLVM_ABI void registerDynamicPostParseCallback(std::function<void()> Cb);

namespace detail {
/// Hand each per-parse dynamic storage collected in \p Frame to \p Ctx,
/// transferring ownership.
LLVM_ABI void publishDynamicStorages(ParseFrame &Frame, OptionsContext &Ctx);
} // namespace detail

/// An option whose name and description are only known at runtime, e.g. one
/// flag per entry in a plugin/pass/backend registry.
///
/// `OptionInfo` stores `StringRef`, so the strings must be owned somewhere
/// stable; this owns them alongside the descriptor and the parse destination.
/// The parser keeps a pointer to the descriptor, so a RuntimeOption must
/// outlive the parse and must not move - hold it in a container with stable
/// addresses.
///
/// Prefer a constexpr `OptionInfo` whenever the name is a literal; this exists
/// only for the genuinely dynamic cases.
///
/// Takes the descriptor as a template argument rather than a pointer so it can
/// attach Opt's own OptionStaticInfo base subobject - the entry's compile-time
/// half, which every entry must have.
template <auto *Opt, typename... Args>
detail::OptionEntry makeEntry(Args &&...As) {
  detail::OptionEntry E =
      detail::buildSingleEntry(Opt, std::forward<Args>(As)...);
  E.Static = static_cast<const detail::OptionStaticInfo *>(Opt);
  return E;
}

template <typename T> class RuntimeOption {
  std::string NameStr;
  std::string DescStr;
  std::optional<OptionInfo<T>> Info;
  T Slot{};
  unsigned Count = 0;

public:
  /// Extra \p Args are forwarded to the OptionInfo constructor, so the usual
  /// modifiers all work.
  template <typename... Args>
  RuntimeOption(llvm::StringRef Name, llvm::StringRef Desc, Args &&...As)
      : NameStr(Name.str()), DescStr(Desc.str()) {
    Info.emplace(NameStr, DescStr, std::forward<Args>(As)...);
  }

  // Addresses of the members are handed to the parser as copying or moving
  // would leave it pointing at the wrong object.
  RuntimeOption(const RuntimeOption &) = delete;
  RuntimeOption &operator=(const RuntimeOption &) = delete;

  /// Build the parse entry, using this option's own occurrence counter.
  detail::OptionEntry makeEntry() {
    detail::OptionEntry E = detail::buildSingleEntry(&*Info, Slot, Count);
    E.Static = static_cast<const detail::OptionStaticInfo *>(&*Info);
    return E;
  }

  /// Build the parse entry sharing \p SharedCount with sibling options, so that
  /// a single occurrence across the group satisfies OneOrMore.
  detail::OptionEntry makeEntry(unsigned &SharedCount) {
    detail::OptionEntry E = detail::buildSingleEntry(&*Info, Slot, SharedCount);
    E.Static = static_cast<const detail::OptionStaticInfo *>(&*Info);
    return E;
  }

  /// Mutable access to this option's static half, for help-only fields that
  /// have no descriptor spelling (e.g. enum group headers).
  detail::OptionStaticInfo &staticInfo() {
    return static_cast<detail::OptionStaticInfo &>(*Info);
  }

  const T &value() const { return Slot; }
  unsigned occurrences() const { return Count; }
};

/// Register a DynamicRegistration for automatic discovery during parsing.
LLVM_ABI void registerDynamicRegistration(detail::DynamicRegistration R);

/// Build a DynamicRegistration for \p Reg.
template <const auto *Reg>
detail::DynamicRegistration makeDynamicRegistration(
    std::function<void(
        const typename std::remove_pointer_t<decltype(Reg)>::ParsedOptionsT &)>
        ApplyFn) {
  using RegT = std::remove_pointer_t<decltype(Reg)>;
  using ParsedT = typename RegT::ParsedOptionsT;

  detail::DynamicRegistration R;
  R.RegAddr = static_cast<const void *>(Reg);
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
/// If ApplyFn is provided, it is called after parsing with that parse's values.
///
/// Relying on this is discouraged; prefer to add() the registry to the
/// OptionParser explicitly, so the tool can control the order of registry
/// application and the help text.
template <const auto *Reg>
void registerDynamicRegistry(
    std::function<void(
        const typename std::remove_pointer_t<decltype(Reg)>::ParsedOptionsT &)>
        ApplyFn = nullptr) {
  registerDynamicRegistration(makeDynamicRegistration<Reg>(std::move(ApplyFn)));
}

/// Instance-scoped option parser. Holds registries, dynamic entries, and
/// hide-filter state.
class LLVM_ABI OptionParser {
public:
  OptionParser() = default;
  OptionParser(OptionParser &&) = default;
  OptionParser &operator=(OptionParser &&) = default;
  OptionParser(const OptionParser &) = delete;
  OptionParser &operator=(const OptionParser &) = delete;

  /// Add a statically-known registry with an optional bridge function.
  template <const auto *Reg> OptionParser &add() {
    Registries.push_back(typeEraseRegistry<Reg>());
    return *this;
  }

  /// Overload taking a bridge function; see typeEraseRegistryWithBridge.
  template <const auto *Reg,
            void (*BridgeFn)(const typename std::remove_pointer_t<
                             decltype(Reg)>::ParsedOptionsT &)>
  OptionParser &add() {
    Registries.push_back(typeEraseRegistryWithBridge<Reg, BridgeFn>());
    return *this;
  }

  /// Add a dynamically-built option entry (names known only at runtime).
  OptionParser &addDynamicEntry(detail::OptionEntry E) {
    assert(E.Static && "entry has no static half; build it with "
                       "clv2::makeEntry<&Opt>() or RuntimeOption::makeEntry()");
    DynamicEntries.push_back(std::move(E));
    return *this;
  }

  /// Print the same help --help would, without parsing. For tools that detect
  /// a usage error after the parse and want to show usage alongside it.
  ///
  /// Category filters set by hideUnrelatedOptions()/hideAllDynamicEntries()
  /// apply.
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
  /// only the options they need.
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
  /// Touches no global mutable state - concurrent parses are safe.
  std::unique_ptr<OptionsContext>
  parse(int argc, const char *const *argv, llvm::StringRef Overview = {},
        llvm::raw_ostream *Errs = nullptr, llvm::StringRef VersionString = {},
        llvm::raw_ostream *HelpOS = nullptr,
        std::function<void(llvm::raw_ostream &)> VersionPrinter = {});

private:
  std::vector<TypeErasedRegistry> Registries;
  std::vector<detail::OptionEntry> DynamicEntries;
  std::vector<std::unique_ptr<ParsedOptionsBase>> Storages;
  llvm::SmallVector<const OptionCategory *, 8> AllowedCategories;
  std::string ExtraHelp_;
  llvm::SmallVector<llvm::StringRef, 8> HiddenNames;
  llvm::SmallVector<llvm::StringRef, 8> ShownNames;
  bool HideUnrelated = false;
  bool DrainGlobalDynamic = false;
  bool HideAllRegistered = false;
};

template <typename EnumT, std::size_t N, typename... Args>
constexpr OptionInfo<EnumT>
makeEnumOption(llvm::StringRef Name, llvm::StringRef Desc,
               const EnumVal<EnumT> (&Vals)[N], Args &&...args) {
  return OptionInfo<EnumT>(Name, Desc, ValuesRef<EnumT>(Vals),
                           std::forward<Args>(args)...);
}

template <typename EnumT, std::size_t N, typename... Args>
constexpr ListOptionInfo<EnumT>
makeEnumListOption(llvm::StringRef Name, llvm::StringRef Desc,
                   const EnumVal<EnumT> (&Vals)[N], Args &&...args) {
  return ListOptionInfo<EnumT>(Name, Desc, ValuesRef<EnumT>(Vals),
                               std::forward<Args>(args)...);
}

/// A runtime-registered subcommand entry.
struct RuntimeSubCommandEntry {
  std::string Name;
  std::string Desc;
  /// Options scoped to this subcommand.
  std::vector<detail::OptionEntry> Options;
  /// Owned storage for option name/description strings referenced by Options.
  std::vector<std::string> OwnedStrings;
};

/// Register a runtime subcommand directly.
LLVM_ABI void registerRuntimeSubcommand(RuntimeSubCommandEntry E);

} // namespace llvm::clv2

#endif // LLVM_SUPPORT_COMMANDLINEV2_H
