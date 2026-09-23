//===--- OptParser.td - Common Option Parsing Interfaces ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the common interfaces used by the option parsing TableGen
//  backend.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OPTION_OPTPARSER_TD
#define LLVM_OPTION_OPTPARSER_TD

// Define the kinds of options.

class OptionKind<string name, int precedence = 0, bit sentinel = false> {
  string Name = name;
  // The kind precedence, kinds with lower precedence are matched first.
  int Precedence = precedence;
  // Indicate a sentinel option.
  bit Sentinel = sentinel;
}

// An option group.
def KIND_GROUP : OptionKind<"Group">;
// The input option kind.
def KIND_INPUT : OptionKind<"Input", 1, true>;
// The unknown option kind.
def KIND_UNKNOWN : OptionKind<"Unknown", 2, true>;
// A flag with no values.
def KIND_FLAG : OptionKind<"Flag">;
// An option which prefixes its (single) value.
def KIND_JOINED : OptionKind<"Joined", 1>;
// An option which is followed by its value.
def KIND_SEPARATE : OptionKind<"Separate">;
// An option followed by its values, which are separated by commas.
def KIND_COMMAJOINED : OptionKind<"CommaJoined">;
// An option which is which takes multiple (separate) arguments.
def KIND_MULTIARG : OptionKind<"MultiArg">;
// An option which is either joined to its (non-empty) value, or followed by its
// value.
def KIND_JOINED_OR_SEPARATE : OptionKind<"JoinedOrSeparate">;
// An option which is both joined to its (first) value, and followed by its
// (second) value.
def KIND_JOINED_AND_SEPARATE : OptionKind<"JoinedAndSeparate">;
// An option which consumes all remaining arguments if there are any.
def KIND_REMAINING_ARGS : OptionKind<"RemainingArgs">;
// An option which consumes an optional joined argument and any other remaining
// arguments.
def KIND_REMAINING_ARGS_JOINED : OptionKind<"RemainingArgsJoined">;

// Define the option flags.

class OptionFlag {}

// HelpHidden - The option should not be displayed in --help, even if it has
// help text. Clients *can* use this in conjunction with the OptTable::PrintHelp
// arguments to implement hidden help groups.
def HelpHidden : OptionFlag;

// RenderAsInput - The option should not render the name when rendered as an
// input (i.e., the option is rendered as values).
def RenderAsInput : OptionFlag;

// RenderJoined - The option should be rendered joined, even if separate (only
// sensible on single value separate options).
def RenderJoined : OptionFlag;

// RenderSeparate - The option should be rendered separately, even if joined
// (only sensible on joined options).
def RenderSeparate : OptionFlag;

// Define Visibility categories

class OptionVisibility {}

// Explicit specifier for default visibility
def DefaultVis : OptionVisibility;

// Define the option group class.

class OptionGroup<string name> {
  string EnumName = ?; // Uses the def name if undefined.
  string Name = name;
  string HelpText = ?;
  OptionGroup Group = ?;
  list<OptionFlag> Flags = [];
  list<OptionVisibility> Visibility = [];
}

// Define the option class.

class HelpTextVariant<list<OptionVisibility> visibilities, string text> {
  list<OptionVisibility> Visibilities = visibilities;
  string Text = text;
}

// Class definition for positional subcommands.
class SubCommand<string name, string helpText, string usage = ""> {
  string Name = name;
  string HelpText = helpText;
  string Usage = usage;
}

class Option<list<string> prefixes, string name, OptionKind kind,
             list<SubCommand> subcommands = []> {
  string EnumName = ?; // Uses the def name if undefined.
  list<string> Prefixes = prefixes;
  string Name = name;
  OptionKind Kind = kind;
  // Used by MultiArg option kind.
  int NumArgs = 0;
  string HelpText = ?;
  list<HelpTextVariant> HelpTextsForVariants = [];
  string MetaVarName = ?;
  string Values = ?;
  code ValuesCode = ?;
  list<OptionFlag> Flags = [];
  list<OptionVisibility> Visibility = [DefaultVis];
  OptionGroup Group = ?;
  Option Alias = ?;
  list<string> AliasArgs = [];
  code MacroPrefix = "";
  code KeyPath = ?;
  code DefaultValue = ?;
  code ImpliedValue = ?;
  code ImpliedCheck = "false";
  code ShouldParse = "true";
  bit ShouldAlwaysEmit = false;
  code NormalizerRetTy = ?;
  code NormalizedValuesScope = "";
  code Normalizer = "";
  code Denormalizer = "";
  list<code> NormalizedValues = ?;
  list<SubCommand> SubCommands = subcommands;
}

// Helpers for defining options.

class Flag<list<string> prefixes, string name,
           list<SubCommand> subcommands = []>
    : Option<prefixes, name, KIND_FLAG, subcommands>;
class Joined<list<string> prefixes, string name,
             list<SubCommand> subcommands = []>
    : Option<prefixes, name, KIND_JOINED, subcommands>;
class Separate<list<string> prefixes, string name,
               list<SubCommand> subcommands = []>
    : Option<prefixes, name, KIND_SEPARATE, subcommands>;
class CommaJoined<list<string> prefixes, string name,
                  list<SubCommand> subcommands = []>
    : Option<prefixes, name, KIND_COMMAJOINED, subcommands>;
class MultiArg<list<string> prefixes, string name, int numargs,
               list<SubCommand> subcommands = []>
    : Option<prefixes, name, KIND_MULTIARG, subcommands> {
  int NumArgs = numargs;
}
class JoinedOrSeparate<list<string> prefixes, string name,
                       list<SubCommand> subcommands = []>
    : Option<prefixes, name, KIND_JOINED_OR_SEPARATE, subcommands>;
class JoinedAndSeparate<list<string> prefixes, string name,
                        list<SubCommand> subcommands = []>
    : Option<prefixes, name, KIND_JOINED_AND_SEPARATE, subcommands>;

// Mix-ins for adding optional attributes.

class Alias<Option alias> { Option Alias = alias; }
class AliasArgs<list<string> aliasargs> { list<string> AliasArgs = aliasargs; }
class EnumName<string name> { string EnumName = name; }
class Flags<list<OptionFlag> flags> { list<OptionFlag> Flags = flags; }
class Visibility<list<OptionVisibility> visibility> {
  list<OptionVisibility> Visibility = visibility;
}
class Group<OptionGroup group> { OptionGroup Group = group; }
class HelpText<string text> { string HelpText = text; }
class HelpTextForVariants<list<OptionVisibility> Visibilities, string text> {
  list<HelpTextVariant> HelpTextsForVariants = [
    HelpTextVariant<Visibilities, text>
  ];
}

class MetaVarName<string name> { string MetaVarName = name; }
class Values<string value> { string Values = value; }
class ValuesCode<code valuecode> { code ValuesCode = valuecode; }

// Helpers for defining marshalling information (typically used in Clang's -cc1
// frontend).

// The key path to the mapped field and the macro prefix for the resulting
// definition database.
class KeyPathAndMacro<string key_path_prefix, string key_path_base,
                      string macro_prefix = ""> {
  code KeyPath = !strconcat(key_path_prefix, key_path_base);
  code MacroPrefix = macro_prefix;
}

// Mixin that implies the specified value for the current option when any of the
// given key paths evaluates to true.
class ImpliedByAnyOf<list<string> key_paths, code value = "true"> {
  code ImpliedCheck = !foldl("false", key_paths, accumulator, key_path,
                             !strconcat(accumulator, " || ", key_path));
  code ImpliedValue = value;
}

// Parent class for marshalled options (typically used in Clang's -cc1 frontend).
class MarshallingInfo<KeyPathAndMacro kpm, code defaultvalue> {
  code KeyPath = kpm.KeyPath;
  code MacroPrefix = kpm.MacroPrefix;
  code DefaultValue = defaultvalue;
}

// Marshalled option accepting a string argument.
class MarshallingInfoString<KeyPathAndMacro kpm, code defaultvalue="std::string()">
  : MarshallingInfo<kpm, defaultvalue> {
  code Normalizer = "normalizeString";
  code Denormalizer = "denormalizeString";
}

// Marshalled option accepting an integer argument.
class MarshallingInfoInt<KeyPathAndMacro kpm, code defaultvalue="0", code type="unsigned">
  : MarshallingInfo<kpm, defaultvalue> {
  code Normalizer = "normalizeStringIntegral<"#type#">";
  code Denormalizer = "denormalizeString<"#type#">";
}

// Marshalled option accepting vector of strings.
class MarshallingInfoStringVector<KeyPathAndMacro kpm>
  : MarshallingInfo<kpm, "std::vector<std::string>({})"> {
  code Normalizer = "normalizeStringVector";
  code Denormalizer = "denormalizeStringVector";
}

// Marshalled option - single positive flag.
class MarshallingInfoFlag<KeyPathAndMacro kpm, code defaultvalue = "false">
  : MarshallingInfo<kpm, defaultvalue> {
  code Normalizer = "normalizeSimpleFlag";
  code Denormalizer = "denormalizeSimpleFlag";
}

// Marshalled option - single negative flag.
class MarshallingInfoNegativeFlag<KeyPathAndMacro kpm, code defaultvalue = "true">
  : MarshallingInfo<kpm, defaultvalue> {
  code Normalizer = "normalizeSimpleNegativeFlag";
  code Denormalizer = "denormalizeSimpleFlag";
}

// Implementation detail of BoolOption.
class MarshallingInfoBooleanFlag<KeyPathAndMacro kpm, code defaultvalue, code value,
                                 code other_value, code other_name>
  : MarshallingInfoFlag<kpm, defaultvalue> {
  code Normalizer = "makeBooleanOptionNormalizer("#value#", "#other_value#", OPT_"#other_name#")";
  code Denormalizer = "makeBooleanOptionDenormalizer("#value#")";
}

// Marshalled option accepting any of the specified enum values.
// Typically used with `Values`, `NormalizedValues` and `NormalizedValuesScope`.
class MarshallingInfoEnum<KeyPathAndMacro kpm, code defaultvalue>
  : MarshallingInfo<kpm, defaultvalue> {
  code Normalizer = "normalizeSimpleEnum";
  code Denormalizer = "denormalizeSimpleEnum";
}

// Mixins for additional marshalling attributes.

class ShouldParseIf<code condition> { code ShouldParse = condition; }
class AlwaysEmit { bit ShouldAlwaysEmit = true; }
class Normalizer<code normalizer> { code Normalizer = normalizer; }
class Denormalizer<code denormalizer> { code Denormalizer = denormalizer; }
class NormalizedValuesScope<code scope> { code NormalizedValuesScope = scope; }
class NormalizedValues<list<code> definitions> { list<code> NormalizedValues = definitions; }

// Predefined options.

// FIXME: Have generator validate that these appear in correct position (and
// aren't duplicated).
def INPUT : Option<[], "<input>", KIND_INPUT>;
def UNKNOWN : Option<[], "<unknown>", KIND_UNKNOWN>;

// Helpers for declaring a plain C++ struct per library, generated by
// -gen-opt-parser-defs from a set of Option records tagged with Field<>. See
// llvm/utils/TableGen/OptionParserEmitter.cpp (OPTIONS_STRUCT_DECL /
// OPTIONS_STRUCT_DEFS) and llvm/include/llvm/Option/LibraryOptions.h.

// Marks a def as the struct that Field<>-tagged Option records in the same
// .td file populate. Reuses Option's existing KeyPath/DefaultValue fields
// (already present for Clang's marshalling mechanism) rather than
// introducing a parallel pair.
class OptionsStruct<string ns = "llvm"> {
  string Namespace = ns;
}

// Opt-in mixin for an OptionsStruct def: the tool has exactly one bare
// positional argument (an OPT_INPUT arg, see the pre-existing INPUT record
// below) that should be collected into a struct field named `name`, rather
// than forwarded to Rest like every other unrecognized token. The generated
// parse() records the first OPT_INPUT arg it sees into that field; any
// further positional arg is forwarded to Rest for the caller to reject.
class PositionalField<code type, code name> {
  code PositionalType = type;
  code PositionalName = name;
}

// Declares a struct member of type `type`. `name` is normally NAME (the
// enclosing multiclass's implicit def-name variable) so KeyPath ends up as
// the bare field name.
class Field<code type, code name, code default = ""> {
  code FieldType = type;
  code KeyPath = name;
  code DefaultValue = default;
}

// Caps a ValueField<>'s parsed value at an inclusive upper bound (the
// lower bound is always 0), rejecting out-of-range values at parse time
// with the same "value must be in the range [0, Max]!" diagnostic clv2's
// per-option `Validate` callback used to produce for options like this
// (e.g. UtilsOptions.td's hot/cold operator-new hint values). -1 (the
// default, see ValueField below) means "no bound"; every ValueField<>
// carries this field so the emitter can check it uniformly, but only
// options that pass a real maxValue get a check emitted.
class MaxValue<int max> { int MaxValue = max; }

class EnumMember<string name, string spelling> {
  string Name = name;
  string Spelling = spelling;
}
class OptionEnum<string name, list<EnumMember> members> {
  // C++ type reference used wherever the enum's *type* appears (struct
  // field type, the generated parse helper's `Out` parameter, member value
  // expressions). May be namespace-qualified (e.g. "cl::boolOrDefault").
  string Name = name;
  // Valid C++ identifier used to name the generated `enum class` (when not
  // External) and the `parse<Ident>` helper function. Defaults to `name`,
  // which is fine as long as `name` has no "::" in it; External enums whose
  // Name is qualified must override this with a plain identifier via
  // QualifiedEnumIdent<> below, since "::" cannot appear in a function name.
  string Ident = name;
  list<EnumMember> Members = members;
  // Set when `name` already names a real C++ enum defined elsewhere (e.g.
  // InliningAdvisorMode from InlineAdvisor.h, or cl::boolOrDefault from
  // CommandLine.h). The emitter then skips generating an `enum class name`
  // of its own -- which would otherwise collide with / duplicate the real
  // type -- while still emitting the spelling<->value parse helper.
  bit External = 0;
}

// Overrides OptionEnum's Ident for an External enum whose Name is
// namespace-qualified (e.g. Name = "cl::boolOrDefault", Ident = "BoolOrDefault").
class QualifiedEnumIdent<string ident> { string Ident = ident; }

// Tags a Field<> option whose type is one of the OptionEnum defs above, so
// the emitter can find the enum's spellings when generating apply().
class FieldEnum<OptionEnum e> { OptionEnum FieldEnum = e; }

// For a flag-shaped option (no value), assigns a fixed literal to the field
// when the flag is seen (used by the BoolField pair below: the positive
// spelling assigns "true", the paired "-no-x" spelling assigns "false").
class Assign<code name, code value> {
  code AssignMember = name;
  code AssignValue = value;
}

// Tags a Joined "-name=value" option that parses its value as a bool and
// writes it into an existing Field<>'s KeyPath, declared by a sibling def
// in the same multiclass -- contrast with Assign<>, which always writes a
// fixed literal and never reads a value. Used by BoolField below to add an
// "=true/false" spelling alongside its bare "-x"/"-no-x" flags, matching
// classic cl::opt's boolean spelling convention without redeclaring the
// same struct member twice.
class BoolAssign<code keypath> {
  code AssignKeyPath = keypath;
}

// "-name=value" (Joined: value embedded in the same argv token). Every such
// def is paired with an always-hidden "-name value" (Separate) alias, so
// both spellings populate the same field -- see Option::acceptInternal's
// SeparateClass and the Alias<> ID-rewrite mechanism that routes a matched
// Separate-form Arg into the same apply() case as the primary Joined def.
// Each occurrence (either spelling) still occupies exactly one argv token
// from the struct's own parse() perspective (two tokens for the Separate
// spelling, both consumed by that one Arg), so forwarding to Rest for what
// isn't recognized stays unambiguous.
//
// `hidden` defaults to visible (0): unlike the pilot's original blanket
// Flags<[HelpHidden]>, most options in a real tool's --help should actually
// show up there. Pass hidden = 1 for options that are deliberately
// undocumented / expert-only (clv2's per-option Hidden equivalent).
multiclass ValueField<string name, code type, code default, string help,
                      bit hidden = 0, string metaVar = "", int maxValue = -1> {
  def NAME : Joined<["-", "--"], name#"=">, HelpText<help>,
    Flags<!if(hidden, [HelpHidden], [])>, Field<type, NAME, default>,
    MetaVarName<metaVar>, MaxValue<maxValue>;
  def NAME#_sep : Separate<["-", "--"], name>, Flags<[HelpHidden]>,
    Alias<!cast<Option>(NAME)>;
}

// Like ValueField, but the field is std::optional<type>: absent means the
// option was never specified on the command line (mirrors clv2's
// wasOptSpecified<> check), rather than folding "unspecified" into an
// ordinary same-typed default value. Mirrors ValueField's own Joined+
// Separate expansion (see e.g. ARM_DataBankMask, which hand-wrote this
// shape before it was promoted to a multiclass here). No bare-flag
// spelling; for a std::optional<bool> field with one, use
// OptionalValueField instead.
multiclass OptionalField<string name, code type, string help,
                         bit hidden = 0, string metaVar = ""> {
  def NAME : Joined<["-", "--"], name#"=">, HelpText<help>,
    Flags<!if(hidden, [HelpHidden], [])>,
    Field<"std::optional<" # type # ">", NAME, "std::nullopt">,
    MetaVarName<metaVar>;
  def NAME#_sep : Separate<["-", "--"], name>, Flags<[HelpHidden]>,
    Alias<!cast<Option>(NAME)>;
}

// "-name" sets the field true; the generated "-no-name" sets it false. The
// "-no-name" spelling has no help text of its own (it's documented by the
// primary spelling), so it always stays hidden regardless of `hidden`. A
// third, always-hidden "-name=value" spelling (classic cl::opt's boolean
// convention, e.g. "-allow-unused-prefixes=false") is also accepted,
// parsing "true"/"1"/"false"/"0" via BoolAssign<> into the same field.
multiclass BoolField<string name, code default, string help, bit hidden = 0> {
  def NAME : Flag<["-", "--"], name>, HelpText<help>,
    Flags<!if(hidden, [HelpHidden], [])>, Field<"bool", NAME, default>,
    Assign<NAME, "true">;
  def NAME#_no : Flag<["-", "--"], "no-"#name>, Flags<[HelpHidden]>,
    Assign<NAME, "false">;
  def NAME#_eq : Joined<["-", "--"], name#"=">, Flags<[HelpHidden]>,
    BoolAssign<NAME>;
}

multiclass EnumField<string name, OptionEnum e, code default, string help,
                     bit hidden = 0, string metaVar = ""> {
  def NAME : Joined<["-", "--"], name#"=">, HelpText<help>,
    Flags<!if(hidden, [HelpHidden], [])>, Field<e.Name, NAME, default>,
    FieldEnum<e>, MetaVarName<metaVar>;
  def NAME#_sep : Separate<["-", "--"], name>, Flags<[HelpHidden]>,
    Alias<!cast<Option>(NAME)>;
}

// Tags a Field<> option whose value is accumulated into a std::vector<T>
// rather than assigned. `elementType` is the vector's element type.
// `commaSplit` requests that the emitted apply() split each matched Arg's
// single raw value on commas, keeping empty entries (matching classic
// cl::opt's CommaSeparated handling in CommandLine.cpp, not
// Option::acceptInternal's CommaJoinedClass, which discards them -- some
// consumers, e.g. FileCheck's check/comment-prefix validation, reject an
// empty entry explicitly downstream and need it to survive the split)
// before parsing and pushing each piece. This splitting is done in emitted
// code rather than by
// OptTable's core matching (i.e. the def itself is always plain Joined/
// JoinedOrSeparate, never CommaJoined) so that both the Joined
// ("-name=v1,v2") and Separate ("-name v1,v2") spellings of the same field,
// which each hand apply() exactly one raw string, produce identical results.
// `alwaysPrefix` marks a clv2 AlwaysPrefixFormat-style field (appendEquals =
// 0 in ListField/EnumListField below, e.g. "-Dfoo=bar"): the value must be
// attached to the same token, and a bare token with nothing attached (e.g.
// plain "-D") is a hard "requires a value!" error, not an empty value nor a
// prompt to consume the next token -- see Option::acceptInternal's
// JoinedClass (always matches, never steals a following token) and
// clv2::detail::isAlwaysPrefix()'s matching behavior in CommandLineV2.cpp.
class ListElement<code elementType, bit commaSplit = 0,
                  bit alwaysPrefix = 0> {
  code ElementType = elementType;
  bit CommaSplit = commaSplit;
  bit AlwaysPrefix = alwaysPrefix;
}

// "-name=v1,v2,..." (commaJoined = 1, the default: the emitted apply() code
// splits the matched value on commas, keeping empty entries -- see
// ListElement above) or plain "-name=v"/"-nameV" repeated once per value
// (commaJoined = 0; pass appendEquals = 0 for clv2's AlwaysPrefixFormat
// spelling, e.g. "-Dfoo=bar" with no "=" between the option name and the
// value). Either way the field is std::vector<elementType>; repeated
// occurrences append rather than overwrite, matching clv2 ListOptionInfo's
// accumulate-in-argv-order semantics.
//
// Also accepts the "-name value" (Separate) spelling, but only when
// appendEquals = 1 (the option's Joined spelling embeds "=" as the value
// separator): a second, always-hidden Alias<> def with a bare Name -- same
// mechanism as ValueField/EnumField above, and unambiguous because "name"
// and "name=" are different literal spellings. When appendEquals = 0 (clv2's
// AlwaysPrefixFormat, e.g. "-Dfoo=bar", no separator between the option name
// and the value), there is deliberately no Separate spelling at all -- see
// ListElement's AlwaysPrefix above -- so the primary def stays plain Joined.
multiclass ListField<string name, code elementType, string help,
                     bit commaJoined = 1, bit appendEquals = 1,
                     bit hidden = 0, string metaVar = ""> {
  def NAME : Option<["-", "--"], !if(appendEquals, name#"=", name),
                    KIND_JOINED>,
    HelpText<help>, Flags<!if(hidden, [HelpHidden], [])>,
    Field<"std::vector<" # elementType # ">", NAME, "{}">,
    ListElement<elementType, commaJoined, !not(appendEquals)>,
    MetaVarName<metaVar>;
  if appendEquals then
  def NAME#_sep : Separate<["-", "--"], name>, Flags<[HelpHidden]>,
    Alias<!cast<Option>(NAME)>;
}

// Same shape as ListField, but each value is parsed through an OptionEnum
// (see FieldEnum<> below) instead of a plain scalar type.
multiclass EnumListField<string name, OptionEnum e, string help,
                         bit commaJoined = 0, bit appendEquals = 1,
                         bit hidden = 0, string metaVar = ""> {
  def NAME : Option<["-", "--"], !if(appendEquals, name#"=", name),
                    KIND_JOINED>,
    HelpText<help>, Flags<!if(hidden, [HelpHidden], [])>,
    Field<"std::vector<" # e.Name # ">", NAME, "{}">,
    ListElement<e.Name, commaJoined, !not(appendEquals)>, FieldEnum<e>,
    MetaVarName<metaVar>;
  if appendEquals then
  def NAME#_sep : Separate<["-", "--"], name>, Flags<[HelpHidden]>,
    Alias<!cast<Option>(NAME)>;
}

// "-name" (bare, no value) or "-name=value" (Joined), both writing the same
// struct field. Deliberately has no "-name value" (Separate) convenience
// spelling: unlike ValueField/EnumField/ListField/EnumListField above, the
// bare "-name" spelling here already means something (the no-value case),
// so a Separate def would need the exact same bare Name and would collide
// with (and be made unreachable by) the existing NAME#_bare Flag def below.
// The field is std::optional<type>: absent means the flag was
// never passed; present means it was, with `bareValue` recorded for the
// no-value spelling. Mirrors clv2's ValueOptional semantics -- e.g. a bare
// "-print-pipeline-passes" vs "-print-pipeline-passes=tree" -- without a
// dedicated emitter feature: the bare spelling only carries an Assign<>,
// reusing the same "two defs, one KeyPath" idiom BoolField uses for its
// paired "-no-x" spelling. Like "-no-x", the bare spelling stays hidden
// unconditionally -- it's documented by the primary spelling's help text.
multiclass OptionalValueField<string name, code type, code bareValue,
                              string help, bit hidden = 0,
                              string metaVar = ""> {
  def NAME : Joined<["-", "--"], name#"=">, HelpText<help>,
    Flags<!if(hidden, [HelpHidden], [])>,
    Field<"std::optional<" # type # ">", NAME, "std::nullopt">,
    MetaVarName<metaVar>;
  def NAME#_bare : Flag<["-", "--"], name>, Flags<[HelpHidden]>,
    Assign<NAME, bareValue>;
}

#endif // LLVM_OPTION_OPTPARSER_TD
