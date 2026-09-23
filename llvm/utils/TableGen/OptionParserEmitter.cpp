//===- OptionParserEmitter.cpp - Table Driven Command Option Line Parsing -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Common/OptEmitter.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Support/InterleavedRange.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/StringToOffsetTable.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <cstring>
#include <map>

using namespace llvm;

static std::string getOptionName(const Record &R) {
  // Use the record name unless EnumName is defined.
  if (isa<UnsetInit>(R.getValueInit("EnumName")))
    return R.getName().str();

  return R.getValueAsString("EnumName").str();
}

// The bare, user-facing spelling for a Field<>/Assign<> record's option, used
// in generated parse-failure diagnostics (e.g. "for the --dump-input option:
// ..."). ValueField/EnumField/ListField/EnumListField's primary def's Name
// includes a trailing "=" when the option's Joined spelling embeds one
// (appendEquals=1, see OptParser.td); strip it so the message reads the same
// regardless of which spelling matched.
static std::string getOptionDisplayName(const Record &R) {
  StringRef Name = R.getValueAsString("Name");
  return Name.ends_with("=") ? Name.drop_back().str() : Name.str();
}

// clv2's own "for the -X option: ..." diagnostics use a single dash for a
// one-character name (e.g. "-D") and a double dash otherwise (e.g.
// "--dump-input"); matched here so generated diagnostics read identically.
static const char *getOptionDashPrefix(StringRef DisplayName) {
  return DisplayName.size() == 1 ? "-" : "--";
}

// Maps a Field<>'s C++ element type to the wording clv2's own per-type
// parsers used (CommandLineV2.cpp's parseIntArg/parseUIntArg/etc.), so
// generated parse-failure diagnostics keep matching that established,
// widely-lit-tested repo convention.
static const char *getTypeArgWord(StringRef ElementType) {
  if (ElementType == "int" || ElementType == "int64_t")
    return "integer";
  if (ElementType == "unsigned" || ElementType == "uint64_t")
    return "uint";
  if (ElementType == "double" || ElementType == "float")
    return "floating point";
  if (ElementType == "bool")
    return "boolean";
  return nullptr;
}

// Only pass EmitComment for short strings that cannot contain "*/".
static void writeStrTableOffset(raw_ostream &OS,
                                const StringToOffsetTable &Table,
                                llvm::StringRef Str, bool EmitComment = false) {
  std::optional<unsigned> Offset = Table.GetStringOffset(Str);
  if (!Offset)
    PrintFatalError("string was not added to the option string table: " + Str);
  OS << *Offset;
  if (EmitComment) {
    OS << " /* ";
    OS.write_escaped(Str);
    OS << " */";
  }
}

static raw_ostream &writeCstring(raw_ostream &OS, llvm::StringRef Str) {
  OS << '"';
  OS.write_escaped(Str);
  OS << '"';
  return OS;
}

static StringRef getOptionalString(const Record &R, StringRef Field) {
  return R.getValueAsOptionalString(Field).value_or("");
}

// Offset zero is the empty string and stands for an unset HelpText. A
// HelpText<""> marks an option as deliberately undocumented, so it maps to a
// second empty string that the table does not put at offset zero.
static StringRef getHelpText(const Record &R) {
  std::optional<StringRef> S = R.getValueAsOptionalString("HelpText");
  if (!S)
    return StringRef();
  return S->empty() ? StringRef("\0", 1) : *S;
}

// The string table appends the empty string that terminates the list.
static std::string getAliasArgsBlob(const Record &R) {
  std::string Blob;
  for (StringRef AliasArg : R.getValueAsListOfStrings("AliasArgs")) {
    if (AliasArg.empty())
      PrintFatalError(R.getLoc(), "AliasArgs entries must not be empty");
    Blob += AliasArg;
    Blob += '\0';
  }
  return Blob;
}

static std::string getOptionPrefixedName(const Record &R) {
  std::vector<StringRef> Prefixes = R.getValueAsListOfStrings("Prefixes");
  StringRef Name = R.getValueAsString("Name");

  if (Prefixes.empty())
    return Name.str();

  return (Prefixes[0] + Twine(Name)).str();
}

class MarshallingInfo {
public:
  static constexpr const char *MacroName = "OPTION_WITH_MARSHALLING";
  const Record &R;
  bool ShouldAlwaysEmit = false;
  StringRef MacroPrefix;
  StringRef KeyPath;
  StringRef DefaultValue;
  StringRef NormalizedValuesScope;
  StringRef ImpliedCheck;
  StringRef ImpliedValue;
  StringRef ShouldParse;
  StringRef Normalizer;
  StringRef Denormalizer;
  int TableIndex = -1;
  std::vector<StringRef> Values;
  std::vector<StringRef> NormalizedValues;
  std::string ValueTableName;

  static size_t NextTableIndex;

  static constexpr const char *ValueTablePreamble = R"(
struct SimpleEnumValue {
  const char *Name;
  unsigned Value;
};

struct SimpleEnumValueTable {
  const SimpleEnumValue *Table;
  unsigned Size;
};
)";

  static constexpr const char *ValueTablesDecl =
      "static const SimpleEnumValueTable SimpleEnumValueTables[] = ";

  MarshallingInfo(const Record &R) : R(R) {}

  std::string getMacroName() const {
    return (MacroPrefix + MarshallingInfo::MacroName).str();
  }

  void emit(raw_ostream &OS) const {
    OS << ShouldParse;
    OS << ", ";
    OS << ShouldAlwaysEmit;
    OS << ", ";
    OS << KeyPath;
    OS << ", ";
    emitScopedNormalizedValue(OS, DefaultValue);
    OS << ", ";
    OS << ImpliedCheck;
    OS << ", ";
    emitScopedNormalizedValue(OS, ImpliedValue);
    OS << ", ";
    OS << Normalizer;
    OS << ", ";
    OS << Denormalizer;
    OS << ", ";
    OS << TableIndex;
  }

  std::optional<StringRef> emitValueTable(raw_ostream &OS) const {
    if (TableIndex == -1)
      return {};
    OS << "static const SimpleEnumValue " << ValueTableName << "[] = {\n";
    for (unsigned I = 0, E = Values.size(); I != E; ++I) {
      OS << "{";
      writeCstring(OS, Values[I]);
      OS << ",";
      OS << "static_cast<unsigned>(";
      emitScopedNormalizedValue(OS, NormalizedValues[I]);
      OS << ")},";
    }
    OS << "};\n";
    return StringRef(ValueTableName);
  }

private:
  void emitScopedNormalizedValue(raw_ostream &OS,
                                 StringRef NormalizedValue) const {
    if (!NormalizedValuesScope.empty())
      OS << NormalizedValuesScope << "::";
    OS << NormalizedValue;
  }
};

size_t MarshallingInfo::NextTableIndex = 0;

static MarshallingInfo createMarshallingInfo(const Record &R) {
  assert(!isa<UnsetInit>(R.getValueInit("KeyPath")) &&
         !isa<UnsetInit>(R.getValueInit("DefaultValue")) &&
         "MarshallingInfo must have a provide a keypath, default value and a "
         "value merger");

  MarshallingInfo Ret(R);

  Ret.ShouldAlwaysEmit = R.getValueAsBit("ShouldAlwaysEmit");
  Ret.MacroPrefix = R.getValueAsString("MacroPrefix");
  Ret.KeyPath = R.getValueAsString("KeyPath");
  Ret.DefaultValue = R.getValueAsString("DefaultValue");
  Ret.NormalizedValuesScope = R.getValueAsString("NormalizedValuesScope");
  Ret.ImpliedCheck = R.getValueAsString("ImpliedCheck");
  Ret.ImpliedValue =
      R.getValueAsOptionalString("ImpliedValue").value_or(Ret.DefaultValue);

  Ret.ShouldParse = R.getValueAsString("ShouldParse");
  Ret.Normalizer = R.getValueAsString("Normalizer");
  Ret.Denormalizer = R.getValueAsString("Denormalizer");

  if (!isa<UnsetInit>(R.getValueInit("NormalizedValues"))) {
    assert(!isa<UnsetInit>(R.getValueInit("Values")) &&
           "Cannot provide normalized values for value-less options");
    Ret.TableIndex = MarshallingInfo::NextTableIndex++;
    Ret.NormalizedValues = R.getValueAsListOfStrings("NormalizedValues");
    Ret.Values.reserve(Ret.NormalizedValues.size());
    Ret.ValueTableName = getOptionName(R) + "ValueTable";

    StringRef ValuesStr = R.getValueAsString("Values");
    for (;;) {
      size_t Idx = ValuesStr.find(',');
      if (Idx == StringRef::npos)
        break;
      if (Idx > 0)
        Ret.Values.push_back(ValuesStr.slice(0, Idx));
      ValuesStr = ValuesStr.substr(Idx + 1);
    }
    if (!ValuesStr.empty())
      Ret.Values.push_back(ValuesStr);

    assert(Ret.Values.size() == Ret.NormalizedValues.size() &&
           "The number of normalized values doesn't match the number of "
           "values");
  }

  return Ret;
}

static void emitHelpTextsForVariants(
    raw_ostream &OS, const StringToOffsetTable &Table,
    ArrayRef<std::pair<std::vector<std::string>, StringRef>>
        HelpTextsForVariants) {
  // OptTable must be constexpr so it uses std::arrays with these capacities.
  const unsigned MaxVisibilityPerHelp = 2;
  const unsigned MaxVisibilityHelp = 1;

  assert(HelpTextsForVariants.size() <= MaxVisibilityHelp &&
         "Too many help text variants to store in "
         "OptTable::HelpTextsForVariants");

  OS << ", (std::array<std::pair<std::array<unsigned, " << MaxVisibilityPerHelp
     << ">, llvm::StringTable::Offset>, " << MaxVisibilityHelp << ">{{ ";

  ListSeparator Sep;
  for (const auto &[Visibilities, Help] : HelpTextsForVariants) {
    assert(Visibilities.size() <= MaxVisibilityPerHelp &&
           "Too many visibilities to store in an "
           "OptTable::HelpTextsForVariants entry");
    OS << Sep << "{std::array<unsigned, " << MaxVisibilityPerHelp << ">{{"
       << llvm::interleaved(Visibilities) << "}}, ";
    writeStrTableOffset(OS, Table, Help);
    OS << "}";
  }
  // Unused entries are value-initialized.
  for (size_t I = HelpTextsForVariants.size(); I < MaxVisibilityHelp; ++I)
    OS << Sep << "{}";
  OS << " }})";
}

/// OptionParserEmitter - This tablegen backend takes an input .td file
/// describing a list of options and emits a data structure for parsing and
/// working with those options when given an input command line.
static void emitOptionParser(const RecordKeeper &Records, raw_ostream &OS) {
  // Get the option groups and options.
  ArrayRef<const Record *> Groups =
      Records.getAllDerivedDefinitions("OptionGroup");
  std::vector<const Record *> Opts = Records.getAllDerivedDefinitions("Option");
  llvm::sort(Opts, IsOptionRecordsLess);

  std::vector<const Record *> SubCommands =
      Records.getAllDerivedDefinitions("SubCommand");

  emitSourceFileHeader("Option Parsing Definitions", OS);

  // Generate prefix groups.
  using PrefixKeyT = SmallVector<SmallString<2>, 2>;
  using PrefixesT = std::map<PrefixKeyT, unsigned>;
  PrefixesT Prefixes;
  Prefixes.try_emplace(PrefixKeyT(), 0);
  for (const Record &R : llvm::make_pointee_range(Opts)) {
    std::vector<StringRef> RPrefixes = R.getValueAsListOfStrings("Prefixes");
    PrefixKeyT PrefixKey(RPrefixes.begin(), RPrefixes.end());
    Prefixes.try_emplace(PrefixKey, 0);
  }

  // Generate sub command groups.
  using SubCommandKeyT = SmallVector<StringRef, 2>;
  using SubCommandIDsT = std::map<SubCommandKeyT, unsigned>;
  SubCommandIDsT SubCommandIDs;

  auto PrintSubCommandIdsOffset = [&SubCommandIDs, &OS](const Record &R) {
    if (R.getValue("SubCommands") != nullptr) {
      std::vector<const Record *> SubCommands =
          R.getValueAsListOfDefs("SubCommands");
      SubCommandKeyT SubCommandKey;
      for (const auto &SubCommand : SubCommands)
        SubCommandKey.push_back(SubCommand->getName());
      OS << SubCommandIDs[SubCommandKey];
    } else {
      // The option SubCommandIDsOffset (for default top level toolname is 0).
      OS << " 0";
    }
  };

  SubCommandIDs.try_emplace(SubCommandKeyT(), 0);
  for (const Record &R : llvm::make_pointee_range(Opts)) {
    std::vector<const Record *> RSubCommands =
        R.getValueAsListOfDefs("SubCommands");
    SubCommandKeyT SubCommandKey;
    for (const auto &SubCommand : RSubCommands)
      SubCommandKey.push_back(SubCommand->getName());
    SubCommandIDs.try_emplace(SubCommandKey, 0);
  }

  DenseSet<StringRef> PrefixesUnionSet;
  for (const auto &[Prefix, _] : Prefixes)
    PrefixesUnionSet.insert_range(Prefix);
  SmallVector<StringRef> PrefixesUnion(PrefixesUnionSet.begin(),
                                       PrefixesUnionSet.end());
  array_pod_sort(PrefixesUnion.begin(), PrefixesUnion.end());

  llvm::StringToOffsetTable Table;
  // We can add all the prefixes via the union.
  for (const auto &Prefix : PrefixesUnion)
    Table.GetOrAddStringOffset(Prefix);
  for (const Record &R : llvm::make_pointee_range(Groups)) {
    Table.GetOrAddStringOffset(R.getValueAsString("Name"));
    Table.GetOrAddStringOffset(getHelpText(R));
  }
  for (const Record &R : llvm::make_pointee_range(Opts)) {
    Table.GetOrAddStringOffset(getOptionPrefixedName(R));
    Table.GetOrAddStringOffset(getHelpText(R));
    Table.GetOrAddStringOffset(getOptionalString(R, "MetaVarName"));
    Table.GetOrAddStringOffset(getOptionalString(R, "Values"));
    Table.GetOrAddStringOffset(getAliasArgsBlob(R));
    for (const Record *VisibilityHelp :
         R.getValueAsListOfDefs("HelpTextsForVariants"))
      Table.GetOrAddStringOffset(VisibilityHelp->getValueAsString("Text"));
  }

  // Dump string table.
  OS << "/////////\n";
  OS << "// String table\n\n";
  OS << "#ifdef OPTTABLE_STR_TABLE_CODE\n";
  Table.EmitStringTableDef(OS, "OptionStrTable");
  OS << "#endif // OPTTABLE_STR_TABLE_CODE\n\n";

  // Dump prefixes.
  OS << "/////////\n";
  OS << "// Prefixes\n\n";
  OS << "#ifdef OPTTABLE_PREFIXES_TABLE_CODE\n";
  OS << "static constexpr llvm::StringTable::Offset OptionPrefixesTable[] = "
        "{\n";
  {
    // Ensure the first prefix set is always empty.
    assert(!Prefixes.empty() &&
           "We should always emit an empty set of prefixes");
    assert(Prefixes.begin()->first.empty() &&
           "First prefix set should always be empty");
    llvm::ListSeparator Sep(",\n");
    unsigned CurIndex = 0;
    for (auto &[Prefix, PrefixIndex] : Prefixes) {
      // First emit the number of prefix strings in this list of prefixes.
      OS << Sep << "  " << Prefix.size() << " /* prefixes */";
      PrefixIndex = CurIndex;
      assert((CurIndex == 0 || !Prefix.empty()) &&
             "Only first prefix set should be empty!");
      for (const auto &PrefixKey : Prefix)
        OS << ", " << *Table.GetStringOffset(PrefixKey) << " /* '" << PrefixKey
           << "' */";
      CurIndex += Prefix.size() + 1;
    }
  }
  OS << "\n};\n";
  OS << "#endif // OPTTABLE_PREFIXES_TABLE_CODE\n\n";

  // Dump subcommand IDs.
  OS << "/////////";
  OS << "// SubCommand IDs\n\n";
  OS << "#ifdef OPTTABLE_SUBCOMMAND_IDS_TABLE_CODE\n";
  OS << "static constexpr unsigned OptionSubCommandIDsTable[] = {\n";
  {
    // Ensure the first subcommand set is always empty.
    assert(!SubCommandIDs.empty() &&
           "We should always emit an empty set of subcommands");
    assert(SubCommandIDs.begin()->first.empty() &&
           "First subcommand set should always be empty");
    llvm::ListSeparator Sep(",\n");
    unsigned CurIndex = 0;
    for (auto &[SubCommand, SubCommandIndex] : SubCommandIDs) {
      // First emit the number of subcommand strings in this list of
      // subcommands.
      OS << Sep << "  " << SubCommand.size() << " /* subcommands */";
      SubCommandIndex = CurIndex;
      assert((CurIndex == 0 || !SubCommand.empty()) &&
             "Only first subcommand set should be empty!");
      for (const auto &SubCommandKey : SubCommand) {
        auto It = llvm::find_if(SubCommands, [&](const Record *R) {
          return R->getName() == SubCommandKey;
        });
        assert(It != SubCommands.end() && "SubCommand not found");
        OS << ", " << std::distance(SubCommands.begin(), It) << " /* '"
           << SubCommandKey << "' */";
      }
      CurIndex += SubCommand.size() + 1;
    }
  }
  OS << "\n};\n";
  OS << "#endif // OPTTABLE_SUBCOMMAND_IDS_TABLE_CODE\n\n";

  // Dump prefixes union.
  OS << "/////////\n";
  OS << "// Prefix Union\n\n";
  OS << "#ifdef OPTTABLE_PREFIXES_UNION_CODE\n";
  OS << "static constexpr llvm::StringTable::Offset OptionPrefixesUnion[] = "
        "{\n";
  {
    llvm::ListSeparator Sep(", ");
    for (auto Prefix : PrefixesUnion)
      OS << Sep << "  " << *Table.GetStringOffset(Prefix) << " /* '" << Prefix
         << "' */";
  }
  OS << "\n};\n";
  OS << "#endif // OPTTABLE_PREFIXES_UNION_CODE\n\n";

  // Dump groups.
  OS << "/////////\n";
  OS << "// ValuesCode\n\n";
  OS << "#ifdef OPTTABLE_VALUES_CODE\n";
  std::vector<const Record *> ValuesCodeOpts;
  for (const Record &R : llvm::make_pointee_range(Opts)) {
    // The option values, if any;
    if (!isa<UnsetInit>(R.getValueInit("ValuesCode"))) {
      if (!isa<UnsetInit>(R.getValueInit("Values")))
        PrintFatalError(R.getLoc(), "cannot set both Values and ValuesCode");
      ValuesCodeOpts.push_back(&R);
      OS << "#define VALUES_CODE " << getOptionName(R) << "_Values\n";
      OS << R.getValueAsString("ValuesCode") << "\n";
      OS << "#undef VALUES_CODE\n";
    }
  }
  // A function keeps these strings out of a relocated table. It names OPT_ IDs,
  // so include this block after the option enum; a table that uses a different
  // ID prefix cannot use ValuesCode.
  OS << "static llvm::StringRef getOptionValuesCode(unsigned ID) {\n";
  OS << "  switch (ID) {\n";
  for (const Record *R : ValuesCodeOpts)
    OS << "  case OPT_" << getOptionName(*R) << ": return " << getOptionName(*R)
       << "_Values;\n";
  OS << "  }\n  return {};\n}\n";
  OS << "#endif\n";

  OS << "/////////\n";
  OS << "// Groups\n\n";
  OS << "#ifdef OPTION\n";
  for (const Record &R : llvm::make_pointee_range(Groups)) {
    // Start a single option entry.
    OS << "OPTION(";

    // A zero prefix offset corresponds to an empty set of prefixes.
    OS << "0 /* no prefixes */";

    // The option string offset.
    OS << ", ";
    writeStrTableOffset(OS, Table, R.getValueAsString("Name"),
                        /*EmitComment=*/true);

    // The option identifier name.
    OS << ", " << getOptionName(R);

    // The option kind.
    OS << ", Group";

    // The containing option group (if any).
    OS << ", ";
    if (const DefInit *DI = dyn_cast<DefInit>(R.getValueInit("Group")))
      OS << getOptionName(*DI->getDef());
    else
      OS << "INVALID";

    // The other option arguments (unused for groups).
    OS << ", INVALID, 0, 0, 0, 0";

    // The option help text.
    OS << ", ";
    writeStrTableOffset(OS, Table, getHelpText(R));

    // Not using Visibility specific text for group help.
    emitHelpTextsForVariants(OS, Table, {});

    // The option meta-variable name (unused).
    OS << ", 0";

    // The option Values (unused for groups).
    OS << ", 0";

    // The option SubCommandIDsOffset.
    OS << ", ";
    PrintSubCommandIdsOffset(R);
    OS << ")\n";
  }
  OS << "\n";

  OS << "//////////\n";
  OS << "// Options\n\n";

  auto WriteOptRecordFields = [&](raw_ostream &OS, const Record &R) {
    // The option prefix;
    std::vector<StringRef> RPrefixes = R.getValueAsListOfStrings("Prefixes");
    OS << Prefixes[PrefixKeyT(RPrefixes.begin(), RPrefixes.end())] << ", ";

    // The option prefixed name.
    writeStrTableOffset(OS, Table, getOptionPrefixedName(R),
                        /*EmitComment=*/true);

    // The option identifier name.
    OS << ", " << getOptionName(R);

    // The option kind.
    OS << ", " << R.getValueAsDef("Kind")->getValueAsString("Name");

    // The containing option group (if any).
    OS << ", ";
    const ListInit *GroupFlags = nullptr;
    const ListInit *GroupVis = nullptr;
    if (const DefInit *DI = dyn_cast<DefInit>(R.getValueInit("Group"))) {
      GroupFlags = DI->getDef()->getValueAsListInit("Flags");
      GroupVis = DI->getDef()->getValueAsListInit("Visibility");
      OS << getOptionName(*DI->getDef());
    } else {
      OS << "INVALID";
    }

    // The option alias (if any).
    OS << ", ";
    if (const DefInit *DI = dyn_cast<DefInit>(R.getValueInit("Alias")))
      OS << getOptionName(*DI->getDef());
    else
      OS << "INVALID";

    // The option alias arguments (if any).
    OS << ", ";
    writeStrTableOffset(OS, Table, getAliasArgsBlob(R));

    // "Flags" for the option, such as HelpHidden and Render*
    OS << ", ";
    int NumFlags = 0;
    const ListInit *LI = R.getValueAsListInit("Flags");
    for (const Init *I : *LI)
      OS << (NumFlags++ ? " | " : "") << cast<DefInit>(I)->getDef()->getName();
    if (GroupFlags) {
      for (const Init *I : *GroupFlags)
        OS << (NumFlags++ ? " | " : "")
           << cast<DefInit>(I)->getDef()->getName();
    }
    if (NumFlags == 0)
      OS << '0';

    // Option visibility, for sharing options between drivers.
    OS << ", ";
    int NumVisFlags = 0;
    LI = R.getValueAsListInit("Visibility");
    for (const Init *I : *LI)
      OS << (NumVisFlags++ ? " | " : "")
         << cast<DefInit>(I)->getDef()->getName();
    if (GroupVis) {
      for (const Init *I : *GroupVis)
        OS << (NumVisFlags++ ? " | " : "")
           << cast<DefInit>(I)->getDef()->getName();
    }
    if (NumVisFlags == 0)
      OS << '0';

    // The option parameter field.
    OS << ", " << R.getValueAsInt("NumArgs");

    // The option help text.
    OS << ", ";
    writeStrTableOffset(OS, Table, getHelpText(R));

    std::vector<std::pair<std::vector<std::string>, StringRef>>
        HelpTextsForVariants;
    for (const Record *VisibilityHelp :
         R.getValueAsListOfDefs("HelpTextsForVariants")) {
      ArrayRef<const Init *> Visibilities =
          VisibilityHelp->getValueAsListInit("Visibilities")->getElements();

      std::vector<std::string> VisibilityNames;
      for (const Init *Visibility : Visibilities)
        VisibilityNames.push_back(Visibility->getAsUnquotedString());

      HelpTextsForVariants.emplace_back(
          VisibilityNames, VisibilityHelp->getValueAsString("Text"));
    }
    emitHelpTextsForVariants(OS, Table, HelpTextsForVariants);

    // The option meta-variable name.
    OS << ", ";
    writeStrTableOffset(OS, Table, getOptionalString(R, "MetaVarName"));

    // The option Values. Used for shell autocompletion.
    OS << ", ";
    writeStrTableOffset(OS, Table, getOptionalString(R, "Values"));

    // The option SubCommandIDsOffset.
    OS << ", ";
    PrintSubCommandIdsOffset(R);
  };

  auto IsMarshallingOption = [](const Record &R) {
    return !isa<UnsetInit>(R.getValueInit("KeyPath")) &&
           !R.getValueAsString("KeyPath").empty();
  };

  std::vector<const Record *> OptsWithMarshalling;
  for (const Record &R : llvm::make_pointee_range(Opts)) {
    // Start a single option entry.
    OS << "OPTION(";
    WriteOptRecordFields(OS, R);
    OS << ")\n";
    if (IsMarshallingOption(R))
      OptsWithMarshalling.push_back(&R);
  }
  OS << "#endif // OPTION\n";

  auto CmpMarshallingOpts = [](const Record *const *A, const Record *const *B) {
    unsigned AID = (*A)->getID();
    unsigned BID = (*B)->getID();

    if (AID < BID)
      return -1;
    if (AID > BID)
      return 1;
    return 0;
  };
  // The RecordKeeper stores records (options) in lexicographical order, and we
  // have reordered the options again when generating prefix groups. We need to
  // restore the original definition order of options with marshalling to honor
  // the topology of the dependency graph implied by `DefaultAnyOf`.
  array_pod_sort(OptsWithMarshalling.begin(), OptsWithMarshalling.end(),
                 CmpMarshallingOpts);

  std::vector<MarshallingInfo> MarshallingInfos;
  MarshallingInfos.reserve(OptsWithMarshalling.size());
  for (const auto *R : OptsWithMarshalling)
    MarshallingInfos.push_back(createMarshallingInfo(*R));

  for (const auto &MI : MarshallingInfos) {
    OS << "#ifdef " << MI.getMacroName() << "\n";
    OS << MI.getMacroName() << "(";
    WriteOptRecordFields(OS, MI.R);
    OS << ", ";
    MI.emit(OS);
    OS << ")\n";
    OS << "#endif // " << MI.getMacroName() << "\n";
  }

  OS << "\n";
  OS << "#ifdef SIMPLE_ENUM_VALUE_TABLE";
  OS << "\n";
  OS << MarshallingInfo::ValueTablePreamble;
  std::vector<StringRef> ValueTableNames;
  for (const auto &MI : MarshallingInfos)
    if (auto MaybeValueTableName = MI.emitValueTable(OS))
      ValueTableNames.push_back(*MaybeValueTableName);

  OS << MarshallingInfo::ValueTablesDecl << "{";
  for (auto ValueTableName : ValueTableNames)
    OS << "{" << ValueTableName << ", std::size(" << ValueTableName << ")},\n";
  OS << "};\n";
  OS << "static const unsigned SimpleEnumValueTablesSize = "
        "std::size(SimpleEnumValueTables);\n";

  OS << "#endif // SIMPLE_ENUM_VALUE_TABLE\n";
  OS << "\n";
  OS << "/////////\n";
  OS << "\n// SubCommands\n\n";
  OS << "#ifdef OPTTABLE_SUBCOMMANDS_CODE\n";
  OS << "static constexpr llvm::opt::OptTable::SubCommand OptionSubCommands[] "
        "= "
        "{\n";
  for (const Record *SubCommand : SubCommands) {
    OS << "  { \"" << SubCommand->getValueAsString("Name") << "\", ";
    OS << "\"" << SubCommand->getValueAsString("HelpText") << "\", ";
    OS << "\"" << SubCommand->getValueAsString("Usage") << "\" },\n";
  }
  OS << "};\n";
  OS << "#endif // OPTTABLE_SUBCOMMANDS_CODE\n\n";

  // ==========================================================================
  // Library options structs. See the OptionsStruct/Field/Assign/FieldEnum
  // classes and BoolField/ValueField/EnumField multiclasses in OptParser.td,
  // and llvm/include/llvm/Option/LibraryOptions.h for the runtime side
  // (parseFieldValue, registerLibraryOptions).
  //
  // `table()` is intentionally NOT generated here: every existing OptTable
  // consumer in this codebase builds its GenericOptTable by hand from the
  // OPTION-gated rows already emitted above, and the per-library .cpp does
  // the same for consistency rather than duplicating that idiom here.
  // ==========================================================================
  std::vector<const Record *> OptionStructs =
      Records.getAllDerivedDefinitions("OptionsStruct");
  std::vector<const Record *> OptionEnums =
      Records.getAllDerivedDefinitions("OptionEnum");

  auto HasField = [](const Record &R) {
    return R.getValue("FieldType") != nullptr;
  };
  auto HasAssign = [](const Record &R) {
    return R.getValue("AssignMember") != nullptr;
  };
  auto HasBoolAssign = [](const Record &R) {
    return R.getValue("AssignKeyPath") != nullptr;
  };
  auto HasListElement = [](const Record &R) {
    return R.getValue("ElementType") != nullptr;
  };

  std::vector<const Record *> StructOpts;
  for (const Record &R : llvm::make_pointee_range(Opts))
    if (HasField(R) || HasAssign(R) || HasBoolAssign(R))
      StructOpts.push_back(&R);

  if (!OptionStructs.empty()) {
    if (OptionStructs.size() > 1)
      PrintFatalError("only one OptionsStruct def is supported per .td file");
    const Record &SR = *OptionStructs.front();
    StringRef StructName = SR.getName();
    StringRef Namespace = SR.getValueAsString("Namespace");
    bool HasPositional = SR.getValue("PositionalType") != nullptr;

    OS << "/////////\n";
    OS << "// Options struct declaration\n\n";
    OS << "#ifdef OPTIONS_STRUCT_DECL\n";
    // Non-external enums are generated types owned by this struct, so they
    // must be declared inside its namespace (a qualified `enum class
    // Namespace::Name { ... };` definition is not legal C++). External enums
    // already use fully-qualified names (e.g. "llvm::cl::boolOrDefault"),
    // which resolve the same whether nested in `namespace Namespace` or not,
    // so opening the namespace here doesn't affect them.
    OS << "namespace " << Namespace << " {\n";
    for (const Record *ER : OptionEnums) {
      // EnumType is the C++ type reference (may be namespace-qualified, e.g.
      // "cl::boolOrDefault"); EnumIdent is a valid bare identifier used to
      // name the generated `enum class` / `parse<Ident>` helper, since "::"
      // cannot appear in a function name.
      StringRef EnumType = ER->getValueAsString("Name");
      StringRef EnumIdent = ER->getValueAsString("Ident");
      bool IsExternal = ER->getValueAsBit("External");
      std::vector<const Record *> Members = ER->getValueAsListOfDefs("Members");
      // External enums (e.g. InliningAdvisorMode, cl::boolOrDefault) already
      // exist in C++ elsewhere; only the spelling<->value parse helper below
      // is generated for them, not a colliding duplicate `enum class`.
      if (!IsExternal) {
        OS << "enum class " << EnumType << " {\n";
        for (const Record *M : Members)
          OS << "  " << M->getValueAsString("Name") << ",\n";
        OS << "};\n";
      }
      // External enums may be referenced (with the same Ident) from more than
      // one OptionsStruct's .td file; if two such generated headers both end
      // up included in the same translation unit, the parse<Ident> helper
      // below would otherwise be defined twice. Guard it so only the first
      // inclusion wins; non-external enums are private to this struct and
      // can't collide this way.
      if (IsExternal) {
        OS << "#ifndef LLVM_OPTPARSER_PARSE_" << EnumIdent << "_DEFINED\n";
        OS << "#define LLVM_OPTPARSER_PARSE_" << EnumIdent << "_DEFINED\n";
      }
      OS << "inline bool parse" << EnumIdent << "(llvm::StringRef V, "
         << EnumType << " &Out) {\n";
      OS << "  static constexpr std::pair<llvm::StringLiteral, " << EnumType
         << "> Table[] = {\n";
      for (const Record *M : Members)
        OS << "    {\"" << M->getValueAsString("Spelling") << "\", " << EnumType
           << "::" << M->getValueAsString("Name") << "},\n";
      OS << "  };\n";
      OS << "  for (const auto &[Spelling, Val] : Table)\n";
      OS << "    if (V == Spelling) { Out = Val; return true; }\n";
      OS << "  return false;\n";
      OS << "}\n";
      if (IsExternal)
        OS << "#endif\n";
    }
    OS << "struct " << StructName << " {\n";
    if (HasPositional) {
      OS << "  " << SR.getValueAsString("PositionalType") << " "
         << SR.getValueAsString("PositionalName") << ";\n";
    }
    for (const Record *R : StructOpts) {
      if (!HasField(*R))
        continue;
      StringRef FieldType = R->getValueAsString("FieldType");
      StringRef KeyPath = R->getValueAsString("KeyPath");
      StringRef Default = R->getValueAsString("DefaultValue");
      OS << "  " << FieldType << " " << KeyPath;
      if (!Default.empty())
        OS << " = " << Default;
      OS << ";\n";
    }
    OS << "\n";
    OS << "  static " << StructName << " Current;\n";
    OS << "  static unsigned Slot;\n";
    OS << "  static llvm::opt::OptTable &table();\n";
    OS << "  // Returns true if A was recognized and applied. On failure, "
          "Err\n";
    OS << "  // is left empty if A simply isn't one of this struct's options\n";
    OS << "  // (the caller should forward it to Rest), or set to a "
          "formatted\n";
    OS << "  // diagnostic if A was recognized but its value was invalid "
          "(the\n";
    OS << "  // caller should treat this as a hard parse error).\n";
    OS << "  bool apply(const llvm::opt::Arg &A, std::string &Err);\n";
    OS << "  llvm::Error parse(llvm::ArrayRef<const char *> Args,\n";
    OS << "                    llvm::SmallVectorImpl<const char *> &Rest,\n";
    OS << "                    llvm::raw_ostream &Errs);\n";
    OS << "};\n";
    OS << "} // namespace " << Namespace << "\n";
    OS << "#endif // OPTIONS_STRUCT_DECL\n\n";

    OS << "/////////\n";
    OS << "// Options struct definitions\n\n";
    OS << "#ifdef OPTIONS_STRUCT_DEFS\n";
    OS << StructName << " " << Namespace << "::" << StructName
       << "::Current{};\n";
    OS << "unsigned " << Namespace << "::" << StructName << "::Slot = 0;\n";
    OS << "bool " << Namespace << "::" << StructName
       << "::apply(const llvm::opt::Arg &A, std::string &Err) {\n";
    OS << "  switch (A.getOption().getID()) {\n";
    for (const Record *R : StructOpts) {
      OS << "  case OPT_" << getOptionName(*R) << ":\n";
      const DefInit *EnumDI =
          R->getValue("FieldEnum")
              ? dyn_cast<DefInit>(R->getValueInit("FieldEnum"))
              : nullptr;
      std::string DisplayName = getOptionDisplayName(*R);
      if (HasAssign(*R)) {
        OS << "    " << R->getValueAsString("AssignMember") << " = "
           << R->getValueAsString("AssignValue") << ";\n";
        OS << "    return true;\n";
      } else if (HasBoolAssign(*R)) {
        StringRef KeyPath = R->getValueAsString("AssignKeyPath");
        OS << "    if (!llvm::opt::parseFieldValue(A.getValue(), " << KeyPath
           << ")) {\n";
        OS << "      Err = (llvm::Twine(\"for the "
           << getOptionDashPrefix(DisplayName) << DisplayName
           << " option: '\") + A.getValue() + \"' value invalid for boolean "
              "argument!\").str();\n";
        OS << "      return false;\n";
        OS << "    }\n";
        OS << "    return true;\n";
      } else if (EnumDI && HasListElement(*R)) {
        StringRef EnumType = EnumDI->getDef()->getValueAsString("Name");
        StringRef EnumIdent = EnumDI->getDef()->getValueAsString("Ident");
        StringRef KeyPath = R->getValueAsString("KeyPath");
        bool CommaSplit = R->getValueAsBit("CommaSplit");
        bool AlwaysPrefix = R->getValueAsBit("AlwaysPrefix");
        // Each matched Arg (whichever spelling -- Joined, JoinedOrSeparate,
        // or the Separate alias -- actually matched) carries exactly one
        // raw value; when CommaSplit is set that raw value is split here,
        // in emitted code, rather than relying on OptTable's CommaJoined
        // Kind, so both spellings behave identically. See ListElement in
        // OptParser.td.
        if (AlwaysPrefix) {
          // AlwaysPrefixFormat (e.g. "-Dfoo=bar"): the value must be
          // attached to the same token; a bare token with nothing attached
          // is a hard error, never a prompt to consume the next token.
          OS << "    if (llvm::StringRef(A.getValue()).empty()) {\n";
          OS << "      Err = \"for the " << getOptionDashPrefix(DisplayName)
             << DisplayName << " option: requires a value!\";\n";
          OS << "      return false;\n";
          OS << "    }\n";
        }
        OS << "    {\n";
        if (CommaSplit) {
          OS << "      llvm::SmallVector<llvm::StringRef, 4> Parts;\n";
          OS << "      llvm::StringRef(A.getValue())"
                ".split(Parts, ',', -1, /*KeepEmpty=*/true);\n";
          OS << "      for (llvm::StringRef Part : Parts) {\n";
          OS << "        " << EnumType << " Elt;\n";
          OS << "        if (!parse" << EnumIdent << "(Part, Elt)) {\n";
          OS << "          Err = (llvm::Twine(\"for the "
             << getOptionDashPrefix(DisplayName) << DisplayName
             << " option: Cannot find option named '\") + Part + \"'!\")"
                ".str();\n";
          OS << "          return false;\n";
          OS << "        }\n";
          OS << "        " << KeyPath << ".push_back(Elt);\n";
          OS << "      }\n";
        } else {
          OS << "      " << EnumType << " Elt;\n";
          OS << "      if (!parse" << EnumIdent << "(A.getValue(), Elt)) {\n";
          OS << "        Err = (llvm::Twine(\"for the "
             << getOptionDashPrefix(DisplayName) << DisplayName
             << " option: Cannot find option named '\") + A.getValue() + "
                "\"'!\").str();\n";
          OS << "        return false;\n";
          OS << "      }\n";
          OS << "      " << KeyPath << ".push_back(Elt);\n";
        }
        OS << "    }\n";
        OS << "    return true;\n";
      } else if (EnumDI) {
        StringRef EnumIdent = EnumDI->getDef()->getValueAsString("Ident");
        StringRef KeyPath = R->getValueAsString("KeyPath");
        StringRef FieldType = R->getValueAsString("FieldType");
        // A std::optional<EnumType> KeyPath (hand-authored to track "was
        // this option specified" for an enum field, mirroring OptionalField
        // for non-enum types) can't bind directly to parse<Ident>'s
        // `EnumType &Out` parameter, so parse into a temporary and assign.
        if (FieldType.starts_with("std::optional<")) {
          StringRef EnumType = EnumDI->getDef()->getValueAsString("Name");
          OS << "    {\n";
          OS << "      " << EnumType << " Tmp;\n";
          OS << "      if (!parse" << EnumIdent << "(A.getValue(), Tmp)) {\n";
          OS << "        Err = (llvm::Twine(\"for the "
             << getOptionDashPrefix(DisplayName) << DisplayName
             << " option: Cannot find option named '\") + A.getValue() + "
                "\"'!\").str();\n";
          OS << "        return false;\n";
          OS << "      }\n";
          OS << "      " << KeyPath << " = Tmp;\n";
          OS << "    }\n";
          OS << "    return true;\n";
        } else {
          OS << "    if (!parse" << EnumIdent << "(A.getValue(), " << KeyPath
             << ")) {\n";
          OS << "      Err = (llvm::Twine(\"for the "
             << getOptionDashPrefix(DisplayName) << DisplayName
             << " option: Cannot find option named '\") + A.getValue() + "
                "\"'!\").str();\n";
          OS << "      return false;\n";
          OS << "    }\n";
          OS << "    return true;\n";
        }
      } else if (HasListElement(*R)) {
        StringRef ElementType = R->getValueAsString("ElementType");
        StringRef KeyPath = R->getValueAsString("KeyPath");
        bool CommaSplit = R->getValueAsBit("CommaSplit");
        bool AlwaysPrefix = R->getValueAsBit("AlwaysPrefix");
        const char *TypeWord = getTypeArgWord(ElementType);
        if (AlwaysPrefix) {
          // AlwaysPrefixFormat (e.g. "-Dfoo=bar"): the value must be
          // attached to the same token; a bare token with nothing attached
          // is a hard error, never a prompt to consume the next token.
          OS << "    if (llvm::StringRef(A.getValue()).empty()) {\n";
          OS << "      Err = \"for the " << getOptionDashPrefix(DisplayName)
             << DisplayName << " option: requires a value!\";\n";
          OS << "      return false;\n";
          OS << "    }\n";
        }
        OS << "    {\n";
        if (CommaSplit) {
          OS << "      llvm::SmallVector<llvm::StringRef, 4> Parts;\n";
          OS << "      llvm::StringRef(A.getValue())"
                ".split(Parts, ',', -1, /*KeepEmpty=*/true);\n";
          OS << "      for (llvm::StringRef Part : Parts) {\n";
          OS << "        " << ElementType << " Elt;\n";
          OS << "        if (!llvm::opt::parseFieldValue(Part, Elt)) {\n";
          if (TypeWord) {
            OS << "          Err = (llvm::Twine(\"for the "
               << getOptionDashPrefix(DisplayName) << DisplayName
               << " option: '\") + Part + \"' value invalid for " << TypeWord
               << " argument!\").str();\n";
          } else {
            OS << "          Err = (llvm::Twine(\"for the "
               << getOptionDashPrefix(DisplayName) << DisplayName
               << " option: '\") + Part + \"' invalid value!\").str();\n";
          }
          OS << "          return false;\n";
          OS << "        }\n";
          OS << "        " << KeyPath << ".push_back(std::move(Elt));\n";
          OS << "      }\n";
        } else {
          OS << "      " << ElementType << " Elt;\n";
          OS << "      if (!llvm::opt::parseFieldValue(A.getValue(), Elt)) "
                "{\n";
          if (TypeWord) {
            OS << "        Err = (llvm::Twine(\"for the "
               << getOptionDashPrefix(DisplayName) << DisplayName
               << " option: '\") + A.getValue() + \"' value invalid for "
               << TypeWord << " argument!\").str();\n";
          } else {
            OS << "        Err = (llvm::Twine(\"for the "
               << getOptionDashPrefix(DisplayName) << DisplayName
               << " option: '\") + A.getValue() + \"' invalid value!\")"
                  ".str();\n";
          }
          OS << "        return false;\n";
          OS << "      }\n";
          OS << "      " << KeyPath << ".push_back(std::move(Elt));\n";
        }
        OS << "    }\n";
        OS << "    return true;\n";
      } else {
        StringRef FieldType = R->getValueAsString("FieldType");
        const char *TypeWord = getTypeArgWord(FieldType);
        OS << "    if (!llvm::opt::parseFieldValue(A.getValue(), "
           << R->getValueAsString("KeyPath") << ")) {\n";
        if (TypeWord) {
          OS << "      Err = (llvm::Twine(\"for the "
             << getOptionDashPrefix(DisplayName) << DisplayName
             << " option: '\") + A.getValue() + \"' value invalid for "
             << TypeWord << " argument!\").str();\n";
        } else {
          OS << "      Err = (llvm::Twine(\"for the "
             << getOptionDashPrefix(DisplayName) << DisplayName
             << " option: '\") + A.getValue() + \"' invalid value!\").str();\n";
        }
        OS << "      return false;\n";
        OS << "    }\n";
        if (const RecordVal *MaxValueField = R->getValue("MaxValue")) {
          (void)MaxValueField;
          int MaxValue = R->getValueAsInt("MaxValue");
          if (MaxValue >= 0) {
            OS << "    if (" << R->getValueAsString("KeyPath") << " > "
               << MaxValue << ") {\n";
            OS << "      Err = (llvm::Twine(\"for the "
               << getOptionDashPrefix(DisplayName) << DisplayName
               << " option: '\") + A.getValue() + \"' value must be in "
                  "the range [0, "
               << MaxValue << "]!\").str();\n";
            OS << "      return false;\n";
            OS << "    }\n";
          }
        }
        OS << "    return true;\n";
      }
    }
    OS << "  default:\n";
    OS << "    return false;\n";
    OS << "  }\n";
    OS << "}\n";
    OS << "llvm::Error " << Namespace << "::" << StructName
       << "::parse(llvm::ArrayRef<const char *> Args,\n";
    OS << "    llvm::SmallVectorImpl<const char *> &Rest, "
          "llvm::raw_ostream &Errs) {\n";
    OS << "  unsigned MissingArgIndex, MissingArgCount;\n";
    OS << "  llvm::opt::InputArgList AL = table().ParseArgs(\n";
    OS << "      Args, MissingArgIndex, MissingArgCount);\n";
    OS << "  if (MissingArgCount) {\n";
    OS << "    Errs << \"missing argument for option '\"\n";
    OS << "         << AL.getArgString(MissingArgIndex) << \"'\\n\";\n";
    OS << "    return llvm::createStringError(\n";
    OS << "        llvm::inconvertibleErrorCode(),\n";
    OS << "        \"missing argument for option\");\n";
    OS << "  }\n";
    OS << "  // Most Field<>/Assign<> options here are single-token Joined "
          "or\n";
    OS << "  // Flag options (see OptParser.td); some also accept a "
          "two-token\n";
    OS << "  // Separate/JoinedOrSeparate spelling, in which case A->getIndex()"
          "\n";
    OS << "  // is the first (option-name) token only. Either way, anything "
          "apply()\n";
    OS << "  // does not recognize (including INPUT/UNKNOWN) is forwarded to "
          "Rest\n";
    OS << "  // verbatim, starting from that index. A recognized option "
          "whose value\n";
    OS << "  // fails to parse is a hard error, not a forward: apply() "
          "reports that\n";
    OS << "  // by setting Err (see its declaration above).\n";
    if (HasPositional)
      OS << "  bool SawPositional = false;\n";
    OS << "  for (const llvm::opt::Arg *A : AL) {\n";
    if (HasPositional) {
      OS << "    if (A->getOption().getID() == OPT_INPUT) {\n";
      OS << "      if (!SawPositional) {\n";
      OS << "        " << SR.getValueAsString("PositionalName")
         << " = A->getValue();\n";
      OS << "        SawPositional = true;\n";
      OS << "      } else {\n";
      OS << "        Rest.push_back(Args[A->getIndex()]);\n";
      OS << "      }\n";
      OS << "      continue;\n";
      OS << "    }\n";
    }
    OS << "    std::string Err;\n";
    OS << "    if (!apply(*A, Err)) {\n";
    OS << "      if (Err.empty())\n";
    OS << "        Rest.push_back(Args[A->getIndex()]);\n";
    OS << "      else\n";
    OS << "        return llvm::createStringError(\n";
    OS << "            llvm::inconvertibleErrorCode(), Err);\n";
    OS << "    }\n";
    OS << "  }\n";
    OS << "  return llvm::Error::success();\n";
    OS << "}\n";
    OS << "#endif // OPTIONS_STRUCT_DEFS\n\n";
  }

  OS << "\n";
}

static TableGen::Emitter::Opt X("gen-opt-parser-defs", emitOptionParser,
                                "Generate option definitions");
