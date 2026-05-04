//===- CLV2Options.td - Base classes for clv2 option generation -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// TableGen base class definitions for generating clv2::OptionInfo declarations,
// OptionsRegistry instances, OptionRegistryOf trait specializations, and
// typed getter functions.
//
// Usage:
//   include "llvm/Support/CLV2Options.td"
//   def MyOptsReg : Registry<"my_opts">;
//   def MyFlag : Option<"my-flag", "Enable my feature", "bool"> {
//     let Registry = MyOptsReg;
//     let Default = "true";
//   }
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Option categories
//===----------------------------------------------------------------------===//

class OptionCategory<string name, string desc = ""> {
  string Name = name;
  string Description = desc;
}

//===----------------------------------------------------------------------===//
// Context types — define how to reach OptionsContext from a domain object
//===----------------------------------------------------------------------===//

class ContextType<string typeName, string accessor> {
  string TypeName = typeName;
  // Expression to get OptionsContext* from a parameter named '$'.
  // '$' is replaced with the actual parameter name.
  string AccessorExpr = accessor;
}

def CTX_Function : ContextType<"llvm::Function",
    "$.getContext().getOptionsContext()">;

def CTX_Module : ContextType<"llvm::Module",
    "$.getContext().getOptionsContext()">;

def CTX_MachineFunction : ContextType<"llvm::MachineFunction",
    "$.getFunction().getContext().getOptionsContext()">;

//===----------------------------------------------------------------------===//
// Registries — options declare membership via `let Registry = ...`
//===----------------------------------------------------------------------===//

class Registry<string ns> {
  string Namespace = ns;
  // Getter overloads to generate per option. Always includes OptionsContext*.
  list<ContextType> ContextTypes = [CTX_Function];

  // Emit getters as declarations in the header and definitions in a single
  // translation unit, instead of `inline` definitions in the header.
  //
  // Header-inline getters are the dominant build-time cost: every TU including
  // a generated header parses every getter and eagerly instantiates
  // getOptValOr<> -> ParsedOptions<N> -> index_of_v<N> for each, even though
  // most are never called anywhere.
  //
  // With this set, the owning library must compile one source file that does:
  //   #include "<Registry>OptInfos.h"
  //   #define CLV2_OPTIONS_GETTER_DEFS
  //   #include "<Registry>OptInfos.inc"
  bit OutOfLineGetters = 0;

  // Export annotation for out-of-line getter declarations (e.g. "LLVM_ABI").
  // Only needed when a getter is called from outside the library that compiles
  // the definitions.  Ignored unless OutOfLineGetters is set.
  string ExportMacro = "";
}

//===----------------------------------------------------------------------===//
// Option base — shared fields for all options
//===----------------------------------------------------------------------===//

class OptionBase<string name, string desc, string type> {
  string CLIName = name;
  string Description = desc;
  string CppType = type;
  string Default = "";        // Raw C++ expression; empty = no Init{}

  // Registry membership (required)
  Registry Registry = ?;

  // Optional category
  OptionCategory Category = ?;

  // C++ name overrides (auto-derived if empty)
  string CppPrefix = "";     // override prefix (default: from Registry)
  string CppSuffix = "";     // override suffix (default: CamelCase of CLIName)
  // Override the generated getter name.  The default is "get" + the CLI name
  // camel-cased, which collapses hyphens -- so "foo-bar" and "fooBar" both
  // yield getFooBar.  Two options differing only in hyphenation therefore
  // need an explicit GetterName on one of them; the collision is a C++
  // redefinition error, so it fails loudly at build time.
  string GetterName = "";

  // Visibility
  bit Hidden = 0;
  bit ReallyHidden = 0;

  // Help formatting
  string ValueDesc = "";

  // Post-parse validation.  Validate is the *body* of a function with the
  // signature
  //
  //   bool (const CppType &Value, StringRef OptName, ParseDiag &Diag)
  //
  // returning true to accept.  To reject, call reject(), which is in scope and
  // always returns false:
  //
  //   bool reject(const Twine &Msg)
  //
  // reject() reports "<prog>: for the --<opt> option: <msg>".  The body
  // composes the message, so it can quote the whole value, name one offending
  // token inside a list, or enumerate the legal choices.
  //
  //   let Validate = [{
  //     if (Value > 100)
  //       return reject("'" + Twine(Value) + "' value must be in the "
  //                     "range [0, 100]!");
  //     return true;
  //   }];
  //
  // The body must be self-contained: this descriptor is a header-inline
  // constexpr, so the descriptor -- and therefore anything the body calls -- is
  // odr-used by every TU that reads the option, including tools that do not
  // link the defining library.  Calling out to a target-internal helper links
  // fine in that target's own tools and fails everywhere else (clang-format,
  // clangd, mlir-minimal-opt all pull in target option headers).  An option
  // whose valid set is known only to one library needs its descriptor
  // hand-written in that library's TU instead -- see OI_RegAlloc in
  // TargetPassConfig.cpp, where descriptor and validator sit together.
  //
  // The check runs once per occurrence and reports through the parser's normal
  // diagnostic path, so it honours OnError -- unlike a hand-written apply
  // function calling exit(), which kills the host process.
  code Validate = [{}];

  // Parsing modifiers.  These must cover every modifier that changes how
  // an argument is *parsed* -- a missing one is silently dropped at migration
  // and only shows up as a tool rejecting a command line it used to accept.
  bit Positional = 0;
  bit Required = 0;
  bit CommaSeparated = 0;
  bit ValueOptional = 0;
  bit ValueRequired = 0;
  bit ValueDisallowed = 0;

  // Occurrence counts (mutually exclusive with Required).
  bit ZeroOrMore = 0;
  bit OneOrMore = 0;
  /// Swallow every remaining argument, e.g. args forwarded to a subprocess.
  bit ConsumeAfter = 0;

  // Formatting: -Ipath (value glued to the name) rather than -I path.
  bit Prefix = 0;
  bit AlwaysPrefix = 0;

  // Misc parsing behaviour.
  bit Sink = 0;               // accept otherwise-unknown options
  bit Grouping = 0;           // -abc means -a -b -c
  bit PositionalEatsArgs = 0; // positional list also takes -flags
  bit DefaultOption = 0;      // applied before explicit args
}

//===----------------------------------------------------------------------===//
// Enum support
//===----------------------------------------------------------------------===//

class EnumValue<string name, string desc = ""> {
  string Name = name;
  string Description = desc;
  string CppName = "";  // Override C++ enum member name (auto-derived if empty)
  int Value = -9999;    // -9999 = auto-assign sequential from 0
  bit Sentinel = 0;     // If 1, include in enum class but NOT in EnumVal CLI array
  bit AliasOnly = 0;    // If 1, include in EnumVal CLI array but NOT in enum class
}

class EnumType<string name, list<EnumValue> values> {
  string Name = name;
  list<EnumValue> Values = values;
  bit External = 0;  // If 1, enum class is defined elsewhere — only emit EnumVal array
}

//===----------------------------------------------------------------------===//
// Option, ListOption, BitsOption — the three option classes
//
// CppType is a raw C++ type string (e.g., "bool", "unsigned", "std::string").
// When CppType matches an EnumType's Name, the emitter uses enum emission.
//
// Default is a raw C++ expression (e.g., "true", "42u", "1.5f").
// For enum options, Default is the CLI name (e.g., "none") which the emitter
// resolves to the C++ enum value via the EnumType's values.
// If Default is empty, no Init{} is emitted.
//===----------------------------------------------------------------------===//

class Option<string name, string desc, string type>
    : OptionBase<name, desc, type>;

class ListOption<string name, string desc, string type>
    : OptionBase<name, desc, type>;

class BitsOption<string name, string desc, string type>
    : OptionBase<name, desc, type>;

//===----------------------------------------------------------------------===//
// Aliases
//===----------------------------------------------------------------------===//

class Alias<string name, string target, string desc = ""> {
  string CLIName = name;
  string AliasFor = target;     // CLIName of the target option
  string Description = desc;
  bit Hidden = 0;
  Registry Registry = ?;
}

//===----------------------------------------------------------------------===//
// Subcommands — reference options, many-to-many relationship
//===----------------------------------------------------------------------===//

class SubCommand<string name, string desc> {
  string Name = name;
  string Description = desc;
  list<OptionBase> Options = [];
}
