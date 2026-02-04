#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclVisitor.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Regex.h"

#include <memory>
#include <optional>
#include <system_error>
#include <fstream>

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

static llvm::cl::OptionCategory ListGlobalsCategory("list-globals options");

// Option to ignore const-qualified variables.
static llvm::cl::opt<bool>
    IgnoreConst("ignore-const",
                llvm::cl::desc("Ignore const-qualified variables"),
                llvm::cl::init(false), llvm::cl::cat(ListGlobalsCategory));

// Option to ignore variables with thread-local storage specifier.
static llvm::cl::opt<bool> IgnoreThreadLocal(
    "ignore-thread-local",
    llvm::cl::desc("Ignore variables with thread-local storage specifier"),
    llvm::cl::init(false), llvm::cl::cat(ListGlobalsCategory));

// Option to exclude sub-paths from the expanded paths list.
static llvm::cl::list<std::string> ExcludeSubpaths(
    "exclude-subpath",
    llvm::cl::desc("Exclude files whose path starts with this prefix"),
    llvm::cl::value_desc("path"), llvm::cl::ZeroOrMore,
    llvm::cl::cat(ListGlobalsCategory));

// Option to write output to a file (required).
static llvm::cl::opt<std::string>
    OutputFilename("o", llvm::cl::desc("Write output to <file>"),
                   llvm::cl::value_desc("file"), llvm::cl::Required,
                   llvm::cl::cat(ListGlobalsCategory));

// Optional filter file with regex patterns; matching lines are excluded.
static llvm::cl::opt<std::string>
    FilterFilename("filter-file",
                   llvm::cl::desc("File with regex patterns for lines to "
                                  "exclude (one pattern per line)"),
                   llvm::cl::value_desc("file"),
                   llvm::cl::init(""),
                   llvm::cl::cat(ListGlobalsCategory));

namespace {

static std::string getTSCSpecPrefix(ThreadStorageClassSpecifier TSC) {
  switch (TSC) {
  case TSCS_unspecified:
    return "";
  case TSCS___thread:
    return "__thread ";
  case TSCS__Thread_local:
    return "_Thread_local ";
  case TSCS_thread_local:
    return "thread_local ";
  }
  return "";
}

static std::optional<std::string> getRoleForVarDecl(const VarDecl *VD) {
  const DeclContext *DC = VD->getDeclContext();

  const bool IsConst = VD->getType().isConstQualified();
  if (IgnoreConst && IsConst)
    return std::nullopt;

  const bool IsThreadLocal = VD->getTSCSpec() != TSCS_unspecified;
  if (IgnoreThreadLocal && IsThreadLocal)
    return std::nullopt;

  std::string Prefix =
      (IsConst ? "const " : "") + getTSCSpecPrefix(VD->getTSCSpec());

  const bool InFileScope =
      isa<TranslationUnitDecl>(DC) || isa<NamespaceDecl>(DC);
  const bool InFunctionScope =
      isa<FunctionDecl>(DC) || isa<CXXMethodDecl>(DC) ||
      isa<CXXConstructorDecl>(DC) || isa<CXXDestructorDecl>(DC);

  if (VD->isStaticDataMember())
    return Prefix + "static data member";

  if (VD->getStorageClass() == SC_Static) {
    std::string Role = Prefix;
    if (InFileScope)
      Role += "file-scope ";
    else if (InFunctionScope)
      Role += "function-scope ";
    return Role + "static";
  }

  if (InFileScope)
    return Prefix + "global";

  return std::nullopt;
}

// Collect all C/C++ source/header files under a path (file or directory).
static bool hasSourceExtension(llvm::StringRef Path) {
  llvm::StringRef Ext = llvm::sys::path::extension(Path);
  return Ext.equals_insensitive(".c") || Ext.equals_insensitive(".cc") ||
         Ext.equals_insensitive(".cpp") || Ext.equals_insensitive(".cxx") ||
         Ext.equals_insensitive(".h") || Ext.equals_insensitive(".hh") ||
         Ext.equals_insensitive(".hpp") || Ext.equals_insensitive(".hxx");
}

static bool isExcluded(llvm::StringRef Path) {
  return std::find_if(ExcludeSubpaths.begin(), ExcludeSubpaths.end(),
                      [&Path](const std::string &Excl) {
                        return llvm::StringRef(Path).starts_with(Excl);
                      }) != ExcludeSubpaths.end();
}

static void collectSourceFiles(const std::string &InputPath,
                               std::vector<std::string> &OutFiles) {
  namespace fs = llvm::sys::fs;

  fs::file_status Status;
  if (fs::status(InputPath, Status)) {
    llvm::errs() << "error: cannot access " << InputPath << '\n';
    return;
  }

  if (fs::is_regular_file(Status) && !isExcluded(InputPath)) {
    OutFiles.push_back(InputPath);
    return;
  }

  if (!fs::is_directory(Status))
    return;

  std::error_code EC;
  for (fs::recursive_directory_iterator It(InputPath, EC), End;
       It != End && !EC; It.increment(EC)) {
    if (EC)
      break;
    auto Path = It->path();
    if (!fs::is_regular_file(Path) || !hasSourceExtension(Path) ||
        isExcluded(Path))
      continue;
    OutFiles.emplace_back(Path);
  }

  if (EC)
    llvm::errs() << "error while traversing " << InputPath << ": "
                 << EC.message() << '\n';
}

static std::vector<llvm::Regex>
loadExcludePatternsFromFile(const std::string &Path) {
  std::vector<llvm::Regex> Patterns;
  if (Path.empty())
    return Patterns;

  std::ifstream In(Path);
  if (!In.is_open()) {
    llvm::errs() << "error: cannot open filter file '" << Path << "'\n";
    return Patterns;
  }

  std::string Line;
  unsigned LineNo = 0;
  while (std::getline(In, Line)) {
    ++LineNo;
    llvm::StringRef L(Line);
    L = L.trim();
    // Ignore empty lines and lines whose first non-whitespace character is '#'.
    if (L.empty() || L.starts_with("#"))
      continue;

    llvm::Regex R(L);
    std::string Err;
    if (!R.isValid(Err)) {
      llvm::errs() << "warning: invalid regex in filter file '" << Path
                   << "' line " << LineNo << ": " << Err << "\n";
      continue;
    }
    Patterns.emplace_back(std::move(R));
  }

  return Patterns;
}

class VarDeclCallback : public MatchFinder::MatchCallback {
public:
  VarDeclCallback(llvm::raw_ostream &OS,
                  const std::vector<llvm::Regex> &ExcludePatterns)
      : OS(OS), ExcludePatterns(ExcludePatterns) {}

  void run(const MatchFinder::MatchResult &Result) override {
    const auto *VD = Result.Nodes.getNodeAs<VarDecl>("var");
    if (!VD || VD->isImplicit() || !VD->getName().size())
      return;

    // Exclude parameters of any function or function-type declaration.
    if (isa<ParmVarDecl>(VD))
      return;

    const ASTContext *Ctx = Result.Context;
    const SourceManager &SM = Ctx->getSourceManager();
    SourceLocation Loc = VD->getLocation();
    if (!Loc.isValid())
      return;

    PresumedLoc PLoc = SM.getPresumedLoc(Loc);
    if (!PLoc.isValid() || !PLoc.getFilename())
      return;

    std::optional<std::string> RoleOpt = getRoleForVarDecl(VD);
    if (!RoleOpt)
      return;

    // Get a printable representation of the variable's type.
    std::string TypeStr = VD->getType().getAsString(Ctx->getPrintingPolicy());

    // Build the output line into a string so we can apply regex filters.
    std::string Line;
    llvm::raw_string_ostream SS(Line);
    SS << PLoc.getFilename() << ':' << PLoc.getLine() << ':'
       << PLoc.getColumn() << ": [" << *RoleOpt << "] "
       << TypeStr << ' ' << VD->getNameAsString() << '\n';
    SS.flush();

    if (std::any_of(ExcludePatterns.begin(), ExcludePatterns.end(),
                    [&Line](const llvm::Regex &R) { return R.match(Line); }))
      return;

    OS << Line;
  }

private:
  llvm::raw_ostream &OS;
  const std::vector<llvm::Regex> &ExcludePatterns;
};

} // namespace

int main(int argc, const char **argv) {
  llvm::cl::HideUnrelatedOptions(ListGlobalsCategory);

  auto ExpectedParser =
      CommonOptionsParser::create(argc, argv, ListGlobalsCategory);
  if (!ExpectedParser) {
    llvm::errs() << ExpectedParser.takeError();
    return 1;
  }

  CommonOptionsParser &OptionsParser = ExpectedParser.get();

  // Expand each argument: if it's a file, use it directly; if it's a directory,
  // recursively collect all source files under it.
  std::vector<std::string> ExpandedPaths;
  for (const auto &Path : OptionsParser.getSourcePathList())
    collectSourceFiles(Path, ExpandedPaths);

  // Optionally exclude sub-paths.
  std::vector<std::string> FilteredPaths;
  FilteredPaths.reserve(ExpandedPaths.size());
  for (const auto &Path : ExpandedPaths) {
    auto It = std::find_if(ExcludeSubpaths.begin(), ExcludeSubpaths.end(),
                           [&Path](const std::string &Excl) {
                             return llvm::StringRef(Path).starts_with(Excl);
                           });
    if (It == ExcludeSubpaths.end())
      FilteredPaths.push_back(Path);
  }

  if (FilteredPaths.empty()) {
    llvm::errs() << "error: no input files found\n";
    return 1;
  }

  ClangTool Tool(OptionsParser.getCompilations(), FilteredPaths);

  // Decide where to write output: always to the required -o file.
  std::error_code EC;
  llvm::raw_fd_ostream FileOS(OutputFilename, EC, llvm::sys::fs::OF_Text);
  if (EC) {
    llvm::errs() << "error: cannot open output file '" << OutputFilename
                 << "': " << EC.message() << '\n';
    return 1;
  }
  llvm::raw_ostream &OS = FileOS;

  // Load exclude regex patterns, if any.
  std::vector<llvm::Regex> ExcludePatterns =
      loadExcludePatternsFromFile(FilterFilename);

  VarDeclCallback Callback(OS, ExcludePatterns);
  MatchFinder Finder;

  // Match VarDecls from the main file or from `.inc` files that are not
  // inside a function type alias / typedef; the callback will further
  // filter to globals/statics and will drop parameters.
  Finder.addMatcher(
      varDecl(anyOf(isExpansionInMainFile(),
                    isExpansionInFileMatching(".*\\.inc")),
              unless(hasAncestor(typeAliasDecl())),
              unless(hasAncestor(typedefDecl())))
          .bind("var"),
      &Callback);

  return Tool.run(newFrontendActionFactory(&Finder).get());
}
