//===- CLV2OptionsEmitter.cpp - Generate clv2 option declarations -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This TableGen backend emits llvm::clv2::OptionInfo declarations,
// OptionsRegistry instances, OptionRegistryOf trait specializations, and typed
// getter functions from .td descriptions.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <algorithm>
#include <string>
#include <vector>

#define DEBUG_TYPE "clv2-options-emitter"

using namespace llvm;

namespace {

/// Escape a string for use inside a C++ string literal.
static std::string escapeString(StringRef S) {
  std::string Result;
  for (char C : S) {
    switch (C) {
    case '\n':
      Result += "\\n";
      break;
    case '\r':
      Result += "\\r";
      break;
    case '\t':
      Result += "\\t";
      break;
    case '\\':
      Result += "\\\\";
      break;
    case '"':
      Result += "\\\"";
      break;
    default:
      // Anything outside printable ASCII is emitted as an octal escape.
      // Deliberately not \xNN: a hex escape consumes unboundedly many hex
      // digits, so an escaped byte followed by [0-9a-fA-F] would merge into
      // one oversized escape ("\x01abc" is a single character, not four).
      // \NNN caps at three digits and so self-terminates.
      if (static_cast<unsigned char>(C) < 0x20 ||
          static_cast<unsigned char>(C) >= 0x7f) {
        char Buf[5];
        snprintf(Buf, sizeof(Buf), "\\%03o", static_cast<unsigned char>(C));
        Result += Buf;
        break;
      }
      Result += C;
      break;
    }
  }
  return Result;
}

/// Convert kebab-case CLI name to CamelCase C++ identifier.
/// "adce-remove-control-flow" → "AdceRemoveControlFlow"
static std::string toCamelCase(StringRef KebabName) {
  std::string Result;
  bool CapNext = true;
  for (char C : KebabName) {
    if (C == '-') {
      CapNext = true;
    } else {
      Result += CapNext ? llvm::toUpper(C) : C;
      CapNext = false;
    }
  }
  return Result;
}

/// Get the C++ variable name for an option.
/// Uses the .td record name (def name) directly — it IS the C++ variable name.
static std::string getCppName(const Record *Opt) {
  return std::string(Opt->getName());
}

/// Get the getter function name for an option.
static std::string getGetterName(const Record *Opt) {
  std::string Override = std::string(Opt->getValueAsString("GetterName"));
  if (!Override.empty())
    return Override;
  return "get" + toCamelCase(Opt->getValueAsString("CLIName"));
}

/// Two options whose CLI names differ only by hyphenation derive the same
/// getter, because toCamelCase drops '-': "foo-bar" and "fooBar" both give
/// getFooBar.  Left alone that surfaces as a redefinition error inside
/// generated code, which points at the .inc rather than at the .td.  Diagnose
/// it here, where both records can be named.
static void
checkGetterNameCollisions(const std::vector<const Record *> &Registries,
                          const std::vector<const Record *> &Options) {
  // Getters are emitted into the registry's namespace, so only options sharing
  // a namespace can collide.  Distinct registries may share one.
  DenseMap<const Record *, StringRef> NSOf;
  for (const Record *Reg : Registries)
    NSOf[Reg] = Reg->getValueAsString("Namespace");

  StringMap<const Record *> Seen;
  bool Failed = false;
  for (const Record *Opt : Options) {
    const Record *Reg = Opt->getValueAsDef("Registry");
    std::string Key = (NSOf.lookup(Reg) + "::" + getGetterName(Opt)).str();
    auto It = Seen.find(Key);
    if (It == Seen.end()) {
      Seen[Key] = Opt;
      continue;
    }
    const Record *Prev = It->second;
    PrintError(Opt->getLoc(), "getter name '" + Key +
                                  "' collides with option '" +
                                  Prev->getValueAsString("CLIName") +
                                  "'; set GetterName on one of them");
    PrintNote(Prev->getLoc(), "previous option with this getter is here");
    Failed = true;
  }
  if (Failed)
    PrintFatalError("duplicate clv2 getter name");
}

using EnumTypeMap = StringMap<const Record *>;

/// Read the C++ type directly from the CppType field.
static std::string getCppType(const Record *Opt) {
  return std::string(Opt->getValueAsString("CppType"));
}

/// Read the default value directly from the Default field.
/// For enum options, resolves CLI name to C++ via the EnumType.
static std::string getDefaultExpr(const Record *Opt,
                                  const EnumTypeMap &EnumTypes) {
  std::string DefVal = std::string(Opt->getValueAsString("Default"));
  if (DefVal.empty())
    return "";

  // If this is an enum type, resolve CLI name → C++ enum value
  std::string CppType = getCppType(Opt);
  auto It = EnumTypes.find(CppType);
  if (It != EnumTypes.end()) {
    const Record *ET = It->second;
    std::string EnumName = std::string(ET->getValueAsString("Name"));
    for (const auto *V : ET->getValueAsListOfDefs("Values")) {
      if (V->getValueAsString("Name") == DefVal) {
        StringRef CppOverride = V->getValueAsString("CppName");
        std::string CppVal = CppOverride.empty() ? toCamelCase(DefVal)
                                                 : std::string(CppOverride);
        // If the C++ value is a numeric literal, don't prefix with TypeName::
        if (!CppVal.empty() && (isdigit(CppVal[0]) || CppVal[0] == '-'))
          return CppVal;
        return EnumName + "::" + CppVal;
      }
    }
    // Default didn't match any CLI name — treat as raw C++ expression
    return DefVal;
  }

  return DefVal;
}

/// Check if an option is a list type.
static bool isListOption(const Record *Opt) {
  return Opt->isSubClassOf("ListOption");
}

/// Check if an option is a bits/flags type.
static bool isBitsOption(const Record *Opt) {
  return Opt->isSubClassOf("BitsOption");
}

/// Check if an option's CppType matches a known EnumType.
static bool isEnumOption(const Record *Opt, const EnumTypeMap &EnumTypes) {
  return EnumTypes.count(Opt->getValueAsString("CppType"));
}

/// Check if an option uses an external enum type (defined in another header).
static bool isExternalEnum(const Record *Opt, const EnumTypeMap &EnumTypes) {
  auto It = EnumTypes.find(Opt->getValueAsString("CppType"));
  return It != EnumTypes.end() && It->second->getValueAsBit("External");
}

class CLV2OptionsEmitter {
private:
  const RecordKeeper &Records;

public:
  CLV2OptionsEmitter(const RecordKeeper &RK) : Records(RK) {}

  void run(raw_ostream &OS);

private:
  void emitEnumTypes(raw_ostream &OS);
  void emitOptionDecls(raw_ostream &OS,
                       const std::vector<const Record *> &Options,
                       const EnumTypeMap &EnumTypes);
  void emitTraitSpecs(raw_ostream &OS,
                      const std::vector<const Record *> &Options);
  void emitRegistries(raw_ostream &OS,
                      const std::vector<const Record *> &Registries,
                      const std::vector<const Record *> &Options,
                      const std::vector<const Record *> &Aliases);
  void emitAliases(raw_ostream &OS, const std::vector<const Record *> &Aliases);
  /// How a registry's getters are written out.
  ///   Inline — `inline` definition in the header (the default, unchanged).
  ///   Decl   — declaration only; the body is emitted once elsewhere.
  ///   Def    — the out-of-line definition, for the single owning TU.
  enum class GetterEmit { Inline, Decl, Def };

  void emitGetters(raw_ostream &OS,
                   const std::vector<const Record *> &Registries,
                   const std::vector<const Record *> &Options,
                   const EnumTypeMap &EnumTypes, bool CtxOnly, GetterEmit Mode);
};

} // anonymous namespace

void CLV2OptionsEmitter::run(raw_ostream &OS) {
  emitSourceFileHeader("clv2 Option Declarations", OS);

  auto Options = Records.getAllDerivedDefinitionsIfDefined("OptionBase");
  auto Registries = Records.getAllDerivedDefinitionsIfDefined("Registry");
  auto Aliases = Records.getAllDerivedDefinitionsIfDefined("Alias");

  // Build enum type lookup map: EnumType.Name → Record*
  EnumTypeMap EnumTypes;
  for (const auto *ET : Records.getAllDerivedDefinitionsIfDefined("EnumType"))
    EnumTypes[ET->getValueAsString("Name")] = ET;

  checkGetterNameCollisions(Registries, Options);

  OS << "#ifdef CLV2_OPTIONS_DECL\n\n";
  OS << "namespace llvm::clv2 {\n\n";

  // 1. Emit enum types
  emitEnumTypes(OS);

  // 2. Emit option categories (guarded to avoid redefinition when multiple
  //    .inc files share categories via a common .td include)
  for (const auto *Cat :
       Records.getAllDerivedDefinitionsIfDefined("OptionCategory")) {
    StringRef Name = Cat->getName();
    StringRef CatName = Cat->getValueAsString("Name");
    StringRef CatDesc = Cat->getValueAsString("Description");
    // OptionCategory::Name is the section header printed by --help (matching
    // Prefer the human-readable Description for it and
    // fall back to the TableGen record name when no description is given.
    StringRef Header = CatDesc.empty() ? CatName : CatDesc;
    OS << "#ifndef CLV2_CAT_" << Name << "\n";
    OS << "#define CLV2_CAT_" << Name << "\n";
    OS << "inline constexpr OptionCategory " << Name << "{\""
       << escapeString(Header) << "\"};\n";
    OS << "#endif\n";
  }
  if (!Records.getAllDerivedDefinitionsIfDefined("OptionCategory").empty())
    OS << "\n";

  // 3. Emit option declarations
  emitOptionDecls(OS, Options, EnumTypes);

  // 4. Emit alias declarations (before registries, since registries reference
  // them)
  emitAliases(OS, Aliases);

  // 5. Emit registries (auto-collecting options by Registry field)
  emitRegistries(OS, Registries, Options, Aliases);

  // 6. Emit OptionRegistryOf trait specializations
  emitTraitSpecs(OS, Options);

  OS << "} // namespace llvm::clv2\n\n";

  // 7. Emit OptionsContext*-only getters (safe with forward declarations)
  emitGetters(OS, Registries, Options, EnumTypes, /*CtxOnly=*/true,
              GetterEmit::Inline);
  emitGetters(OS, Registries, Options, EnumTypes, /*CtxOnly=*/true,
              GetterEmit::Decl);

  OS << "#endif // CLV2_OPTIONS_DECL\n\n";

  // 8. Emit context-type overload getters (need full type definitions)
  OS << "#ifdef CLV2_OPTIONS_GETTERS\n\n";
  emitGetters(OS, Registries, Options, EnumTypes, /*CtxOnly=*/false,
              GetterEmit::Inline);
  emitGetters(OS, Registries, Options, EnumTypes, /*CtxOnly=*/false,
              GetterEmit::Decl);
  OS << "#endif // CLV2_OPTIONS_GETTERS\n\n";

  // 9. Out-of-line getter definitions, for registries that opted in via
  //    Registry::OutOfLineGetters.  Exactly one TU per registry defines
  //    CLV2_OPTIONS_GETTER_DEFS; everyone else sees only the declarations
  //    emitted above and does not pay to instantiate 100s of getter bodies.
  OS << "#ifdef CLV2_OPTIONS_GETTER_DEFS\n\n";
  emitGetters(OS, Registries, Options, EnumTypes, /*CtxOnly=*/true,
              GetterEmit::Def);
  emitGetters(OS, Registries, Options, EnumTypes, /*CtxOnly=*/false,
              GetterEmit::Def);
  OS << "#endif // CLV2_OPTIONS_GETTER_DEFS\n";
}

void CLV2OptionsEmitter::emitEnumTypes(raw_ostream &OS) {
  for (const auto *ET : Records.getAllDerivedDefinitionsIfDefined("EnumType")) {
    StringRef Name = ET->getValueAsString("Name");
    auto Values = ET->getValueAsListOfDefs("Values");

    bool IsExternal = ET->getValueAsBit("External");

    // Emit C++ enum (skip for external types — defined in another header)
    if (!IsExternal) {
      OS << "enum class " << Name << " {\n";
      int AutoVal = 0;
      for (const auto *V : Values) {
        if (V->getValueAsBit("AliasOnly"))
          continue; // CLI alias only — not a distinct enum member
        StringRef VName = V->getValueAsString("Name");
        StringRef CppNameOverride = V->getValueAsString("CppName");
        int64_t Explicit = V->getValueAsInt("Value");
        std::string CppVal = CppNameOverride.empty()
                                 ? toCamelCase(VName)
                                 : std::string(CppNameOverride);
        if (Explicit != -9999)
          OS << "  " << CppVal << " = " << Explicit << ",\n";
        else
          OS << "  " << CppVal << " = " << AutoVal << ",\n";
        AutoVal = (Explicit != -9999 ? Explicit : AutoVal) + 1;
      }
      OS << "};\n\n";
    }

    // Emit EnumVal array (always, even for external types)
    // Use the def name for the array to avoid issues with :: in external type
    // names Skip sentinel values (Sentinel = 1) — they're in the enum class but
    // not CLI-selectable
    StringRef DefName = ET->getName();
    OS << "inline constexpr EnumVal<" << Name << "> " << DefName
       << "Vals[] = {\n";
    for (const auto *V : Values) {
      if (V->getValueAsBit("Sentinel"))
        continue;
      StringRef VName = V->getValueAsString("Name");
      StringRef VDesc = V->getValueAsString("Description");
      StringRef CppNameOverride = V->getValueAsString("CppName");
      std::string CppVal = CppNameOverride.empty()
                               ? toCamelCase(VName)
                               : std::string(CppNameOverride);
      // For external enums whose CppName is a numeric literal (e.g., "0"),
      // don't prefix with TypeName:: since int::0 is invalid C++.
      bool IsLiteral =
          !CppVal.empty() && (isdigit(CppVal[0]) || CppVal[0] == '-');
      if (IsLiteral)
        OS << "    {\"" << escapeString(VName) << "\", " << CppVal << ", \""
           << escapeString(VDesc) << "\"},\n";
      else
        OS << "    {\"" << escapeString(VName) << "\", " << Name
           << "::" << CppVal << ", \"" << escapeString(VDesc) << "\"},\n";
    }
    OS << "};\n\n";
  }
}

void CLV2OptionsEmitter::emitOptionDecls(
    raw_ostream &OS, const std::vector<const Record *> &Options,
    const EnumTypeMap &EnumTypes) {
  for (const auto *Opt : Options) {
    std::string Name = getCppName(Opt);
    StringRef CLIName = Opt->getValueAsString("CLIName");
    StringRef Desc = Opt->getValueAsString("Description");
    std::string CppType = getCppType(Opt);
    std::string DefaultVal = getDefaultExpr(Opt, EnumTypes);
    bool IsHidden = Opt->getValueAsBit("Hidden");
    bool IsReallyHidden = Opt->getValueAsBit("ReallyHidden");
    bool IsPositional = Opt->getValueAsBit("Positional");
    bool IsRequired = Opt->getValueAsBit("Required");
    bool IsCommaSeparated = Opt->getValueAsBit("CommaSeparated");
    bool IsValueOptional = Opt->getValueAsBit("ValueOptional");
    bool IsValueRequired = Opt->getValueAsBit("ValueRequired");
    bool IsValueDisallowed = Opt->getValueAsBit("ValueDisallowed");
    bool IsZeroOrMore = Opt->getValueAsBit("ZeroOrMore");
    bool IsOneOrMore = Opt->getValueAsBit("OneOrMore");
    bool IsConsumeAfter = Opt->getValueAsBit("ConsumeAfter");
    bool IsPrefix = Opt->getValueAsBit("Prefix");
    bool IsAlwaysPrefix = Opt->getValueAsBit("AlwaysPrefix");
    bool IsSink = Opt->getValueAsBit("Sink");
    bool IsGrouping = Opt->getValueAsBit("Grouping");
    bool IsPositionalEatsArgs = Opt->getValueAsBit("PositionalEatsArgs");
    bool IsDefaultOption = Opt->getValueAsBit("DefaultOption");
    std::string ValDesc = std::string(Opt->getValueAsString("ValueDesc"));
    std::string ValidateBody = std::string(Opt->getValueAsString("Validate"));
    bool IsList = isListOption(Opt);
    bool IsEnum = isEnumOption(Opt, EnumTypes);

    const RecordVal *CatRV = Opt->getValue("Category");
    bool HasCat =
        CatRV && CatRV->getValue() && !isa<UnsetInit>(CatRV->getValue());

    bool IsBits = isBitsOption(Opt);

    // Post-parse validation.  The .td supplies the body of a function whose
    // signature is fixed here; reject() is a lambda over that signature's
    // OptName/Diag so the body only has to compose a message.  Emitted just
    // above the descriptor so its address is available in the constexpr
    // initialiser, and `inline` rather than `static` because this is a header
    // included by many TUs.
    bool HasValidate = !ValidateBody.empty();
    if (HasValidate) {
      if (IsBits)
        PrintFatalError(Opt->getLoc(),
                        "Validate is not supported on bits options");
      OS << "inline bool validate_" << Name << "(const " << CppType
         << " &Value, llvm::StringRef OptName,\n"
         << "                     clv2::detail::ParseDiag &Diag) {\n";
      OS << "  [[maybe_unused]] auto reject = [&](const llvm::Twine &Msg) {\n";
      OS << "    return clv2::detail::rejectOptionValue(OptName, Msg, Diag);\n";
      OS << "  };\n";
      OS << ValidateBody;
      OS << "\n}\n";
    }

    if (IsBits) {
      // Bits/flags option — BitsOptionInfo<EnumType> with vals array
      const Record *ET = EnumTypes.lookup(CppType);
      std::string ValsName =
          ET ? std::string(ET->getName()) + "Vals" : CppType + "Vals";
      OS << "inline constexpr BitsOptionInfo<" << CppType << "> " << Name
         << "{\n";
      OS << "    \"" << escapeString(CLIName) << "\", \"" << escapeString(Desc)
         << "\",\n";
      OS << "    " << ValsName;
    } else if (IsEnum && !IsList) {
      const Record *ET = EnumTypes.lookup(CppType);
      OS << "inline constexpr auto " << Name << " =\n";
      OS << "    makeEnumOption<" << CppType << ">(\n";
      OS << "        \"" << escapeString(CLIName) << "\", \""
         << escapeString(Desc) << "\",\n";
      OS << "        " << ET->getName() << "Vals";
    } else if (IsEnum && IsList) {
      const Record *ET = EnumTypes.lookup(CppType);
      OS << "inline constexpr auto " << Name << " =\n";
      OS << "    makeEnumListOption<" << CppType << ">(\n";
      OS << "        \"" << escapeString(CLIName) << "\", \""
         << escapeString(Desc) << "\",\n";
      OS << "        " << ET->getName() << "Vals";
    } else if (IsList) {
      OS << "inline constexpr ListOptionInfo<" << CppType << "> " << Name
         << "{\n";
      OS << "    \"" << escapeString(CLIName) << "\", \"" << escapeString(Desc)
         << "\"";
    } else {
      OS << "inline constexpr OptionInfo<" << CppType << "> " << Name << "{\n";
      OS << "    \"" << escapeString(CLIName) << "\", \"" << escapeString(Desc)
         << "\"";
    }

    if (HasValidate)
      OS << ",\n    Validate<" << CppType << ">{&validate_" << Name << "}";
    if (!DefaultVal.empty())
      OS << ",\n    Init{" << DefaultVal << "}";
    if (IsHidden)
      OS << ", Hidden";
    if (IsReallyHidden)
      OS << ", ReallyHidden";
    if (IsPositional)
      OS << ", Positional{}";
    if (IsRequired)
      OS << ", Required";
    if (IsCommaSeparated)
      OS << ", CommaSeparated";
    if (IsValueOptional)
      OS << ", ValueOptional";
    if (IsValueRequired)
      OS << ", ValueRequired";
    if (IsValueDisallowed)
      OS << ", ValueDisallowed";
    if (IsZeroOrMore)
      OS << ", ZeroOrMore";
    if (IsOneOrMore)
      OS << ", OneOrMore";
    if (IsConsumeAfter)
      OS << ", ConsumeAfter";
    if (IsAlwaysPrefix)
      OS << ", AlwaysPrefixFormat";
    else if (IsPrefix)
      OS << ", PrefixFormat";
    if (IsSink)
      OS << ", Sink";
    if (IsGrouping)
      OS << ", Grouping";
    if (IsPositionalEatsArgs)
      OS << ", PositionalEatsArgs";
    if (IsDefaultOption)
      OS << ", DefaultOption";
    if (!ValDesc.empty())
      OS << ", value_desc(\"" << escapeString(ValDesc) << "\")";
    if (HasCat) {
      const Record *Cat = Opt->getValueAsDef("Category");
      OS << ", cat(" << Cat->getName() << ")";
    }

    OS << ((IsEnum && !IsBits) ? ")" : "}") << ";\n\n";
  }
}

void CLV2OptionsEmitter::emitTraitSpecs(
    raw_ostream &OS, const std::vector<const Record *> &Options) {
  // Position of each option within its own registry's pack.  emitRegistries()
  // writes the OptionsRegistry<...> arguments by walking Options in this same
  // order and keeping only that registry's members, then appends aliases -- so
  // counting per registry here reproduces the option indices exactly.  The
  // accessors in OptionsContext.h use Index to skip index_of_v, whose scan of
  // the whole pack is quadratic when done once per option.
  DenseMap<const Record *, std::size_t> NextIndex;

  for (const auto *Opt : Options) {
    std::string Name = getCppName(Opt);
    const Record *Reg = Opt->getValueAsDef("Registry");
    StringRef RegName = Reg->getName();
    std::size_t Index = NextIndex[Reg]++;
    OS << "template <> struct OptionRegistryOf<&" << Name << "> {\n";
    OS << "  static constexpr const auto *Reg = &" << RegName << ";\n";
    OS << "  static constexpr std::size_t Index = " << Index << ";\n";
    OS << "};\n";
  }
  OS << "\n";
}

void CLV2OptionsEmitter::emitRegistries(
    raw_ostream &OS, const std::vector<const Record *> &Registries,
    const std::vector<const Record *> &Options,
    const std::vector<const Record *> &Aliases) {
  for (const auto *Reg : Registries) {
    StringRef RegName = Reg->getName();

    // Collect options belonging to this registry
    std::vector<std::string> Members;
    for (const auto *Opt : Options) {
      const Record *OptReg = Opt->getValueAsDef("Registry");
      if (OptReg == Reg)
        Members.push_back("&" + getCppName(Opt));
    }
    for (const auto *A : Aliases) {
      const Record *AliasReg = A->getValueAsDef("Registry");
      if (AliasReg == Reg)
        Members.push_back("&" + std::string(A->getName()));
    }

    OS << "inline constexpr OptionsRegistry<\n";
    for (size_t I = 0; I < Members.size(); ++I) {
      OS << "    " << Members[I];
      if (I + 1 < Members.size())
        OS << ",";
      OS << "\n";
    }
    OS << "    > " << RegName << ";\n\n";
  }
}

void CLV2OptionsEmitter::emitAliases(
    raw_ostream &OS, const std::vector<const Record *> &Aliases) {
  for (const auto *A : Aliases) {
    StringRef Name = A->getName();
    StringRef CLIName = A->getValueAsString("CLIName");
    StringRef Target = A->getValueAsString("AliasFor");
    bool IsHidden = A->getValueAsBit("Hidden");
    StringRef Desc = A->getValueAsString("Description");
    OS << "inline constexpr AliasInfo " << Name << "{\""
       << escapeString(CLIName) << "\", \"" << escapeString(Target) << "\"";
    // An alias with no description is hidden by the parser, so emit the
    // description whenever there is one, and always when Hidden is requested
    // (the hidden form takes the description as its third argument).
    if (!Desc.empty() || IsHidden)
      OS << ", \"" << escapeString(Desc) << "\"";
    if (IsHidden)
      OS << ", Hidden";
    OS << "};\n";
  }
  if (!Aliases.empty())
    OS << "\n";
}

void CLV2OptionsEmitter::emitGetters(
    raw_ostream &OS, const std::vector<const Record *> &Registries,
    const std::vector<const Record *> &Options, const EnumTypeMap &EnumTypes,
    bool CtxOnly, GetterEmit Mode) {
  for (const auto *Reg : Registries) {
    StringRef RegName = Reg->getName();
    StringRef NS = Reg->getValueAsString("Namespace");
    auto CtxTypes = Reg->getValueAsListOfDefs("ContextTypes");
    bool OutOfLine = Reg->getValueAsBit("OutOfLineGetters");

    // Inline mode serves the registries that have not opted in; Decl and Def
    // serve the ones that have.  A registry never appears in both.
    if (OutOfLine != (Mode != GetterEmit::Inline))
      continue;

    std::string Export;
    if (OutOfLine && Mode == GetterEmit::Decl) {
      Export = std::string(Reg->getValueAsString("ExportMacro"));
      if (!Export.empty())
        Export += " ";
    }

    OS << "namespace " << NS << " {\n";
    // The ParsedOpts alias is part of the declaration surface, so it must not
    // be repeated in the definitions TU (which already saw the header).
    if (CtxOnly && Mode != GetterEmit::Def)
      OS << "using ParsedOpts = decltype(llvm::clv2::" << RegName
         << ")::ParsedOptionsT;\n\n";

    for (const auto *Opt : Options) {
      const Record *OptReg = Opt->getValueAsDef("Registry");
      if (OptReg != Reg)
        continue;

      std::string CppName = getCppName(Opt);
      std::string GetName = getGetterName(Opt);
      std::string CppType = getCppType(Opt);
      std::string DefaultVal = getDefaultExpr(Opt, EnumTypes);
      bool IsList = isListOption(Opt);
      bool IsBits = isBitsOption(Opt);
      bool IsEnum = isEnumOption(Opt, EnumTypes);
      bool ExtEnum = isExternalEnum(Opt, EnumTypes);
      std::string EnumQual = (IsEnum && !ExtEnum) ? "llvm::clv2::" : "";

      // Determine return type
      std::string RetType;
      if (IsBits) {
        RetType = "unsigned"; // BitsOptionInfo stores as unsigned bitmask
      } else if (IsList) {
        RetType = "std::vector<" + EnumQual + CppType + ">";
      } else if (IsEnum) {
        RetType = EnumQual + CppType;
      } else {
        RetType = CppType;
      }

      // Default value for getter fallback
      std::string FallbackDefault;
      if (DefaultVal.empty())
        FallbackDefault = RetType + "{}";
      else if (CppType == "std::string")
        FallbackDefault = "std::string(" + DefaultVal + ")";
      else if (IsEnum)
        FallbackDefault = EnumQual + DefaultVal;
      else
        FallbackDefault = DefaultVal;

      if (CtxOnly) {
        // OptionsContext overload — safe with forward declarations only.
        // Reference-only by design: having a context is the normal case, so
        // the signature says so and no null test is generated.  A caller that
        // genuinely has nowhere to get one must name defaultOptionsContext(),
        // which keeps the opt-out greppable instead of spelling it "nullptr".
        if (Mode == GetterEmit::Decl) {
          OS << Export << RetType << " " << GetName
             << "(const llvm::clv2::OptionsContext &Ctx);\n";
        } else {
          OS << (Mode == GetterEmit::Inline ? "inline " : "") << RetType << " "
             << GetName << "(const llvm::clv2::OptionsContext &Ctx) {\n";
          OS << "  return llvm::clv2::getOptValOr<&llvm::clv2::" << CppName
             << ">(Ctx, " << FallbackDefault << ");\n";
          OS << "}\n\n";
        }
      } else {
        // Context type overloads — need full type definitions.  These are
        // emitted inside a namespace, so the includes cannot be generated
        // here; the TU that defines CLV2_OPTIONS_GETTER_DEFS must provide
        // every type the AccessorExpr chains through.  Getting it wrong is a
        // compile error in that one TU.
        for (const auto *CT : CtxTypes) {
          StringRef TypeName = CT->getValueAsString("TypeName");
          std::string AccessorExpr =
              std::string(CT->getValueAsString("AccessorExpr"));
          std::string ParamName;
          // TypeName may be namespace-qualified (llvm::Function); derive the
          // parameter name from the unqualified tail.
          StringRef Unqualified = TypeName;
          if (size_t Colons = Unqualified.rfind("::");
              Colons != StringRef::npos)
            Unqualified = Unqualified.substr(Colons + 2);
          ParamName += llvm::toLower(Unqualified[0]);
          size_t Pos;
          while ((Pos = AccessorExpr.find('$')) != std::string::npos)
            AccessorExpr.replace(Pos, 1, ParamName);

          if (Mode == GetterEmit::Decl) {
            OS << Export << RetType << " " << GetName << "(const " << TypeName
               << " &" << ParamName << ");\n";
          } else {
            OS << (Mode == GetterEmit::Inline ? "inline " : "") << RetType
               << " " << GetName << "(const " << TypeName << " &" << ParamName
               << ") {\n";
            OS << "  return " << GetName << "(" << AccessorExpr << ");\n";
            OS << "}\n";
          }
        }
        OS << "\n";
      }
    }

    OS << "} // namespace " << NS << "\n\n";
  }
}

static TableGen::Emitter::OptClass<CLV2OptionsEmitter>
    X("gen-clv2-options", "Generate clv2 option declarations and getters");
