//===--- Dexp.cpp - Dex EXPloration tool ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a simple interactive tool which can be used to manually
// evaluate symbol search quality of Clangd index.
//
//===----------------------------------------------------------------------===//

#include "index/Index.h"
#include "index/Relation.h"
#include "index/Serialization.h"
#include "index/remote/Client.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/LineEditor/LineEditor.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/Signals.h"
#include <optional>

namespace clang {
namespace clangd {
namespace {

using llvm::clv2::Init;
using llvm::clv2::OptionInfo;
using llvm::clv2::OptionsRegistry;
using llvm::clv2::Positional;

static std::string IndexLocation;
static std::string ExecCommand;
static std::string ProjectRoot;

static constexpr OptionInfo<std::string> dexpIndexLocationOpt{
    "", "<path to index file | remote:server.address>", Positional{}};
static constexpr OptionInfo<std::string> dexpExecCommandOpt{
    "c", "Command to execute and then exit."};
static constexpr OptionInfo<std::string> dexpProjectRootOpt{
    "project-root",
    "Path to the project. Required when connecting using remote index."};

static constexpr OptionsRegistry<&dexpIndexLocationOpt, &dexpExecCommandOpt,
                                 &dexpProjectRootOpt>
    DexpGlobalReg;

static constexpr char Overview[] = R"(
This is an **experimental** interactive tool to process user-provided search
queries over given symbol collection obtained via clangd-indexer. The
tool can be used to evaluate search quality of existing index implementations
and manually construct non-trivial test cases.

You can connect to remote index by passing remote:address to dexp. Example:

$ dexp remote:0.0.0.0:9000

Type use "help" request to get information about the details.
)";

void reportTime(llvm::StringRef Name, llvm::function_ref<void()> F) {
  const auto TimerStart = std::chrono::high_resolution_clock::now();
  F();
  const auto TimerStop = std::chrono::high_resolution_clock::now();
  const auto Duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      TimerStop - TimerStart);
  llvm::outs() << llvm::formatv("{0} took {1:ms+n}.\n", Name, Duration);
}

std::vector<SymbolID> getSymbolIDsFromIndex(llvm::StringRef QualifiedName,
                                            const SymbolIndex *Index) {
  FuzzyFindRequest Request;
  // Remove leading "::" qualifier as FuzzyFind doesn't need leading "::"
  // qualifier for global scope.
  bool IsGlobalScope = QualifiedName.consume_front("::");
  auto Names = splitQualifiedName(QualifiedName);
  if (IsGlobalScope || !Names.first.empty())
    Request.Scopes = {std::string(Names.first)};
  else
    // QualifiedName refers to a symbol in global scope (e.g. "GlobalSymbol"),
    // add the global scope to the request.
    Request.Scopes = {""};

  Request.Query = std::string(Names.second);
  std::vector<SymbolID> SymIDs;
  Index->fuzzyFind(Request, [&](const Symbol &Sym) {
    std::string SymQualifiedName = (Sym.Scope + Sym.Name).str();
    if (QualifiedName == SymQualifiedName)
      SymIDs.push_back(Sym.ID);
  });
  return SymIDs;
}

class Command {
  virtual void run() = 0;
  virtual void addOptions(llvm::clv2::OptionParser &P) = 0;

protected:
  const SymbolIndex *Index;

public:
  virtual ~Command() = default;
  bool parseAndRun(llvm::ArrayRef<const char *> Argv, const char *Overview,
                   const SymbolIndex &Index) {
    std::string ParseErrs;
    llvm::raw_string_ostream OS(ParseErrs);
    llvm::clv2::OptionParser SubP;
    addOptions(SubP);
    SubP.parse(Argv.size(), Argv.data(), Overview, &OS);

    llvm::outs() << OS.str();
    this->Index = &Index;
    reportTime(Argv[0], [&] { run(); });
    return true;
  }
};

// FIXME(kbobyrev): Ideas for more commands:
// * load/swap/reload index: this would make it possible to get rid of llvm::cl
//   usages in the tool driver and actually use llvm::cl library in the REPL.
// * show posting list density histogram (our dump data somewhere so that user
//   could build one)
// * show number of tokens of each kind
// * print out tokens with the most dense posting lists
// * print out tokens with least dense posting lists

static constexpr OptionInfo<std::string> OI_FFQuery{
    "", "Query string to be fuzzy-matched", Positional{}, llvm::clv2::Required};
static constexpr OptionInfo<std::string> OI_FFScopes{
    "scopes", "Allowed symbol scopes (comma-separated list)"};
static constexpr OptionInfo<unsigned> OI_FFLimit{
    "limit", "Max results to display", Init{10u}};

class FuzzyFind : public Command {
  std::string Query;
  std::string Scopes;
  unsigned Limit = 10;
  unsigned QueryCount = 0;
  unsigned ScopesCount = 0;
  unsigned LimitCount = 0;

  void addOptions(llvm::clv2::OptionParser &P) override {
    using namespace llvm::clv2;
    P.addDynamicEntry(llvm::clv2::makeEntry<&OI_FFQuery>(Query, QueryCount));
    P.addDynamicEntry(llvm::clv2::makeEntry<&OI_FFScopes>(Scopes, ScopesCount));
    P.addDynamicEntry(llvm::clv2::makeEntry<&OI_FFLimit>(Limit, LimitCount));
  }

private:
  void run() override {
    FuzzyFindRequest Request;
    Request.Limit = Limit;
    Request.Query = Query;
    if (ScopesCount > 0) {
      llvm::SmallVector<llvm::StringRef> ScopeList;
      llvm::StringRef(Scopes).split(ScopeList, ',');
      Request.Scopes = {ScopeList.begin(), ScopeList.end()};
    }
    Request.AnyScope = Request.Scopes.empty();
    // FIXME(kbobyrev): Print symbol final scores to see the distribution.
    static const auto *OutputFormat = "{0,-4} | {1,-40} | {2,-25}\n";
    llvm::outs() << llvm::formatv(OutputFormat, "Rank", "Symbol ID",
                                  "Symbol Name");
    size_t Rank = 0;
    Index->fuzzyFind(Request, [&](const Symbol &Sym) {
      llvm::outs() << llvm::formatv(OutputFormat, Rank++, Sym.ID.str(),
                                    Sym.Scope + Sym.Name);
    });
  }
};

static constexpr OptionInfo<std::string> OI_LookupId{
    "id", "Symbol ID to look up (hex)", Positional{}};
static constexpr OptionInfo<std::string> OI_LookupName{
    "name", "Qualified name to look up."};

class Lookup : public Command {
  std::string ID;
  std::string Name;
  unsigned IDCount = 0;
  unsigned NameCount = 0;

  void addOptions(llvm::clv2::OptionParser &P) override {
    using namespace llvm::clv2;
    P.addDynamicEntry(llvm::clv2::makeEntry<&OI_LookupId>(ID, IDCount));
    P.addDynamicEntry(llvm::clv2::makeEntry<&OI_LookupName>(Name, NameCount));
  }

private:
  void run() override {
    if (IDCount == 0 && NameCount == 0) {
      llvm::errs()
          << "Missing required argument: please provide id or -name.\n";
      return;
    }
    std::vector<SymbolID> IDs;
    if (IDCount > 0) {
      auto SID = SymbolID::fromStr(ID);
      if (!SID) {
        llvm::errs() << llvm::toString(SID.takeError()) << "\n";
        return;
      }
      IDs.push_back(*SID);
    } else {
      IDs = getSymbolIDsFromIndex(Name, Index);
    }

    LookupRequest Request;
    Request.IDs.insert_range(IDs);
    bool FoundSymbol = false;
    Index->lookup(Request, [&](const Symbol &Sym) {
      FoundSymbol = true;
      llvm::outs() << toYAML(Sym);
    });
    if (!FoundSymbol)
      llvm::errs() << "not found\n";
  }
};

static constexpr OptionInfo<std::string> OI_RefsId{
    "id", "Symbol ID of the symbol being queried (hex).", Positional{}};
static constexpr OptionInfo<std::string> OI_RefsName{
    "name", "Qualified name of the symbol being queried."};
static constexpr OptionInfo<std::string> OI_RefsFilter{
    "filter", "Print all results from files matching this regular expression.",
    Init{".*"}};

class Refs : public Command {
  std::string ID;
  std::string Name;
  std::string Filter = ".*";
  unsigned IDCount = 0;
  unsigned NameCount = 0;
  unsigned FilterCount = 0;

  void addOptions(llvm::clv2::OptionParser &P) override {
    using namespace llvm::clv2;
    P.addDynamicEntry(llvm::clv2::makeEntry<&OI_RefsId>(ID, IDCount));
    P.addDynamicEntry(llvm::clv2::makeEntry<&OI_RefsName>(Name, NameCount));
    P.addDynamicEntry(
        llvm::clv2::makeEntry<&OI_RefsFilter>(Filter, FilterCount));
  }

private:
  void run() override {
    if (IDCount == 0 && NameCount == 0) {
      llvm::errs()
          << "Missing required argument: please provide id or -name.\n";
      return;
    }
    std::vector<SymbolID> IDs;
    if (IDCount > 0) {
      auto SID = SymbolID::fromStr(ID);
      if (!SID) {
        llvm::errs() << llvm::toString(SID.takeError()) << "\n";
        return;
      }
      IDs.push_back(*SID);
    } else {
      IDs = getSymbolIDsFromIndex(Name, Index);
      if (IDs.size() > 1) {
        llvm::errs() << llvm::formatv(
            "The name {0} is ambiguous, found {1} different "
            "symbols. Please use id flag to disambiguate.\n",
            Name, IDs.size());
        return;
      }
    }
    RefsRequest RefRequest;
    RefRequest.IDs.insert_range(IDs);
    llvm::Regex RegexFilter(Filter);
    Index->refs(RefRequest, [&RegexFilter](const Ref &R) {
      auto U = URI::parse(R.Location.FileURI);
      if (!U) {
        llvm::errs() << U.takeError();
        return;
      }
      if (RegexFilter.match(U->body()))
        llvm::outs() << R << "\n";
    });
  }
};

static constexpr OptionInfo<std::string> OI_RelationsId{
    "id", "Symbol ID of the symbol being queried (hex).", Positional{}};
static constexpr llvm::clv2::EnumVal<RelationKind> RelationKindVals[] = {
    {"base_of", RelationKind::BaseOf, "Find subclasses of a class."},
    {"overridden_by", RelationKind::OverriddenBy,
     "Find methods that overrides a virtual method."},
};
// CaseInsensitiveValues preserves the previous equals_insensitive matching.
static constexpr auto OI_RelationsRelation =
    llvm::clv2::makeEnumOption<RelationKind>(
        "relation", "Relation kind for the predicate.", RelationKindVals,
        Init{RelationKind::BaseOf}, llvm::clv2::CaseInsensitiveValues);

class Relations : public Command {
  std::string ID;
  RelationKind Relation = RelationKind::BaseOf;
  unsigned IDCount = 0;
  unsigned RelationCount = 0;

  void addOptions(llvm::clv2::OptionParser &P) override {
    using namespace llvm::clv2;
    P.addDynamicEntry(llvm::clv2::makeEntry<&OI_RelationsId>(ID, IDCount));
    P.addDynamicEntry(
        llvm::clv2::makeEntry<&OI_RelationsRelation>(Relation, RelationCount));
  }

private:
  void run() override {
    if (IDCount == 0 || RelationCount == 0) {
      llvm::errs()
          << "Missing required argument: please provide id and -relation.\n";
      return;
    }
    RelationsRequest Req;
    auto SID = SymbolID::fromStr(ID);
    if (!SID) {
      llvm::errs() << llvm::toString(SID.takeError()) << "\n";
      return;
    }
    Req.Subjects.insert(*SID);
    Req.Predicate = Relation;
    Index->relations(Req, [](const SymbolID &SID, const Symbol &S) {
      llvm::outs() << toYAML(S);
    });
  }
};

static constexpr OptionInfo<std::string> OI_ExportOutputFile{
    "output-file", "Output file for export", Positional{},
    llvm::clv2::Required};
static constexpr llvm::clv2::EnumVal<IndexFileFormat> IndexFormatVals[] = {
    {"yaml", IndexFileFormat::YAML, "human-readable YAML format"},
    {"binary", IndexFileFormat::RIFF, "binary RIFF format"},
};
static constexpr auto OI_ExportFormat =
    llvm::clv2::makeEnumOption<IndexFileFormat>(
        "format", "Format of index export", IndexFormatVals,
        Init{IndexFileFormat::YAML}, llvm::clv2::CaseInsensitiveValues);

class Export : public Command {
  IndexFileFormat Format = IndexFileFormat::YAML;
  std::string OutputFile;
  unsigned FormatCount = 0;
  unsigned OutputFileCount = 0;

  void addOptions(llvm::clv2::OptionParser &P) override {
    using namespace llvm::clv2;
    P.addDynamicEntry(
        llvm::clv2::makeEntry<&OI_ExportFormat>(Format, FormatCount));
    P.addDynamicEntry(llvm::clv2::makeEntry<&OI_ExportOutputFile>(
        OutputFile, OutputFileCount));
  }
  void run() override {
    using namespace clang::clangd;
    // Read input file (as specified in global option)
    auto Buffer = llvm::MemoryBuffer::getFile(IndexLocation);
    if (!Buffer) {
      llvm::errs() << llvm::formatv("Can't open {0}", IndexLocation) << "\n";
      return;
    }

    // Auto-detects input format when parsing
    auto IndexIn = clang::clangd::readIndexFile(Buffer->get()->getBuffer(),
                                                SymbolOrigin::Static);
    if (!IndexIn) {
      llvm::errs() << llvm::toString(IndexIn.takeError()) << "\n";
      return;
    }

    // Prepare output file
    std::error_code EC;
    llvm::raw_fd_ostream OutputStream(OutputFile, EC);
    if (EC) {
      llvm::errs() << llvm::formatv("Can't open {0} for writing", OutputFile)
                   << "\n";
      return;
    }

    // Export
    clang::clangd::IndexFileOut IndexOut(IndexIn.get());
    IndexOut.Format = Format;
    OutputStream << IndexOut;
  }
};

struct {
  const char *Name;
  const char *Description;
  std::function<std::unique_ptr<Command>()> Implementation;
} CommandInfo[] = {
    {"find", "Search for symbols with fuzzyFind", std::make_unique<FuzzyFind>},
    {"lookup", "Dump symbol details by ID or qualified name",
     std::make_unique<Lookup>},
    {"refs", "Find references by ID or qualified name", std::make_unique<Refs>},
    {"relations", "Find relations by ID and relation kind",
     std::make_unique<Relations>},
    {"export", "Export index", std::make_unique<Export>},
};

std::unique_ptr<SymbolIndex> openIndex(llvm::StringRef Index) {
  return Index.starts_with("remote:")
             ? remote::getClient(Index.drop_front(strlen("remote:")),
                                 ProjectRoot)
             : loadIndex(Index, SymbolOrigin::Static, /*UseDex=*/true,
                         /*SupportContainedRefs=*/true);
}

bool runCommand(std::string Request, const SymbolIndex &Index) {
  // Split on spaces and add required null-termination.
  llvm::replace(Request, ' ', '\0');
  llvm::SmallVector<llvm::StringRef> Args;
  llvm::StringRef(Request).split(Args, '\0', /*MaxSplit=*/-1,
                                 /*KeepEmpty=*/false);
  if (Args.empty())
    return false;
  if (Args.front() == "help") {
    llvm::outs() << "dexp - Index explorer\nCommands:\n";
    for (const auto &C : CommandInfo)
      llvm::outs() << llvm::formatv("{0,16} - {1}\n", C.Name, C.Description);
    llvm::outs() << "Get detailed command help with e.g. `find -help`.\n";
    return true;
  }
  llvm::SmallVector<const char *> FakeArgv;
  for (llvm::StringRef S : Args)
    FakeArgv.push_back(S.data()); // Terminated by separator or end of string.

  for (const auto &Cmd : CommandInfo) {
    if (Cmd.Name == Args.front())
      return Cmd.Implementation()->parseAndRun(FakeArgv, Cmd.Description,
                                               Index);
  }
  llvm::errs() << "Unknown command. Try 'help'.\n";
  return false;
}

} // namespace
} // namespace clangd
} // namespace clang

int main(int argc, const char *argv[]) {
  using namespace clang::clangd;

  llvm::clv2::OptionParser P;
  P.add<&DexpGlobalReg>();
  llvm::RegisterAllLLVMOptions(P);
  auto OptsCtx = P.parse(argc, argv, Overview);
  auto *GlobalOpts = OptsCtx->getViewPtr<&DexpGlobalReg>();
  IndexLocation = GlobalOpts->get<&dexpIndexLocationOpt>();
  ExecCommand = GlobalOpts->get<&dexpExecCommandOpt>();
  ProjectRoot = GlobalOpts->get<&dexpProjectRootOpt>();
  llvm::sys::PrintStackTraceOnErrorSignal(argv[0]);

  bool RemoteMode = llvm::StringRef(IndexLocation).starts_with("remote:");
  if (RemoteMode && ProjectRoot.empty()) {
    llvm::errs() << "--project-root is required in remote mode\n";
    return -1;
  }

  std::unique_ptr<SymbolIndex> Index;
  reportTime(RemoteMode ? "Remote index client creation" : "Dex build",
             [&]() { Index = openIndex(IndexLocation); });

  if (!Index) {
    llvm::errs() << "Failed to open the index.\n";
    return -1;
  }

  if (!ExecCommand.empty())
    return runCommand(ExecCommand, *Index) ? 0 : 1;

  llvm::LineEditor LE("dexp");
  while (std::optional<std::string> Request = LE.readLine())
    runCommand(std::move(*Request), *Index);
}
