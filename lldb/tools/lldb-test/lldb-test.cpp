//===- lldb-test.cpp ------------------------------------------ *- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "FormatUtil.h"
#include "SystemInitializerTest.h"

#include "Plugins/SymbolFile/DWARF/SymbolFileDWARF.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Mangled.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Section.h"
#include "lldb/Expression/IRMemoryMap.h"
#include "lldb/Initialization/SystemLifetimeManager.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/LineTable.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/Symtab.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Symbol/TypeMap.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/StreamString.h"

#include "llvm/ADT/IntervalMap.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/WithColor.h"

#include <cstdio>
#include <optional>
#include <thread>

using namespace lldb;
using namespace lldb_private;
using namespace llvm;
using lldb_private::plugin::dwarf::SymbolFileDWARF;

namespace opts {

// Top-level option shared across multiple subcommands.
inline constexpr clv2::OptionInfo<std::string> LogOpt{
    "log", "Path to a log file", clv2::Init{""}};

/// Create a target using the file pointed to by \p Filename, or abort.
TargetSP createTarget(Debugger &Dbg, const std::string &Filename);

/// Read \p Filename into a null-terminated buffer, or abort.
std::unique_ptr<MemoryBuffer> openFile(const std::string &Filename);

namespace breakpoint {
inline constexpr clv2::OptionInfo<std::string> Target{
    "", "<target>", clv2::Positional{}, clv2::Required};
inline constexpr clv2::OptionInfo<std::string> CommandFile{
    "", "<command-file>", clv2::Positional{}, clv2::Init{"-"}};
inline constexpr clv2::OptionInfo<bool> Persistent{
    "persistent",
    "Don't automatically remove all breakpoints before each command"};

inline constexpr clv2::SubCommandInfo<&Target, &CommandFile, &Persistent>
    BreakpointCmd{"breakpoints", "Test breakpoint resolution"};

static llvm::StringRef plural(uintmax_t value) { return value == 1 ? "" : "s"; }
static void dumpState(const BreakpointList &List, LinePrinter &P);
static std::string substitute(StringRef Cmd, const std::string &CmdFile);
static int evaluateBreakpoints(Debugger &Dbg, const std::string &TargetFile,
                               const std::string &CmdFile, bool PersistentOpt);
} // namespace breakpoint

namespace object {
inline constexpr clv2::OptionInfo<bool> SectionContents{
    "contents", "Dump each section's contents"};
inline constexpr clv2::OptionInfo<bool> SectionDependentModules{
    "dep-modules", "Dump each dependent module"};
inline constexpr clv2::ListOptionInfo<std::string> InputFilenames{
    "", "<input files>", clv2::Positional{}, clv2::OneOrMore};

inline constexpr clv2::SubCommandInfo<&SectionContents,
                                      &SectionDependentModules, &InputFilenames>
    ObjectFileCmd{"object-file", "Display LLDB object file information"};
} // namespace object

namespace symtab {

/// The same enum as Mangled::NamePreference but with a default
/// 'None' case. This is needed to disambiguate wheter "ManglingPreference" was
/// explicitly set or not.
enum class ManglingPreference {
  None,
  Mangled,
  Demangled,
  MangledWithoutArguments,
};

inline constexpr clv2::EnumVal<ManglingPreference> ManglingPrefVals[] = {
    {"mangled", ManglingPreference::Mangled, "Prefer mangled"},
    {"demangled", ManglingPreference::Demangled, "Prefer demangled"},
    {"demangled-without-args", ManglingPreference::MangledWithoutArguments,
     "Prefer mangled without args"},
};

inline constexpr clv2::OptionInfo<std::string> FindSymbolsByRegex{
    "find-symbols-by-regex",
    "Dump symbols found in the symbol table matching the specified regex."};

inline constexpr auto ManglingPreferenceOpt =
    clv2::makeEnumOption<ManglingPreference>(
        "mangling-preference",
        "Preference on mangling scheme the regex should match against and "
        "dumped.",
        ManglingPrefVals);

inline constexpr clv2::OptionInfo<std::string> InputFile{
    "", "<input file>", clv2::Positional{}, clv2::Required};

inline constexpr clv2::SubCommandInfo<&FindSymbolsByRegex,
                                      &ManglingPreferenceOpt, &InputFile>
    SymTabCmd{"symtab", "Test symbol table functionality"};

/// Validate that the options passed make sense.
static std::optional<llvm::Error> validate(ManglingPreference MangPref,
                                           const std::string &FindRegex);

/// Transforms the selected mangling preference into a Mangled::NamePreference
static Mangled::NamePreference getNamePreference(ManglingPreference MangPref);

static int handleSymtabCommand(Debugger &Dbg, const std::string &InputFileName,
                               const std::string &FindRegex,
                               ManglingPreference MangPref);
} // namespace symtab

namespace symbols {

enum class FindType {
  None,
  Function,
  Block,
  Namespace,
  Type,
  Variable,
};

inline constexpr clv2::EnumVal<FindType> FindTypeVals[] = {
    {"none", FindType::None, "No search, just dump the module."},
    {"function", FindType::Function, "Find functions."},
    {"block", FindType::Block, "Find blocks."},
    {"namespace", FindType::Namespace, "Find namespaces."},
    {"type", FindType::Type, "Find types."},
    {"variable", FindType::Variable, "Find global variables."},
};

inline constexpr clv2::EnumVal<FunctionNameType> FuncNameFlagVals[] = {
    {"auto", eFunctionNameTypeAuto,
     "Automatically deduce flags based on name."},
    {"full", eFunctionNameTypeFull, "Full function name."},
    {"base", eFunctionNameTypeBase, "Base name."},
    {"method", eFunctionNameTypeMethod, "Method name."},
    {"selector", eFunctionNameTypeSelector, "Selector name."},
};

inline constexpr clv2::OptionInfo<std::string> InputFile{
    "", "<input file>", clv2::Positional{}, clv2::Required};
inline constexpr clv2::OptionInfo<std::string> SymbolPath{
    "symbol-file", "The file from which to fetch symbol information.",
    clv2::value_desc("file")};
inline constexpr auto Find =
    clv2::makeEnumOption<FindType>("find", "Choose search type:", FindTypeVals);
inline constexpr clv2::OptionInfo<std::string> Name{"name", "Name to find."};
inline constexpr clv2::OptionInfo<std::string> MangledName{
    "mangled-name",
    "Mangled name to find. Only compatible when searching types"};
inline constexpr clv2::OptionInfo<bool> Regex{
    "regex", "Search using regular expressions (available for variables "
             "and functions only)."};
inline constexpr clv2::OptionInfo<std::string> Context{
    "context", "Restrict search to the context of the given variable.",
    clv2::value_desc("variable")};
inline constexpr clv2::OptionInfo<std::string> CompilerContext{
    "compiler-context", "Specify a compiler context as \"kind:name,...\".",
    clv2::value_desc("context")};
inline constexpr clv2::OptionInfo<bool> FindInAnyModule{
    "find-in-any-module",
    "If true, the type will be searched for in all modules. Otherwise "
    "the modules must be provided in -compiler-context"};
inline constexpr clv2::OptionInfo<std::string> Language{
    "language", "Specify a language type, like C99.",
    clv2::value_desc("language")};
inline constexpr auto FunctionNameFlags =
    clv2::makeEnumListOption<FunctionNameType>(
        "function-flags", "Function search flags:", FuncNameFlagVals);
inline constexpr clv2::OptionInfo<bool> DumpAST{
    "dump-ast", "Dump AST restored from symbols."};
inline constexpr clv2::OptionInfo<bool> DumpClangAST{
    "dump-clang-ast",
    "Dump clang AST restored from symbols. When used on its own this "
    "will dump the entire AST of all loaded symbols. When combined "
    "with -find, it changes the presentation of the search results "
    "from pretty-printing the types to an AST dump."};
inline constexpr clv2::OptionInfo<bool> Verify{"verify",
                                               "Verify symbol information."};
inline constexpr clv2::OptionInfo<std::string> File{
    "file", "File (compile unit) to search."};
inline constexpr clv2::OptionInfo<int> Line{"line", "Line to search."};

inline constexpr clv2::SubCommandInfo<
    &InputFile, &SymbolPath, &Find, &Name, &MangledName, &Regex, &Context,
    &CompilerContext, &FindInAnyModule, &Language, &FunctionNameFlags, &DumpAST,
    &DumpClangAST, &Verify, &File, &Line>
    SymbolsCmd{"symbols", "Dump symbols for an object file"};

// Struct to hold parsed symbols options for passing to handlers.
struct SymbolsOpts {
  const std::string &InputFile;
  const std::string &SymbolPath;
  FindType Find;
  const std::string &Name;
  const std::string &MangledName;
  bool Regex;
  const std::string &Context;
  const std::string &CompilerContext;
  bool FindInAnyModule;
  const std::string &Language;
  const std::vector<FunctionNameType> &FunctionNameFlags;
  bool DumpAST;
  bool DumpClangAST;
  bool Verify;
  const std::string &File;
  int Line;
};

static FunctionNameType
getFunctionNameFlags(const std::vector<FunctionNameType> &Flags) {
  FunctionNameType Result = FunctionNameType(0);
  for (FunctionNameType Flag : Flags)
    Result = FunctionNameType(Result | Flag);
  return Result;
}

static Expected<CompilerDeclContext> getDeclContext(SymbolFile &Symfile,
                                                    const std::string &Ctx);

static Error findFunctions(lldb_private::Module &Module, const SymbolsOpts &SO);
static Error findBlocks(lldb_private::Module &Module, const SymbolsOpts &SO);
static Error findNamespaces(lldb_private::Module &Module,
                            const SymbolsOpts &SO);
static Error findTypes(lldb_private::Module &Module, const SymbolsOpts &SO);
static Error findVariables(lldb_private::Module &Module, const SymbolsOpts &SO);
static Error dumpModule(lldb_private::Module &Module, const SymbolsOpts &SO);
static Error dumpAST(lldb_private::Module &Module, const SymbolsOpts &SO);
static Error dumpEntireClangAST(lldb_private::Module &Module,
                                const SymbolsOpts &SO);
static Error verify(lldb_private::Module &Module, const SymbolsOpts &SO);

using ActionFn = Error (*)(lldb_private::Module &, const SymbolsOpts &);
static Expected<ActionFn> getAction(const SymbolsOpts &SO);
static int dumpSymbols(Debugger &Dbg, const SymbolsOpts &SO);
} // namespace symbols

namespace irmemorymap {
inline constexpr clv2::OptionInfo<std::string> Target{
    "", "<target>", clv2::Positional{}, clv2::Required};
inline constexpr clv2::OptionInfo<std::string> CommandFile{
    "", "<command-file>", clv2::Positional{}, clv2::Init{"-"}};
inline constexpr clv2::OptionInfo<bool> UseHostOnlyAllocationPolicy{
    "host-only", "Use the host-only allocation policy", clv2::Init{false}};

inline constexpr clv2::SubCommandInfo<&Target, &CommandFile,
                                      &UseHostOnlyAllocationPolicy>
    IRMemoryMapCmd{"ir-memory-map", "Test IRMemoryMap"};

using AllocationT = std::pair<addr_t, addr_t>;
using AddrIntervalMap =
    IntervalMap<addr_t, unsigned, 8, IntervalMapHalfOpenInfo<addr_t>>;

struct IRMemoryMapTestState {
  TargetSP Target;
  IRMemoryMap Map;

  AddrIntervalMap::Allocator IntervalMapAllocator;
  AddrIntervalMap Allocations;

  StringMap<addr_t> Label2AddrMap;

  IRMemoryMapTestState(TargetSP Target)
      : Target(Target), Map(Target), Allocations(IntervalMapAllocator) {}
};

bool evalMalloc(StringRef Line, IRMemoryMapTestState &State, bool HostOnly);
bool evalFree(StringRef Line, IRMemoryMapTestState &State);
int evaluateMemoryMapCommands(Debugger &Dbg, const std::string &TargetFile,
                              const std::string &CmdFile, bool HostOnly);
} // namespace irmemorymap

inline constexpr clv2::SubCommandInfo<> AssertCmd{"assert",
                                                  "Test assert handling"};

inline constexpr clv2::SubCommandInfo<> DwoDiagnosticSuffixCmd{
    "dwo-diagnostic-suffix",
    "Print the configured missing DWO diagnostic suffix"};

namespace assert_ns {
int lldb_assert(Debugger &Dbg);
} // namespace assert_ns

// The top-level registry, composing Log + all 7 subcommands.
inline constexpr clv2::OptionsRegistry<
    &LogOpt, &breakpoint::BreakpointCmd, &object::ObjectFileCmd,
    &symbols::SymbolsCmd, &symtab::SymTabCmd, &irmemorymap::IRMemoryMapCmd,
    &AssertCmd, &DwoDiagnosticSuffixCmd>
    LLDBTestReg;

} // namespace opts

llvm::SmallVector<CompilerContext, 4>
parseCompilerContext(const std::string &CompCtx) {
  llvm::SmallVector<CompilerContext, 4> result;
  if (CompCtx.empty())
    return result;

  StringRef str{CompCtx};
  SmallVector<StringRef, 8> entries_str;
  str.split(entries_str, ',', /*maxSplit*/-1, /*keepEmpty=*/false);
  for (auto entry_str : entries_str) {
    StringRef key, value;
    std::tie(key, value) = entry_str.split(':');
    auto kind =
        StringSwitch<CompilerContextKind>(key)
            .Case("TranslationUnit", CompilerContextKind::TranslationUnit)
            .Case("Module", CompilerContextKind::Module)
            .Case("Namespace", CompilerContextKind::Namespace)
            .Case("ClassOrStruct", CompilerContextKind::ClassOrStruct)
            .Case("Union", CompilerContextKind::Union)
            .Case("Function", CompilerContextKind::Function)
            .Case("Variable", CompilerContextKind::Variable)
            .Case("Enum", CompilerContextKind::Enum)
            .Case("Typedef", CompilerContextKind::Typedef)
            .Case("AnyType", CompilerContextKind::AnyType)
            .Default(CompilerContextKind::Invalid);
    if (value.empty()) {
      WithColor::error() << "compiler context entry has no \"name\"\n";
      exit(1);
    }
    result.push_back({kind, ConstString{value}});
  }
  outs() << "Search context: {";
  lldb_private::StreamString s;
  llvm::interleaveComma(result, s, [&](auto &ctx) { ctx.Dump(s); });
  outs() << s.GetString().str() << "}\n";

  return result;
}

template <typename... Args>
static Error make_string_error(const char *Format, Args &&... args) {
  return llvm::make_error<llvm::StringError>(
      llvm::formatv(Format, std::forward<Args>(args)...).str(),
      llvm::inconvertibleErrorCode());
}

TargetSP opts::createTarget(Debugger &Dbg, const std::string &Filename) {
  TargetSP Target;
  Status ST = Dbg.GetTargetList().CreateTarget(
      Dbg, Filename, /*triple*/ "", eLoadDependentsNo,
      /*platform_options*/ nullptr, Target);
  if (ST.Fail()) {
    errs() << formatv("Failed to create target '{0}: {1}\n", Filename, ST);
    exit(1);
  }
  return Target;
}

std::unique_ptr<MemoryBuffer> opts::openFile(const std::string &Filename) {
  auto MB = MemoryBuffer::getFileOrSTDIN(Filename);
  if (!MB) {
    errs() << formatv("Could not open file '{0}: {1}\n", Filename,
                      MB.getError().message());
    exit(1);
  }
  return std::move(*MB);
}

void opts::breakpoint::dumpState(const BreakpointList &List, LinePrinter &P) {
  P.formatLine("{0} breakpoint{1}", List.GetSize(), plural(List.GetSize()));
  if (List.GetSize() > 0)
    P.formatLine("At least one breakpoint.");
  for (size_t i = 0, e = List.GetSize(); i < e; ++i) {
    BreakpointSP BP = List.GetBreakpointAtIndex(i);
    P.formatLine("Breakpoint ID {0}:", BP->GetID());
    AutoIndent Indent(P, 2);
    P.formatLine("{0} location{1}.", BP->GetNumLocations(),
                 plural(BP->GetNumLocations()));
    if (BP->GetNumLocations() > 0)
      P.formatLine("At least one location.");
    P.formatLine("{0} resolved location{1}.", BP->GetNumResolvedLocations(),
                 plural(BP->GetNumResolvedLocations()));
    if (BP->GetNumResolvedLocations() > 0)
      P.formatLine("At least one resolved location.");
    for (size_t l = 0, le = BP->GetNumLocations(); l < le; ++l) {
      BreakpointLocationSP Loc = BP->GetLocationAtIndex(l);
      P.formatLine("Location ID {0}:", Loc->GetID());
      AutoIndent Indent(P, 2);
      P.formatLine("Enabled: {0}", Loc->IsEnabled());
      P.formatLine("Resolved: {0}", Loc->IsResolved());
      SymbolContext sc;
      Loc->GetAddress().CalculateSymbolContext(&sc);
      lldb_private::StreamString S;
      sc.DumpStopContext(&S, BP->GetTarget().GetProcessSP().get(),
                         Loc->GetAddress(), false, true, false, true, true);
      P.formatLine("Address: {0}", S.GetString());
    }
  }
  P.NewLine();
}

std::string opts::breakpoint::substitute(StringRef Cmd,
                                         const std::string &CmdFile) {
  std::string Result;
  raw_string_ostream OS(Result);
  while (!Cmd.empty()) {
    switch (Cmd[0]) {
    case '%':
      if (Cmd.consume_front("%p") && (Cmd.empty() || !isalnum(Cmd[0]))) {
        OS << sys::path::parent_path(CmdFile);
        break;
      }
      [[fallthrough]];
    default:
      size_t pos = Cmd.find('%');
      OS << Cmd.substr(0, pos);
      Cmd = Cmd.substr(pos);
      break;
    }
  }
  return Result;
}

int opts::breakpoint::evaluateBreakpoints(Debugger &Dbg,
                                          const std::string &TargetFile,
                                          const std::string &CmdFile,
                                          bool PersistentOpt) {
  TargetSP Target = opts::createTarget(Dbg, TargetFile);
  std::unique_ptr<MemoryBuffer> MB = opts::openFile(CmdFile);

  LinePrinter P(4, outs());
  StringRef Rest = MB->getBuffer();
  int HadErrors = 0;
  while (!Rest.empty()) {
    StringRef Line;
    std::tie(Line, Rest) = Rest.split('\n');
    Line = Line.ltrim().rtrim();
    if (Line.empty() || Line[0] == '#')
      continue;

    if (!PersistentOpt)
      Target->RemoveAllBreakpoints(/*internal_also*/ true);

    std::string Command = substitute(Line, CmdFile);
    P.formatLine("Command: {0}", Command);
    CommandReturnObject Result(/*colors*/ false);
    if (!Dbg.GetCommandInterpreter().HandleCommand(
            Command.c_str(), /*add_to_history*/ eLazyBoolNo, Result)) {
      P.formatLine("Failed: {0}", Result.GetErrorString());
      HadErrors = 1;
      continue;
    }

    dumpState(Target->GetBreakpointList(/*internal*/ false), P);
  }
  return HadErrors;
}

Expected<CompilerDeclContext>
opts::symbols::getDeclContext(SymbolFile &Symfile, const std::string &Ctx) {
  if (Ctx.empty())
    return CompilerDeclContext();
  VariableList List;
  Symfile.FindGlobalVariables(ConstString(Ctx), CompilerDeclContext(),
                              UINT32_MAX, List);
  if (List.Empty())
    return make_string_error("Context search didn't find a match.");
  if (List.GetSize() > 1)
    return make_string_error("Context search found multiple matches.");
  return List.GetVariableAtIndex(0)->GetDeclContext();
}

static lldb::DescriptionLevel GetDescriptionLevel(bool DumpClangAST) {
  return DumpClangAST ? eDescriptionLevelVerbose : eDescriptionLevelFull;
}

Error opts::symbols::findFunctions(lldb_private::Module &Module,
                                   const SymbolsOpts &SO) {
  if (!SO.MangledName.empty())
    return make_string_error("Cannot search functions by mangled name.");

  SymbolFile &Symfile = *Module.GetSymbolFile();
  SymbolContextList List;
  auto compiler_context = parseCompilerContext(SO.CompilerContext);
  if (!SO.File.empty()) {
    assert(SO.Line != 0);

    FileSpec src_file(SO.File);
    size_t cu_count = Module.GetNumCompileUnits();
    for (size_t i = 0; i < cu_count; i++) {
      lldb::CompUnitSP cu_sp = Module.GetCompileUnitAtIndex(i);
      if (!cu_sp)
        continue;

      LineEntry le;
      cu_sp->FindLineEntry(0, SO.Line, &src_file, false, &le);
      if (!le.IsValid())
        continue;
      const bool include_inlined_functions = false;
      auto addr =
          le.GetSameLineContiguousAddressRange(include_inlined_functions)
              .GetBaseAddress();
      if (!addr.IsValid())
        continue;

      SymbolContext sc;
      uint32_t resolved =
          addr.CalculateSymbolContext(&sc, eSymbolContextFunction);
      if (resolved & eSymbolContextFunction)
        List.Append(sc);
    }
  } else if (SO.Regex) {
    RegularExpression RE(SO.Name);
    assert(RE.IsValid());
    List.Clear();
    Symfile.FindFunctions(RE, true, List);
  } else if (!compiler_context.empty()) {
    List.Clear();
    Module.FindFunctions(compiler_context,
                         getFunctionNameFlags(SO.FunctionNameFlags), {}, List);
  } else {
    Expected<CompilerDeclContext> ContextOr =
        getDeclContext(Symfile, SO.Context);
    if (!ContextOr)
      return ContextOr.takeError();
    const CompilerDeclContext &ContextPtr =
        ContextOr->IsValid() ? *ContextOr : CompilerDeclContext();

    List.Clear();
    std::vector<lldb_private::Module::LookupInfo> lookup_infos =
        lldb_private::Module::LookupInfo::MakeLookupInfos(
            ConstString(SO.Name), getFunctionNameFlags(SO.FunctionNameFlags),
            eLanguageTypeUnknown);
    Symfile.FindFunctions(lookup_infos, ContextPtr, true, List);
  }
  outs() << formatv("Found {0} functions:\n", List.GetSize());
  StreamString Stream;
  List.Dump(&Stream, nullptr);
  outs() << Stream.GetData() << "\n";
  return Error::success();
}

Error opts::symbols::findBlocks(lldb_private::Module &Module,
                                const SymbolsOpts &SO) {
  assert(!SO.Regex);
  assert(!SO.File.empty());
  assert(SO.Line != 0);
  if (!SO.MangledName.empty())
    return make_string_error("Cannot search blocks by mangled name.");

  SymbolContextList List;

  FileSpec src_file(SO.File);
  size_t cu_count = Module.GetNumCompileUnits();
  for (size_t i = 0; i < cu_count; i++) {
    lldb::CompUnitSP cu_sp = Module.GetCompileUnitAtIndex(i);
    if (!cu_sp)
      continue;

    LineEntry le;
    cu_sp->FindLineEntry(0, SO.Line, &src_file, false, &le);
    if (!le.IsValid())
      continue;
    const bool include_inlined_functions = false;
    auto addr = le.GetSameLineContiguousAddressRange(include_inlined_functions)
                    .GetBaseAddress();
    if (!addr.IsValid())
      continue;

    SymbolContext sc;
    uint32_t resolved = addr.CalculateSymbolContext(&sc, eSymbolContextBlock);
    if (resolved & eSymbolContextBlock)
      List.Append(sc);
  }

  outs() << formatv("Found {0} blocks:\n", List.GetSize());
  StreamString Stream;
  List.Dump(&Stream, nullptr);
  outs() << Stream.GetData() << "\n";
  return Error::success();
}

Error opts::symbols::findNamespaces(lldb_private::Module &Module,
                                    const SymbolsOpts &SO) {
  if (!SO.MangledName.empty())
    return make_string_error("Cannot search namespaces by mangled name.");

  SymbolFile &Symfile = *Module.GetSymbolFile();
  Expected<CompilerDeclContext> ContextOr = getDeclContext(Symfile, SO.Context);
  if (!ContextOr)
    return ContextOr.takeError();
  const CompilerDeclContext &ContextPtr =
      ContextOr->IsValid() ? *ContextOr : CompilerDeclContext();

  CompilerDeclContext Result =
      Symfile.FindNamespace(ConstString(SO.Name), ContextPtr);
  if (Result)
    outs() << "Found namespace: "
           << Result.GetScopeQualifiedName().GetStringRef() << "\n";
  else
    outs() << "Namespace not found.\n";
  return Error::success();
}

Error opts::symbols::findTypes(lldb_private::Module &Module,
                               const SymbolsOpts &SO) {
  SymbolFile &Symfile = *Module.GetSymbolFile();
  Expected<CompilerDeclContext> ContextOr = getDeclContext(Symfile, SO.Context);
  if (!ContextOr)
    return ContextOr.takeError();
  ;

  TypeQueryOptions Opts = TypeQueryOptions::e_module_search;
  if (SO.FindInAnyModule)
    Opts |= TypeQueryOptions::e_ignore_modules;
  TypeResults results;
  if (!SO.Name.empty() && !SO.MangledName.empty())
    return make_string_error("Cannot search by both name and mangled name.");

  if (!SO.Name.empty()) {
    if (ContextOr->IsValid()) {
      TypeQuery query(*ContextOr, ConstString(SO.Name), Opts);
      if (!SO.Language.empty())
        query.AddLanguage(
            lldb_private::Language::GetLanguageTypeFromString(SO.Language));
      Symfile.FindTypes(query, results);
    } else {
      TypeQuery query(SO.Name);
      if (!SO.Language.empty())
        query.AddLanguage(
            lldb_private::Language::GetLanguageTypeFromString(SO.Language));
      Symfile.FindTypes(query, results);
    }
  } else if (!SO.MangledName.empty()) {
    Opts = TypeQueryOptions::e_search_by_mangled_name;
    if (ContextOr->IsValid()) {
      TypeQuery query(*ContextOr, ConstString(SO.MangledName), Opts);
      if (!SO.Language.empty())
        query.AddLanguage(
            lldb_private::Language::GetLanguageTypeFromString(SO.Language));
      Symfile.FindTypes(query, results);
    } else {
      TypeQuery query(SO.MangledName, Opts);
      if (!SO.Language.empty())
        query.AddLanguage(
            lldb_private::Language::GetLanguageTypeFromString(SO.Language));
      Symfile.FindTypes(query, results);
    }

  } else {
    TypeQuery query(parseCompilerContext(SO.CompilerContext), Opts);
    if (!SO.Language.empty())
      query.AddLanguage(
          lldb_private::Language::GetLanguageTypeFromString(SO.Language));
    Symfile.FindTypes(query, results);
  }
  outs() << formatv("Found {0} types:\n", results.GetTypeMap().GetSize());
  StreamString Stream;
  // Resolve types to force-materialize typedef types.
  for (const auto &type_sp : results.GetTypeMap().Types())
    type_sp->GetFullCompilerType();
  results.GetTypeMap().Dump(&Stream, false,
                            GetDescriptionLevel(SO.DumpClangAST));
  outs() << Stream.GetData() << "\n";
  return Error::success();
}

Error opts::symbols::findVariables(lldb_private::Module &Module,
                                   const SymbolsOpts &SO) {
  if (!SO.MangledName.empty())
    return make_string_error("Cannot search variables by mangled name.");

  SymbolFile &Symfile = *Module.GetSymbolFile();
  VariableList List;
  if (SO.Regex) {
    RegularExpression RE(SO.Name);
    assert(RE.IsValid());
    Symfile.FindGlobalVariables(RE, UINT32_MAX, List);
  } else if (!SO.File.empty()) {
    CompUnitSP CU;
    for (size_t Ind = 0; !CU && Ind < Module.GetNumCompileUnits(); ++Ind) {
      CompUnitSP Candidate = Module.GetCompileUnitAtIndex(Ind);
      if (!Candidate || Candidate->GetPrimaryFile().GetFilename() != SO.File)
        continue;
      if (CU)
        return make_string_error("Multiple compile units for file `{0}` found.",
                                 SO.File);
      CU = std::move(Candidate);
    }

    if (!CU)
      return make_string_error("Compile unit `{0}` not found.", SO.File);

    List.AddVariables(CU->GetVariableList(true).get());
  } else {
    Expected<CompilerDeclContext> ContextOr =
        getDeclContext(Symfile, SO.Context);
    if (!ContextOr)
      return ContextOr.takeError();
    const CompilerDeclContext &ContextPtr =
        ContextOr->IsValid() ? *ContextOr : CompilerDeclContext();

    Symfile.FindGlobalVariables(ConstString(SO.Name), ContextPtr, UINT32_MAX,
                                List);
  }
  outs() << formatv("Found {0} variables:\n", List.GetSize());
  StreamString Stream;
  List.Dump(&Stream, false);
  outs() << Stream.GetData() << "\n";
  return Error::success();
}

Error opts::symbols::dumpModule(lldb_private::Module &Module,
                                const SymbolsOpts &) {
  StreamString Stream;
  Module.ParseAllDebugSymbols();
  Module.Dump(&Stream);
  outs() << Stream.GetData() << "\n";
  return Error::success();
}

Error opts::symbols::dumpAST(lldb_private::Module &Module,
                             const SymbolsOpts &) {
  Module.ParseAllDebugSymbols();

  SymbolFile *symfile = Module.GetSymbolFile();
  if (!symfile)
    return make_string_error("Module has no symbol file.");

  auto type_system_or_err =
      symfile->GetTypeSystemForLanguage(eLanguageTypeC_plus_plus);
  if (!type_system_or_err)
    return make_string_error("Can't retrieve TypeSystemClang");

  auto ts = *type_system_or_err;
  auto *clang_ast_ctx = llvm::dyn_cast_or_null<TypeSystemClang>(ts.get());
  if (!clang_ast_ctx)
    return make_string_error("Retrieved TypeSystem was not a TypeSystemClang");

  clang::ASTContext &ast_ctx = clang_ast_ctx->getASTContext();

  clang::TranslationUnitDecl *tu = ast_ctx.getTranslationUnitDecl();
  if (!tu)
    return make_string_error("Can't retrieve translation unit declaration.");

  tu->print(outs());

  return Error::success();
}

Error opts::symbols::dumpEntireClangAST(lldb_private::Module &Module,
                                        const SymbolsOpts &SO) {
  Module.ParseAllDebugSymbols();

  SymbolFile *symfile = Module.GetSymbolFile();
  if (!symfile)
    return make_string_error("Module has no symbol file.");

  auto type_system_or_err =
      symfile->GetTypeSystemForLanguage(eLanguageTypeObjC_plus_plus);
  if (!type_system_or_err)
    return make_string_error("Can't retrieve TypeSystemClang");
  auto ts = *type_system_or_err;
  auto *clang_ast_ctx = llvm::dyn_cast_or_null<TypeSystemClang>(ts.get());
  if (!clang_ast_ctx)
    return make_string_error("Retrieved TypeSystem was not a TypeSystemClang");

  StreamString Stream;
  clang_ast_ctx->DumpFromSymbolFile(Stream, SO.Name);
  outs() << Stream.GetData() << "\n";

  return Error::success();
}

Error opts::symbols::verify(lldb_private::Module &Module, const SymbolsOpts &) {
  SymbolFile *symfile = Module.GetSymbolFile();
  if (!symfile)
    return make_string_error("Module has no symbol file.");

  uint32_t comp_units_count = symfile->GetNumCompileUnits();

  outs() << "Found " << comp_units_count << " compile units.\n";

  for (uint32_t i = 0; i < comp_units_count; i++) {
    lldb::CompUnitSP comp_unit = symfile->GetCompileUnitAtIndex(i);
    if (!comp_unit)
      return make_string_error("Cannot parse compile unit {0}.", i);

    outs() << "Processing '" << comp_unit->GetPrimaryFile().GetFilename()
           << "' compile unit.\n";

    LineTable *lt = comp_unit->GetLineTable();
    if (!lt)
      return make_string_error("Can't get a line table of a compile unit.");

    uint32_t count = lt->GetSize();

    outs() << "The line table contains " << count << " entries.\n";

    if (count == 0)
      continue;

    LineEntry le;
    if (!lt->GetLineEntryAtIndex(0, le))
      return make_string_error("Can't get a line entry of a compile unit.");

    for (uint32_t i = 1; i < count; i++) {
      lldb::addr_t curr_end =
          le.range.GetBaseAddress().GetFileAddress() + le.range.GetByteSize();

      if (!lt->GetLineEntryAtIndex(i, le))
        return make_string_error("Can't get a line entry of a compile unit");

      if (curr_end > le.range.GetBaseAddress().GetFileAddress())
        return make_string_error(
            "Line table of a compile unit is inconsistent.");
    }
  }

  outs() << "The symbol information is verified.\n";

  return Error::success();
}

Expected<opts::symbols::ActionFn>
opts::symbols::getAction(const SymbolsOpts &SO) {
  if (SO.Verify && SO.DumpAST)
    return make_string_error(
        "Cannot both verify symbol information and dump AST.");

  if (SO.Verify) {
    if (SO.Find != FindType::None)
      return make_string_error(
          "Cannot both search and verify symbol information.");
    if (SO.Regex || !SO.Context.empty() || !SO.Name.empty() ||
        !SO.File.empty() || SO.Line != 0)
      return make_string_error(
          "-regex, -context, -name, -file and -line options are not "
          "applicable for symbol verification.");
    return verify;
  }

  if (SO.DumpAST) {
    if (SO.Find != FindType::None)
      return make_string_error("Cannot both search and dump AST.");
    if (SO.Regex || !SO.Context.empty() || !SO.Name.empty() ||
        !SO.File.empty() || SO.Line != 0)
      return make_string_error(
          "-regex, -context, -name, -file and -line options are not "
          "applicable for dumping AST.");
    return dumpAST;
  }

  if (SO.DumpClangAST) {
    if (SO.Find == FindType::None) {
      if (SO.Regex || !SO.Context.empty() || !SO.File.empty() || SO.Line != 0)
        return make_string_error(
            "-regex, -context, -name, -file and -line options are not "
            "applicable for dumping the entire clang AST. Either combine with "
            "-find, or use -dump-clang-ast as a standalone option.");
      return dumpEntireClangAST;
    }
    if (SO.Find != FindType::Type)
      return make_string_error("This combination of -dump-clang-ast and -find "
                               "<kind> is not yet implemented.");
  }

  if (SO.Regex && !SO.Context.empty())
    return make_string_error(
        "Cannot search using both regular expressions and context.");

  if (SO.Regex && !RegularExpression(SO.Name).IsValid())
    return make_string_error("`{0}` is not a valid regular expression.",
                             SO.Name);

  if (SO.Regex + !SO.Context.empty() + !SO.File.empty() >= 2)
    return make_string_error(
        "Only one of -regex, -context and -file may be used simultaneously.");
  if (SO.Regex && SO.Name.empty())
    return make_string_error("-regex used without a -name");

  if (SO.FindInAnyModule && (SO.Find != FindType::Type))
    return make_string_error("-find-in-any-module only works with -find=type");

  switch (SO.Find) {
  case FindType::None:
    if (!SO.Context.empty() || !SO.Name.empty() || !SO.File.empty() ||
        SO.Line != 0)
      return make_string_error(
          "Specify search type (-find) to use search options.");
    return dumpModule;

  case FindType::Function:
    if (!SO.File.empty() + (SO.Line != 0) == 1)
      return make_string_error("Both file name and line number must be "
                               "specified when searching a function "
                               "by file position.");
    if (SO.Regex + (getFunctionNameFlags(SO.FunctionNameFlags) != 0) +
            !SO.File.empty() >=
        2)
      return make_string_error("Only one of regular expression, function-flags "
                               "and file position may be used simultaneously "
                               "when searching a function.");
    return findFunctions;

  case FindType::Block:
    if (SO.File.empty() || SO.Line == 0)
      return make_string_error("Both file name and line number must be "
                               "specified when searching a block.");
    if (SO.Regex || getFunctionNameFlags(SO.FunctionNameFlags) != 0)
      return make_string_error("Cannot use regular expression or "
                               "function-flags for searching a block.");
    return findBlocks;

  case FindType::Namespace:
    if (SO.Regex || !SO.File.empty() || SO.Line != 0)
      return make_string_error("Cannot search for namespaces using regular "
                               "expressions, file names or line numbers.");
    return findNamespaces;

  case FindType::Type:
    if (SO.Regex || !SO.File.empty() || SO.Line != 0)
      return make_string_error("Cannot search for types using regular "
                               "expressions, file names or line numbers.");
    if (!SO.Name.empty() && !SO.CompilerContext.empty())
      return make_string_error("Name is ignored if compiler context present.");

    return findTypes;

  case FindType::Variable:
    if (SO.Line != 0)
      return make_string_error("Cannot search for variables "
                               "using line numbers.");
    return findVariables;
  }

  llvm_unreachable("Unsupported symbol action.");
}

std::optional<llvm::Error>
opts::symtab::validate(ManglingPreference MangPref,
                       const std::string &FindRegex) {
  if (MangPref != ManglingPreference::None && FindRegex.empty())
    return make_string_error("Mangling preference set but no regex specified.");

  return {};
}

static Mangled::NamePreference
opts::symtab::getNamePreference(ManglingPreference MangPref) {
  switch (MangPref) {
  case ManglingPreference::None:
  case ManglingPreference::Mangled:
    return Mangled::ePreferMangled;
  case ManglingPreference::Demangled:
    return Mangled::ePreferDemangled;
  case ManglingPreference::MangledWithoutArguments:
    return Mangled::ePreferDemangledWithoutArguments;
  }
  llvm_unreachable("Fully covered switch above!");
}

int opts::symtab::handleSymtabCommand(Debugger &Dbg,
                                      const std::string &InputFileName,
                                      const std::string &FindRegex,
                                      ManglingPreference MangPref) {
  if (auto error = validate(MangPref, FindRegex)) {
    logAllUnhandledErrors(std::move(*error), WithColor::error(), "");
    return 1;
  }

  if (!FindRegex.empty()) {
    ModuleSpec Spec{FileSpec(InputFileName)};

    auto ModulePtr = std::make_shared<lldb_private::Module>(Spec);
    auto *Symtab = ModulePtr->GetSymtab();
    auto NamePreference = getNamePreference(MangPref);
    std::vector<uint32_t> Indexes;

    Symtab->FindAllSymbolsMatchingRexExAndType(
        RegularExpression(FindRegex), lldb::eSymbolTypeAny, Symtab::eDebugAny,
        Symtab::eVisibilityAny, Indexes, NamePreference);
    for (auto i : Indexes) {
      auto *symbol = Symtab->SymbolAtIndex(i);
      if (symbol) {
        StreamString stream;
        symbol->Dump(&stream, nullptr, i, NamePreference);
        outs() << stream.GetString();
      }
    }
  }

  return 0;
}

int opts::symbols::dumpSymbols(Debugger &Dbg, const SymbolsOpts &SO) {
  auto ActionOr = getAction(SO);
  if (!ActionOr) {
    logAllUnhandledErrors(ActionOr.takeError(), WithColor::error(), "");
    return 1;
  }
  auto Action = *ActionOr;

  outs() << "Module: " << SO.InputFile << "\n";
  ModuleSpec Spec{FileSpec(SO.InputFile)};
  StringRef Symbols = SO.SymbolPath.empty() ? SO.InputFile : SO.SymbolPath;
  Spec.GetSymbolFileSpec().SetFile(Symbols, FileSpec::Style::native);

  auto ModulePtr = std::make_shared<lldb_private::Module>(Spec);
  SymbolFile *Symfile = ModulePtr->GetSymbolFile();
  if (!Symfile) {
    WithColor::error() << "Module has no symbol vendor.\n";
    return 1;
  }

  if (Error E = Action(*ModulePtr, SO)) {
    WithColor::error() << toString(std::move(E)) << "\n";
    return 1;
  }

  return 0;
}

static void dumpSectionList(LinePrinter &Printer, const SectionList &List,
                            bool is_subsection, bool ShowContents) {
  size_t Count = List.GetNumSections(0);
  if (Count == 0) {
    Printer.formatLine("There are no {0}sections", is_subsection ? "sub" : "");
    return;
  }
  Printer.formatLine("Showing {0} {1}sections", Count,
                     is_subsection ? "sub" : "");
  for (size_t I = 0; I < Count; ++I) {
    auto S = List.GetSectionAtIndex(I);
    assert(S);
    AutoIndent Indent(Printer, 2);
    Printer.formatLine("Index: {0}", I);
    Printer.formatLine("ID: {0:x}", S->GetID());
    Printer.formatLine("Name: {0}", S->GetName());
    Printer.formatLine("Type: {0}", S->GetTypeAsCString());
    Printer.formatLine("Permissions: {0}", GetPermissionsAsCString(S->GetPermissions()));
    Printer.formatLine("Thread specific: {0:y}", S->IsThreadSpecific());
    Printer.formatLine("VM address: {0:x}", S->GetFileAddress());
    Printer.formatLine("VM size: {0}", S->GetByteSize());
    Printer.formatLine("File size: {0}", S->GetFileSize());

    if (ShowContents) {
      lldb_private::DataExtractor Data;
      S->GetSectionData(Data);
      ArrayRef<uint8_t> Bytes(Data.GetDataStart(), Data.GetDataEnd());
      Printer.formatBinary("Data: ", Bytes, 0);
    }

    if (S->GetType() == eSectionTypeContainer)
      dumpSectionList(Printer, S->GetChildren(), true, ShowContents);
    Printer.NewLine();
  }
}

static int dumpObjectFiles(Debugger &Dbg,
                           const std::vector<std::string> &InputFilenames,
                           bool ShowContents, bool ShowDepModules) {
  LinePrinter Printer(4, llvm::outs());

  int HadErrors = 0;
  for (const auto &File : InputFilenames) {
    ModuleSpec Spec{FileSpec(File)};

    auto ModulePtr = std::make_shared<lldb_private::Module>(Spec);

    ObjectFile *ObjectPtr = ModulePtr->GetObjectFile();
    if (!ObjectPtr) {
      WithColor::error() << File << " not recognised as an object file\n";
      HadErrors = 1;
      continue;
    }

    // Fetch symbol vendor before we get the section list to give the symbol
    // vendor a chance to populate it.
    ModulePtr->GetSymbolFile();
    SectionList *Sections = ModulePtr->GetSectionList();
    if (!Sections) {
      llvm::errs() << "Could not load sections for module " << File << "\n";
      HadErrors = 1;
      continue;
    }

    Printer.formatLine("Plugin name: {0}", ObjectPtr->GetPluginName());
    Printer.formatLine("Architecture: {0}",
                       ModulePtr->GetArchitecture().GetTriple().getTriple());
    Printer.formatLine("UUID: {0}", ModulePtr->GetUUID().GetAsString());
    Printer.formatLine("Executable: {0}", ObjectPtr->IsExecutable());
    Printer.formatLine("Stripped: {0}", ObjectPtr->IsStripped());
    Printer.formatLine("Type: {0}", ObjectPtr->GetType());
    Printer.formatLine("Strata: {0}", ObjectPtr->GetStrata());
    Printer.formatLine("Base VM address: {0:x}",
                       ObjectPtr->GetBaseAddress().GetFileAddress());

    dumpSectionList(Printer, *Sections, /*is_subsection*/ false, ShowContents);

    if (ShowDepModules) {
      // A non-empty section list ensures a valid object file.
      auto Obj = ModulePtr->GetObjectFile();
      FileSpecList Files;
      auto Count = Obj->GetDependentModules(Files);
      Printer.formatLine("Showing {0} dependent module(s)", Count);
      for (size_t I = 0; I < Files.GetSize(); ++I) {
        AutoIndent Indent(Printer, 2);
        Printer.formatLine("Name: {0}",
                           Files.GetFileSpecAtIndex(I).GetPath());
      }
      Printer.NewLine();
    }
  }
  return HadErrors;
}

bool opts::irmemorymap::evalMalloc(StringRef Line, IRMemoryMapTestState &State,
                                   bool HostOnly) {
  // ::= <label> = malloc <size> <alignment>
  StringRef Label;
  std::tie(Label, Line) = Line.split('=');
  if (Line.empty())
    return false;
  Label = Label.trim();
  Line = Line.trim();
  size_t Size;
  uint8_t Alignment;
  int Matches = sscanf(Line.data(), "malloc %zu %hhu", &Size, &Alignment);
  if (Matches != 2)
    return false;

  outs() << formatv("Command: {0} = malloc(size={1}, alignment={2})\n", Label,
                    Size, Alignment);
  if (!isPowerOf2_32(Alignment)) {
    outs() << "Malloc error: alignment is not a power of 2\n";
    exit(1);
  }

  IRMemoryMap::AllocationPolicy AP =
      HostOnly ? IRMemoryMap::eAllocationPolicyHostOnly
               : IRMemoryMap::eAllocationPolicyProcessOnly;

  // Issue the malloc in the target process with "-rw" permissions.
  const uint32_t Permissions = 0x3;
  const bool ZeroMemory = false;
  auto AddrOrErr =
      State.Map.Malloc(Size, Alignment, Permissions, AP, ZeroMemory);
  if (!AddrOrErr) {
    outs() << formatv("Malloc error: {0}\n", toString(AddrOrErr.takeError()));
    return true;
  }
  addr_t Addr = *AddrOrErr;

  // Print the result of the allocation before checking its validity.
  outs() << formatv("Malloc: address = {0:x}\n", Addr);

  // Check that the allocation is aligned.
  if (!Addr || Addr % Alignment != 0) {
    outs() << "Malloc error: zero or unaligned allocation detected\n";
    exit(1);
  }

  // In case of Size == 0, we still expect the returned address to be unique and
  // non-overlapping.
  addr_t EndOfRegion = Addr + std::max<size_t>(Size, 1);
  if (State.Allocations.overlaps(Addr, EndOfRegion)) {
    auto I = State.Allocations.find(Addr);
    outs() << "Malloc error: overlapping allocation detected"
           << formatv(", previous allocation at [{0:x}, {1:x})\n", I.start(),
                      I.stop());
    exit(1);
  }

  // Insert the new allocation into the interval map. Use unique allocation
  // IDs to inhibit interval coalescing.
  static unsigned AllocationID = 0;
  State.Allocations.insert(Addr, EndOfRegion, AllocationID++);

  // Store the label -> address mapping.
  State.Label2AddrMap[Label] = Addr;

  return true;
}

bool opts::irmemorymap::evalFree(StringRef Line, IRMemoryMapTestState &State) {
  // ::= free <label>
  if (!Line.consume_front("free"))
    return false;
  StringRef Label = Line.trim();

  outs() << formatv("Command: free({0})\n", Label);
  auto LabelIt = State.Label2AddrMap.find(Label);
  if (LabelIt == State.Label2AddrMap.end()) {
    outs() << "Free error: Invalid allocation label\n";
    exit(1);
  }

  Status ST;
  addr_t Addr = LabelIt->getValue();
  State.Map.Free(Addr, ST);
  if (ST.Fail()) {
    outs() << formatv("Free error: {0}\n", ST);
    exit(1);
  }

  // Erase the allocation from the live interval map.
  auto Interval = State.Allocations.find(Addr);
  if (Interval != State.Allocations.end()) {
    outs() << formatv("Free: [{0:x}, {1:x})\n", Interval.start(),
                      Interval.stop());
    Interval.erase();
  }

  return true;
}

int opts::irmemorymap::evaluateMemoryMapCommands(Debugger &Dbg,
                                                 const std::string &TargetFile,
                                                 const std::string &CmdFile,
                                                 bool HostOnly) {
  // Set up a Target.
  TargetSP Target = opts::createTarget(Dbg, TargetFile);

  // Set up a Process. In order to allocate memory within a target, this
  // process must be alive and must support JIT'ing.
  CommandReturnObject Result(/*colors*/ false);
  Dbg.SetAsyncExecution(false);
  CommandInterpreter &CI = Dbg.GetCommandInterpreter();
  auto IssueCmd = [&](const char *Cmd) -> bool {
    return CI.HandleCommand(Cmd, eLazyBoolNo, Result);
  };
  if (!IssueCmd("b main") || !IssueCmd("run")) {
    outs() << formatv("Failed: {0}\n", Result.GetErrorString());
    exit(1);
  }

  ProcessSP Process = Target->GetProcessSP();
  if (!Process || !Process->IsAlive() || !Process->CanJIT()) {
    outs() << "Cannot use process to test IRMemoryMap\n";
    exit(1);
  }

  // Set up an IRMemoryMap and associated testing state.
  IRMemoryMapTestState State(Target);

  // Parse and apply commands from the command file.
  std::unique_ptr<MemoryBuffer> MB = opts::openFile(CmdFile);
  StringRef Rest = MB->getBuffer();
  while (!Rest.empty()) {
    StringRef Line;
    std::tie(Line, Rest) = Rest.split('\n');
    Line = Line.ltrim().rtrim();

    if (Line.empty() || Line[0] == '#')
      continue;

    if (evalMalloc(Line, State, HostOnly))
      continue;

    if (evalFree(Line, State))
      continue;

    errs() << "Could not parse line: " << Line << "\n";
    exit(1);
  }
  return 0;
}

int opts::assert_ns::lldb_assert(Debugger &Dbg) {
  lldbassert(false && "lldb-test assert");
  return 1;
}

int main(int argc, const char *argv[]) {
  StringRef ToolName = argv[0];
  sys::PrintStackTraceOnErrorSignal(ToolName);
  PrettyStackTraceProgram X(argc, argv);
  llvm_shutdown_obj Y;

  clv2::OptionParser P;
  P.add<&opts::LLDBTestReg>();
  RegisterAllLLVMOptions(P);
  auto OptsCtx = P.parse(argc, argv, "LLDB Testing Utility\n");
  auto *Opts = OptsCtx->getViewPtr<&opts::LLDBTestReg>();

  if (Opts->isActive<&opts::DwoDiagnosticSuffixCmd>()) {
    outs() << SymbolFileDWARF::GetDwoDiagnosticSuffix() << '\n';
    return 0;
  }

  SystemLifetimeManager DebuggerLifetime;
  if (auto e = DebuggerLifetime.Initialize(
          std::make_unique<SystemInitializerTest>())) {
    WithColor::error() << "initialization failed: " << toString(std::move(e))
                       << '\n';
    return 1;
  }

  llvm::scope_exit TerminateDebugger([&] { DebuggerLifetime.Terminate(); });

  auto Dbg = lldb_private::Debugger::CreateInstance();
  ModuleList::GetGlobalModuleListProperties().SetEnableExternalLookup(false);
  CommandReturnObject Result(/*colors*/ false);
  Dbg->GetCommandInterpreter().HandleCommand(
      "settings set plugin.process.gdb-remote.packet-timeout 60",
      /*add_to_history*/ eLazyBoolNo, Result);
  Dbg->GetCommandInterpreter().HandleCommand(
      "settings set target.inherit-tcc true",
      /*add_to_history*/ eLazyBoolNo, Result);
  Dbg->GetCommandInterpreter().HandleCommand(
      "settings set target.detach-on-error false",
      /*add_to_history*/ eLazyBoolNo, Result);

  std::string Log = Opts->get<&opts::LogOpt>();
  if (!Log.empty())
    if (llvm::Error e =
            Dbg->EnableLog("lldb", {"all"}, Log, 0, 0, eLogHandlerStream))
      WithColor::error() << "failed to enable logs: " << toString(std::move(e))
                         << '\n';

  if (Opts->isActive<&opts::breakpoint::BreakpointCmd>()) {
    auto &Sub = Opts->getSubOptions<&opts::breakpoint::BreakpointCmd>();
    return opts::breakpoint::evaluateBreakpoints(
        *Dbg, Sub.get<&opts::breakpoint::Target>(),
        Sub.get<&opts::breakpoint::CommandFile>(),
        Sub.get<&opts::breakpoint::Persistent>());
  }
  if (Opts->isActive<&opts::object::ObjectFileCmd>()) {
    auto &Sub = Opts->getSubOptions<&opts::object::ObjectFileCmd>();
    return dumpObjectFiles(*Dbg, Sub.get<&opts::object::InputFilenames>(),
                           Sub.get<&opts::object::SectionContents>(),
                           Sub.get<&opts::object::SectionDependentModules>());
  }
  if (Opts->isActive<&opts::symbols::SymbolsCmd>()) {
    auto &Sub = Opts->getSubOptions<&opts::symbols::SymbolsCmd>();
    opts::symbols::SymbolsOpts SO{
        Sub.get<&opts::symbols::InputFile>(),
        Sub.get<&opts::symbols::SymbolPath>(),
        Sub.get<&opts::symbols::Find>(),
        Sub.get<&opts::symbols::Name>(),
        Sub.get<&opts::symbols::MangledName>(),
        Sub.get<&opts::symbols::Regex>(),
        Sub.get<&opts::symbols::Context>(),
        Sub.get<&opts::symbols::CompilerContext>(),
        Sub.get<&opts::symbols::FindInAnyModule>(),
        Sub.get<&opts::symbols::Language>(),
        Sub.get<&opts::symbols::FunctionNameFlags>(),
        Sub.get<&opts::symbols::DumpAST>(),
        Sub.get<&opts::symbols::DumpClangAST>(),
        Sub.get<&opts::symbols::Verify>(),
        Sub.get<&opts::symbols::File>(),
        Sub.get<&opts::symbols::Line>(),
    };
    return opts::symbols::dumpSymbols(*Dbg, SO);
  }
  if (Opts->isActive<&opts::symtab::SymTabCmd>()) {
    auto &Sub = Opts->getSubOptions<&opts::symtab::SymTabCmd>();
    return opts::symtab::handleSymtabCommand(
        *Dbg, Sub.get<&opts::symtab::InputFile>(),
        Sub.get<&opts::symtab::FindSymbolsByRegex>(),
        Sub.get<&opts::symtab::ManglingPreferenceOpt>());
  }
  if (Opts->isActive<&opts::irmemorymap::IRMemoryMapCmd>()) {
    auto &Sub = Opts->getSubOptions<&opts::irmemorymap::IRMemoryMapCmd>();
    return opts::irmemorymap::evaluateMemoryMapCommands(
        *Dbg, Sub.get<&opts::irmemorymap::Target>(),
        Sub.get<&opts::irmemorymap::CommandFile>(),
        Sub.get<&opts::irmemorymap::UseHostOnlyAllocationPolicy>());
  }
  if (Opts->isActive<&opts::AssertCmd>())
    return opts::assert_ns::lldb_assert(*Dbg);

  WithColor::error() << "No command specified.\n";
  return 1;
}
