//===--- ClangdMain.cpp - clangd server loop ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ClangdMain.h"
#include "ClangdLSPServer.h"
#include "CodeComplete.h"
#include "Compiler.h"
#include "Config.h"
#include "ConfigProvider.h"
#include "Feature.h"
#include "FeatureModule.h"
#include "IncludeCleaner.h"
#include "PathMapping.h"
#include "Protocol.h"
#include "TidyProvider.h"
#include "Transport.h"
#include "index/Background.h"
#include "index/Index.h"
#include "index/MemIndex.h"
#include "index/Merge.h"
#include "index/ProjectAware.h"
#include "index/remote/Client.h"
#include "support/Path.h"
#include "support/Shutdown.h"
#include "support/ThreadCrashReporter.h"
#include "support/ThreadsafeFS.h"
#include "support/Trace.h"
#include "clang/Basic/Stack.h"
#include "clang/Format/Format.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include <chrono>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

#ifdef __GLIBC__
#include <malloc.h>
#endif

namespace clang {
namespace clangd {

// Implemented in Check.cpp.
bool check(const llvm::StringRef File, const ThreadsafeFS &TFS,
           const ClangdLSPServer::Options &Opts);
// Defined in Check.cpp, which owns the --check-* options.
void registerCheckOptions(llvm::clv2::OptionParser &P);
void applyCheckOptions(const llvm::clv2::OptionsContext &Ctx);

namespace {

// All flags must be placed in a category, or they will be shown neither in
// --help, nor --help-hidden!
static constexpr llvm::clv2::OptionCategory Clv2CompileCommands{
    "clangd compilation flags options"};
static constexpr llvm::clv2::OptionCategory Clv2Features{
    "clangd feature options"};
static constexpr llvm::clv2::OptionCategory Clv2Misc{
    "clangd miscellaneous options"};
static constexpr llvm::clv2::OptionCategory Clv2Protocol{
    "clangd protocol and logging options"};
static constexpr llvm::clv2::OptionCategory Clv2Retired{
    "clangd flags no longer in use"};
const llvm::clv2::OptionCategory *ClangdCategories[] = {
    &Clv2Features, &Clv2Protocol, &Clv2CompileCommands, &Clv2Misc,
    &Clv2Retired};

enum CompileArgsFrom { LSPCompileArgs, FilesystemCompileArgs };
static CompileArgsFrom CompileArgsFromVal = FilesystemCompileArgs;
static std::string CompileCommandsDir;
static std::string ResourceDir;
static std::vector<std::string> QueryDriverGlobs;
static bool AllScopesCompletion = true;
static bool AllScopesCompletionSet = false;
static bool ShowOrigins = CodeCompleteOptions().ShowOrigins;
static bool EnableBackgroundIndex = true;
static bool EnableBackgroundIndexSet = false;
static llvm::ThreadPriority BackgroundIndexPriority = llvm::ThreadPriority::Low;
static bool EnableClangTidy = true;
static CodeCompleteOptions::CodeCompletionParse CodeCompletionParse =
    CodeCompleteOptions().RunParser;
static CodeCompleteOptions::CodeCompletionRankingModel RankingModel =
    CodeCompleteOptions().RankingModel;
enum CompletionStyleFlag { Detailed, Bundled };
static CompletionStyleFlag CompletionStyle = Detailed;
static bool CompletionStyleSet = false;
static std::string FallbackStyle = clang::format::DefaultFallbackStyle;
static bool FallbackStyleSet = false;
static std::string EnableFunctionArgSnippets = "-1";
static Config::HeaderInsertionPolicy HeaderInsertion =
    CodeCompleteOptions().InsertIncludes;
static bool HeaderInsertionSet = false;
static bool ImportInsertions = CodeCompleteOptions().ImportInsertions;
static bool HeaderInsertionDecorators = true;
static bool HiddenFeatures = false;
static bool IncludeIneligibleResults =
    CodeCompleteOptions().IncludeIneligibleResults;
static int LimitResults = 100;
static int ReferencesLimit = 1000;
static int RenameFileLimit = 50;
static std::vector<std::string> TweakList;
static bool TweakListSet = false;
static unsigned WorkerThreadsCount = getDefaultAsyncThreadsCount();
static bool WorkerThreadsCountSet = false;
static std::string IndexFile;
static bool Test = false;
static bool CrashPragmas = false;
static bool CrashPragmasSet = false;
static std::string CheckFile;
static bool CheckFileSet = false;
enum PCHStorageFlag { Disk, Memory };
static PCHStorageFlag PCHStorage = PCHStorageFlag::Disk;
static bool Sync = false;
static bool SyncSet = false;
static JSONStreamStyle InputStyle = JSONStreamStyle::Standard;
static bool EnableTestScheme = false;
static std::string PathMappingsArg;
static std::string InputMirrorFile;
static Logger::Level LogLevel = Logger::Info;
static OffsetEncoding ForceOffsetEncoding = OffsetEncoding::UnsupportedEncoding;
static bool PrettyPrint = false;
static bool EnableConfig = true;
static bool EnableConfigSet = false;
static bool StrongWorkspaceMode = false;
static bool UseDirtyHeaders = ClangdServer::Options().UseDirtyHeaders;
static bool PreambleParseForwardingFunctions =
    ParseOptions().PreambleParseForwardingFunctions;
static bool SkipPreambleBuild = ParseOptions().SkipPreambleBuild;
#if defined(__GLIBC__) && CLANGD_MALLOC_TRIM
static bool EnableMallocTrim = true;
#endif
#if CLANGD_ENABLE_REMOTE
static std::string RemoteIndexAddress;
static std::string ProjectRoot;
#endif
static bool ExperimentalModulesSupport = false;

//===----------------------------------------------------------------------===//
// clv2 option descriptors
//
// The variables above hold the parsed values and their initialisers are the
// defaults.  Several of those defaults are runtime values (getDefault-
// AsyncThreadsCount(), CodeCompleteOptions(), ParseOptions(), ...) which
// cannot appear in a constexpr descriptor, so applyClangdOptions() below only
// writes a variable when the flag was actually specified.  That keeps one
// source of truth for every default.
//===----------------------------------------------------------------------===//

namespace clv2 = llvm::clv2;

// -- Compilation flags -------------------------------------------------------

inline constexpr clv2::EnumVal<CompileArgsFrom> CompileArgsFromVals[] = {
    {"lsp", LSPCompileArgs,
     "All compile commands come from LSP and 'compile_commands.json' files "
     "are ignored"},
    {"filesystem", FilesystemCompileArgs,
     "All compile commands come from the 'compile_commands.json' files"},
};
inline constexpr auto CompileArgsFromOpt =
    clv2::makeEnumOption<CompileArgsFrom>(
        "compile_args_from", "The source of compile commands",
        CompileArgsFromVals, clv2::Init{FilesystemCompileArgs}, clv2::Hidden,
        clv2::cat(Clv2CompileCommands));

inline constexpr clv2::OptionInfo<std::string> CompileCommandsDirOpt{
    "compile-commands-dir",
    "Specify a path to look for compile_commands.json. If path "
    "is invalid, clangd will look in the current directory and "
    "parent paths of each source file",
    clv2::cat(Clv2CompileCommands)};

inline constexpr clv2::OptionInfo<std::string> ResourceDirOpt{
    "resource-dir", "Directory for system clang headers", clv2::Init{""},
    clv2::Hidden, clv2::cat(Clv2CompileCommands)};

inline constexpr clv2::ListOptionInfo<std::string> QueryDriverGlobsOpt{
    "query-driver",
    "Comma separated list of globs for white-listing gcc-compatible "
    "drivers that are safe to execute. Drivers matching any of these globs "
    "will be used to extract system includes. e.g. "
    "/usr/bin/**/clang-*,/path/to/repo/**/g++-*",
    clv2::CommaSeparated, clv2::cat(Clv2CompileCommands)};

// -- Features ----------------------------------------------------------------

inline constexpr clv2::OptionInfo<bool> AllScopesCompletionOpt{
    "all-scopes-completion",
    "If set to true, code completion will include index symbols that are "
    "not defined in the scopes (e.g. "
    "namespaces) visible from the code completion point. Such completions "
    "can insert scope qualifiers",
    clv2::Init{true}, clv2::cat(Clv2Features)};

inline constexpr clv2::OptionInfo<bool> ShowOriginsOpt{
    "debug-origin", "Show origins of completion items", clv2::Hidden,
    clv2::cat(Clv2Features)};

inline constexpr clv2::OptionInfo<bool> EnableBackgroundIndexOpt{
    "background-index",
    "Index project code in the background and persist index on disk.",
    clv2::Init{true}, clv2::cat(Clv2Features)};

inline constexpr clv2::EnumVal<llvm::ThreadPriority>
    BackgroundIndexPriorityVals[] = {
        {"background", llvm::ThreadPriority::Background,
         "Minimum priority, runs on idle CPUs. "
         "May leave 'performance' cores unused."},
        {"low", llvm::ThreadPriority::Low,
         "Reduced priority compared to interactive work."},
        {"normal", llvm::ThreadPriority::Default,
         "Same priority as other clangd work."},
};
inline constexpr auto BackgroundIndexPriorityOpt =
    clv2::makeEnumOption<llvm::ThreadPriority>(
        "background-index-priority",
        "Thread priority for building the background index. "
        "The effect of this flag is OS-specific.",
        BackgroundIndexPriorityVals, clv2::Init{llvm::ThreadPriority::Low},
        clv2::cat(Clv2Features));

inline constexpr clv2::OptionInfo<bool> EnableClangTidyOpt{
    "clang-tidy", "Enable clang-tidy diagnostics", clv2::Init{true},
    clv2::cat(Clv2Features)};

inline constexpr clv2::EnumVal<CodeCompleteOptions::CodeCompletionParse>
    CodeCompletionParseVals[] = {
        {"always", CodeCompleteOptions::AlwaysParse,
         "Block until the parser can be used"},
        {"auto", CodeCompleteOptions::ParseIfReady,
         "Use text-based completion if the parser is not ready"},
        {"never", CodeCompleteOptions::NeverParse,
         "Always used text-based completion"},
};
inline constexpr auto CodeCompletionParseOpt =
    clv2::makeEnumOption<CodeCompleteOptions::CodeCompletionParse>(
        "completion-parse",
        "Whether the clang-parser is used for code-completion",
        CodeCompletionParseVals, clv2::Hidden, clv2::cat(Clv2Features));

inline constexpr clv2::EnumVal<CodeCompleteOptions::CodeCompletionRankingModel>
    RankingModelVals[] = {
        {"heuristics", CodeCompleteOptions::Heuristics,
         "Use heuristics to rank code completion items"},
        {"decision_forest", CodeCompleteOptions::DecisionForest,
         "Use Decision Forest model to rank completion items"},
};
inline constexpr auto RankingModelOpt =
    clv2::makeEnumOption<CodeCompleteOptions::CodeCompletionRankingModel>(
        "ranking-model", "Model to use to rank code-completion items",
        RankingModelVals, clv2::Hidden, clv2::cat(Clv2Features));

inline constexpr clv2::EnumVal<CompletionStyleFlag> CompletionStyleVals[] = {
    {"detailed", Detailed,
     "One completion item for each semantically distinct "
     "completion, with full type information"},
    {"bundled", Bundled,
     "Similar completion items (e.g. function overloads) are "
     "combined. Type information shown where possible"},
};
inline constexpr auto CompletionStyleOpt =
    clv2::makeEnumOption<CompletionStyleFlag>(
        "completion-style", "Granularity of code completion suggestions",
        CompletionStyleVals, clv2::cat(Clv2Features));

inline constexpr clv2::OptionInfo<std::string> FallbackStyleOpt{
    "fallback-style",
    "clang-format style to apply by default when "
    "no .clang-format file is found",
    clv2::cat(Clv2Features)};

inline constexpr clv2::OptionInfo<std::string> EnableFunctionArgSnippetsOpt{
    "function-arg-placeholders",
    "When disabled (0), completions contain only parentheses for "
    "function calls. When enabled (1), completions also contain "
    "placeholders for method parameters",
    clv2::Init{"-1"}, clv2::cat(Clv2Features)};

inline constexpr clv2::EnumVal<Config::HeaderInsertionPolicy>
    HeaderInsertionVals[] = {
        {"iwyu", Config::HeaderInsertionPolicy::IWYU,
         "Include what you use. "
         "Insert the owning header for top-level symbols, unless the "
         "header is already directly included or the symbol is "
         "forward-declared"},
        {"never", Config::HeaderInsertionPolicy::NeverInsert,
         "Never insert #include directives as part of code completion"},
};
inline constexpr auto HeaderInsertionOpt =
    clv2::makeEnumOption<Config::HeaderInsertionPolicy>(
        "header-insertion",
        "Add #include directives when accepting code completions",
        HeaderInsertionVals, clv2::cat(Clv2Features));

inline constexpr clv2::OptionInfo<bool> ImportInsertionsOpt{
    "import-insertions",
    "If header insertion is enabled, add #import directives when "
    "accepting code completions or fixing includes in Objective-C code",
    clv2::cat(Clv2Features)};

inline constexpr clv2::OptionInfo<bool> HeaderInsertionDecoratorsOpt{
    "header-insertion-decorators",
    "Prepend a circular dot or space before the completion "
    "label, depending on whether "
    "an include line will be inserted or not",
    clv2::Init{true}, clv2::cat(Clv2Features)};

inline constexpr clv2::OptionInfo<bool> HiddenFeaturesOpt{
    "hidden-features",
    "Enable hidden features mostly useful to clangd developers", clv2::Hidden,
    clv2::cat(Clv2Features)};

inline constexpr clv2::OptionInfo<bool> IncludeIneligibleResultsOpt{
    "include-ineligible-results",
    "Include ineligible completion results (e.g. private members)",
    clv2::Hidden, clv2::cat(Clv2Features)};

inline constexpr clv2::OptionInfo<int> LimitResultsOpt{
    "limit-results",
    "Limit the number of results returned by clangd. "
    "0 means no limit (default=100)",
    clv2::Init{100}, clv2::cat(Clv2Features)};

inline constexpr clv2::OptionInfo<int> ReferencesLimitOpt{
    "limit-references",
    "Limit the number of references returned by clangd. "
    "0 means no limit (default=1000)",
    clv2::Init{1000}, clv2::cat(Clv2Features)};

inline constexpr clv2::OptionInfo<int> RenameFileLimitOpt{
    "rename-file-limit",
    "Limit the number of files to be affected by symbol renaming. "
    "0 means no limit (default=50)",
    clv2::Init{50}, clv2::cat(Clv2Features)};

inline constexpr clv2::ListOptionInfo<std::string> TweakListOpt{
    "tweaks",
    "Specify a list of Tweaks to enable (only for clangd developers).",
    clv2::Hidden, clv2::CommaSeparated, clv2::cat(Clv2Features)};

inline constexpr clv2::OptionInfo<bool> StrongWorkspaceModeOpt{
    "strong-workspace-mode",
    "An alternate mode of operation for clangd, where the clangd instance "
    "is used to edit a single workspace.\n"
    "When enabled, fallback commands use the workspace directory as their "
    "working directory instead of the parent folder.",
    clv2::Hidden, clv2::cat(Clv2Features)};

#if CLANGD_ENABLE_REMOTE
inline constexpr clv2::OptionInfo<std::string> RemoteIndexAddressOpt{
    "remote-index-address", "Address of the remote index server",
    clv2::cat(Clv2Features)};

inline constexpr clv2::OptionInfo<std::string> ProjectRootOpt{
    "project-root",
    "Path to the project root. Requires remote-index-address to be set.",
    clv2::cat(Clv2Features)};
#endif

inline constexpr clv2::OptionInfo<bool> ExperimentalModulesSupportOpt{
    "experimental-modules-support",
    "Experimental support for standard c++ modules", clv2::cat(Clv2Features)};

// -- Miscellaneous -----------------------------------------------------------

inline constexpr clv2::OptionInfo<unsigned> WorkerThreadsCountOpt{
    "j",
    "Number of async workers used by clangd. Background index also "
    "uses this many workers.",
    clv2::cat(Clv2Misc)};

inline constexpr clv2::OptionInfo<std::string> IndexFileOpt{
    "index-file",
    "Index file to build the static index. The file must have been created "
    "by a compatible clangd-indexer\n"
    "WARNING: This option is experimental only, and will be removed "
    "eventually. Don't rely on it",
    clv2::Init{""}, clv2::Hidden, clv2::cat(Clv2Misc)};

inline constexpr clv2::OptionInfo<bool> TestOpt{
    "lit-test",
    "Abbreviation for -input-style=delimited -pretty -sync "
    "-enable-test-scheme -enable-config=0 -log=verbose -crash-pragmas. "
    "Also sets config options: Index.StandardLibrary=false. "
    "Intended to simplify lit tests",
    clv2::Hidden, clv2::cat(Clv2Misc)};

inline constexpr clv2::OptionInfo<bool> CrashPragmasOpt{
    "crash-pragmas", "Respect `#pragma clang __debug crash` and friends.",
    clv2::Hidden, clv2::cat(Clv2Misc)};

inline constexpr clv2::OptionInfo<std::string> CheckFileOpt{
    "check",
    "Parse one file in isolation instead of acting as a language server. "
    "Useful to investigate/reproduce crashes or configuration problems. "
    "With --check=<filename>, attempts to parse a particular file.",
    clv2::Init{""}, clv2::ValueOptional, clv2::cat(Clv2Misc)};

inline constexpr clv2::EnumVal<PCHStorageFlag> PCHStorageVals[] = {
    {"disk", PCHStorageFlag::Disk, "store PCHs on disk"},
    {"memory", PCHStorageFlag::Memory, "store PCHs in memory"},
};
inline constexpr auto PCHStorageOpt = clv2::makeEnumOption<PCHStorageFlag>(
    "pch-storage",
    "Storing PCHs in memory increases memory usages, but may "
    "improve performance",
    PCHStorageVals, clv2::Init{PCHStorageFlag::Disk}, clv2::cat(Clv2Misc));

inline constexpr clv2::OptionInfo<bool> SyncOpt{
    "sync",
    "Handle client requests on main thread. Background index still uses "
    "its own thread.",
    clv2::Hidden, clv2::cat(Clv2Misc)};

inline constexpr clv2::OptionInfo<bool> EnableConfigOpt{
    "enable-config",
    "Read user and project configuration from YAML files.\n"
    "Project config is from a .clangd file in the project directory.\n"
    "User config is from clangd/config.yaml in the following directories:\n"
    "\tWindows: %USERPROFILE%\\AppData\\Local\n"
    "\tMac OS: ~/Library/Preferences/\n"
    "\tOthers: $XDG_CONFIG_HOME, usually ~/.config\n"
    "Configuration is documented at https://clangd.llvm.org/config.html",
    clv2::Init{true}, clv2::cat(Clv2Misc)};

inline constexpr clv2::OptionInfo<bool> UseDirtyHeadersOpt{
    "use-dirty-headers",
    "Use files open in the editor when parsing headers instead of reading "
    "from the disk",
    clv2::Hidden, clv2::cat(Clv2Misc)};

inline constexpr clv2::OptionInfo<bool> PreambleParseForwardingFunctionsOpt{
    "parse-forwarding-functions",
    "Parse all emplace-like functions in included headers", clv2::Hidden,
    clv2::cat(Clv2Misc)};

inline constexpr clv2::OptionInfo<bool> SkipPreambleBuildOpt{
    "skip-preamble-build", "If ture, skip preamble build", clv2::Hidden,
    clv2::cat(Clv2Misc)};

#if defined(__GLIBC__) && CLANGD_MALLOC_TRIM
inline constexpr clv2::OptionInfo<bool> EnableMallocTrimOpt{
    "malloc-trim", "Release memory periodically via malloc_trim(3).",
    clv2::Init{true}, clv2::cat(Clv2Misc)};
#endif

// -- Protocol ----------------------------------------------------------------

inline constexpr clv2::EnumVal<JSONStreamStyle> InputStyleVals[] = {
    {"standard", JSONStreamStyle::Standard, "usual LSP protocol"},
    {"delimited", JSONStreamStyle::Delimited,
     "messages delimited by --- lines, with # comment support"},
};
inline constexpr auto InputStyleOpt = clv2::makeEnumOption<JSONStreamStyle>(
    "input-style", "Input JSON stream encoding", InputStyleVals,
    clv2::Init{JSONStreamStyle::Standard}, clv2::Hidden,
    clv2::cat(Clv2Protocol));

inline constexpr clv2::OptionInfo<bool> EnableTestSchemeOpt{
    "enable-test-uri-scheme",
    "Enable 'test:' URI scheme. Only use in lit tests", clv2::Hidden,
    clv2::cat(Clv2Protocol)};

inline constexpr clv2::OptionInfo<std::string> PathMappingsArgOpt{
    "path-mappings",
    "Translates between client paths (as seen by a remote editor) and "
    "server paths (where clangd sees files on disk). "
    "Comma separated list of '<client_path>=<server_path>' pairs, the "
    "first entry matching a given path is used. "
    "e.g. /home/project/incl=/opt/include,/home/project=/workarea/project",
    clv2::Init{""}, clv2::cat(Clv2Protocol)};

inline constexpr clv2::OptionInfo<std::string> InputMirrorFileOpt{
    "input-mirror-file",
    "Mirror all LSP input to the specified file. Useful for debugging",
    clv2::Init{""}, clv2::Hidden, clv2::cat(Clv2Protocol)};

inline constexpr clv2::EnumVal<Logger::Level> LogLevelVals[] = {
    {"error", Logger::Error, "Error messages only"},
    {"info", Logger::Info, "High level execution tracing"},
    {"verbose", Logger::Debug, "Low level details"},
};
inline constexpr auto LogLevelOpt = clv2::makeEnumOption<Logger::Level>(
    "log", "Verbosity of log messages written to stderr", LogLevelVals,
    clv2::Init{Logger::Info}, clv2::cat(Clv2Protocol));

inline constexpr clv2::EnumVal<OffsetEncoding> ForceOffsetEncodingVals[] = {
    {"utf-8", OffsetEncoding::UTF8, "Offsets are in UTF-8 bytes"},
    {"utf-16", OffsetEncoding::UTF16, "Offsets are in UTF-16 code units"},
    {"utf-32", OffsetEncoding::UTF32, "Offsets are in unicode codepoints"},
};
inline constexpr auto ForceOffsetEncodingOpt =
    clv2::makeEnumOption<OffsetEncoding>(
        "offset-encoding",
        "Force the offsetEncoding used for character positions. "
        "This bypasses negotiation via client capabilities",
        ForceOffsetEncodingVals,
        clv2::Init{OffsetEncoding::UnsupportedEncoding},
        clv2::cat(Clv2Protocol));

inline constexpr clv2::OptionInfo<bool> PrettyPrintOpt{
    "pretty", "Pretty-print JSON output", clv2::cat(Clv2Protocol)};

// -- Retired -----------------------------------------------------------------
//
// Kept so that existing command lines keep working; specifying one prints a
// notice and is otherwise ignored.

#define CLANGD_RETIRED_FLAG(Var, Name)                                         \
  inline constexpr clv2::OptionInfo<bool> Var{                                 \
      Name, "Obsolete flag, ignored", clv2::Hidden, clv2::cat(Clv2Retired)};

CLANGD_RETIRED_FLAG(RetiredIndexOpt, "index")
CLANGD_RETIRED_FLAG(RetiredSuggestMissingIncludesOpt,
                    "suggest-missing-includes")
CLANGD_RETIRED_FLAG(RetiredRecoveryASTOpt, "recovery-ast")
CLANGD_RETIRED_FLAG(RetiredRecoveryASTTypeOpt, "recovery-ast-type")
CLANGD_RETIRED_FLAG(RetiredAsyncPreambleOpt, "async-preamble")
CLANGD_RETIRED_FLAG(RetiredCollectMainFileRefsOpt, "collect-main-file-refs")
CLANGD_RETIRED_FLAG(RetiredCrossFileRenameOpt, "cross-file-rename")
CLANGD_RETIRED_FLAG(RetiredInlayHintsOpt, "inlay-hints")
CLANGD_RETIRED_FLAG(RetiredFoldingRangesOpt, "folding-ranges")
CLANGD_RETIRED_FLAG(RetiredIncludeCleanerStdlibOpt, "include-cleaner-stdlib")
#undef CLANGD_RETIRED_FLAG

// This one took a value rather than being a plain flag.
inline constexpr clv2::OptionInfo<std::string> RetiredClangTidyChecksOpt{
    "clang-tidy-checks", "Obsolete flag, ignored", clv2::Hidden,
    clv2::cat(Clv2Retired)};

inline constexpr clv2::OptionsRegistry<
    &CompileArgsFromOpt, &CompileCommandsDirOpt, &ResourceDirOpt,
    &QueryDriverGlobsOpt, &AllScopesCompletionOpt, &ShowOriginsOpt,
    &EnableBackgroundIndexOpt, &BackgroundIndexPriorityOpt, &EnableClangTidyOpt,
    &CodeCompletionParseOpt, &RankingModelOpt, &CompletionStyleOpt,
    &FallbackStyleOpt, &EnableFunctionArgSnippetsOpt, &HeaderInsertionOpt,
    &ImportInsertionsOpt, &HeaderInsertionDecoratorsOpt, &HiddenFeaturesOpt,
    &IncludeIneligibleResultsOpt, &LimitResultsOpt, &ReferencesLimitOpt,
    &RenameFileLimitOpt, &TweakListOpt, &StrongWorkspaceModeOpt,
#if CLANGD_ENABLE_REMOTE
    &RemoteIndexAddressOpt, &ProjectRootOpt,
#endif
    &ExperimentalModulesSupportOpt, &WorkerThreadsCountOpt, &IndexFileOpt,
    &TestOpt, &CrashPragmasOpt, &CheckFileOpt, &PCHStorageOpt, &SyncOpt,
    &EnableConfigOpt, &UseDirtyHeadersOpt, &PreambleParseForwardingFunctionsOpt,
    &SkipPreambleBuildOpt,
#if defined(__GLIBC__) && CLANGD_MALLOC_TRIM
    &EnableMallocTrimOpt,
#endif
    &InputStyleOpt, &EnableTestSchemeOpt, &PathMappingsArgOpt,
    &InputMirrorFileOpt, &LogLevelOpt, &ForceOffsetEncodingOpt, &PrettyPrintOpt,
    &RetiredIndexOpt, &RetiredSuggestMissingIncludesOpt, &RetiredRecoveryASTOpt,
    &RetiredRecoveryASTTypeOpt, &RetiredAsyncPreambleOpt,
    &RetiredCollectMainFileRefsOpt, &RetiredCrossFileRenameOpt,
    &RetiredInlayHintsOpt, &RetiredFoldingRangesOpt,
    &RetiredIncludeCleanerStdlibOpt, &RetiredClangTidyChecksOpt>
    ClangdReg;

/// Copy parsed values into the file-scope variables above.
///
/// Only writes a variable when the flag was specified, so the variable's own
/// initialiser stays the default -- which matters for the several defaults
/// that are runtime values and cannot live in a constexpr descriptor.
static void applyClangdOptions(const llvm::clv2::OptionsContext &Ctx) {
  const auto *O = Ctx.getViewPtr<&ClangdReg>();
  if (!O)
    return;

#define CLANGD_SET(Var, Opt)                                                   \
  if (O->specified<&Opt>())                                                    \
  Var = O->get<&Opt>()
#define CLANGD_SET_F(Var, Opt, Flag)                                           \
  if (O->specified<&Opt>()) {                                                  \
    Var = O->get<&Opt>();                                                      \
    Flag = true;                                                               \
  }

  CLANGD_SET(CompileArgsFromVal, CompileArgsFromOpt);
  CLANGD_SET(CompileCommandsDir, CompileCommandsDirOpt);
  CLANGD_SET(ResourceDir, ResourceDirOpt);
  CLANGD_SET(QueryDriverGlobs, QueryDriverGlobsOpt);
  CLANGD_SET_F(AllScopesCompletion, AllScopesCompletionOpt,
               AllScopesCompletionSet);
  CLANGD_SET(ShowOrigins, ShowOriginsOpt);
  CLANGD_SET_F(EnableBackgroundIndex, EnableBackgroundIndexOpt,
               EnableBackgroundIndexSet);
  CLANGD_SET(BackgroundIndexPriority, BackgroundIndexPriorityOpt);
  CLANGD_SET(EnableClangTidy, EnableClangTidyOpt);
  CLANGD_SET(CodeCompletionParse, CodeCompletionParseOpt);
  CLANGD_SET(RankingModel, RankingModelOpt);
  CLANGD_SET_F(CompletionStyle, CompletionStyleOpt, CompletionStyleSet);
  CLANGD_SET_F(FallbackStyle, FallbackStyleOpt, FallbackStyleSet);
  CLANGD_SET(EnableFunctionArgSnippets, EnableFunctionArgSnippetsOpt);
  CLANGD_SET_F(HeaderInsertion, HeaderInsertionOpt, HeaderInsertionSet);
  CLANGD_SET(ImportInsertions, ImportInsertionsOpt);
  CLANGD_SET(HeaderInsertionDecorators, HeaderInsertionDecoratorsOpt);
  CLANGD_SET(HiddenFeatures, HiddenFeaturesOpt);
  CLANGD_SET(IncludeIneligibleResults, IncludeIneligibleResultsOpt);
  CLANGD_SET(LimitResults, LimitResultsOpt);
  CLANGD_SET(ReferencesLimit, ReferencesLimitOpt);
  CLANGD_SET(RenameFileLimit, RenameFileLimitOpt);
  CLANGD_SET_F(TweakList, TweakListOpt, TweakListSet);
  CLANGD_SET(StrongWorkspaceMode, StrongWorkspaceModeOpt);
#if CLANGD_ENABLE_REMOTE
  CLANGD_SET(RemoteIndexAddress, RemoteIndexAddressOpt);
  CLANGD_SET(ProjectRoot, ProjectRootOpt);
#endif
  CLANGD_SET(ExperimentalModulesSupport, ExperimentalModulesSupportOpt);
  CLANGD_SET_F(WorkerThreadsCount, WorkerThreadsCountOpt,
               WorkerThreadsCountSet);
  CLANGD_SET(IndexFile, IndexFileOpt);
  CLANGD_SET(Test, TestOpt);
  CLANGD_SET_F(CrashPragmas, CrashPragmasOpt, CrashPragmasSet);
  CLANGD_SET_F(CheckFile, CheckFileOpt, CheckFileSet);
  CLANGD_SET(PCHStorage, PCHStorageOpt);
  CLANGD_SET_F(Sync, SyncOpt, SyncSet);
  CLANGD_SET_F(EnableConfig, EnableConfigOpt, EnableConfigSet);
  CLANGD_SET(UseDirtyHeaders, UseDirtyHeadersOpt);
  CLANGD_SET(PreambleParseForwardingFunctions,
             PreambleParseForwardingFunctionsOpt);
  CLANGD_SET(SkipPreambleBuild, SkipPreambleBuildOpt);
#if defined(__GLIBC__) && CLANGD_MALLOC_TRIM
  CLANGD_SET(EnableMallocTrim, EnableMallocTrimOpt);
#endif
  CLANGD_SET(InputStyle, InputStyleOpt);
  CLANGD_SET(EnableTestScheme, EnableTestSchemeOpt);
  CLANGD_SET(PathMappingsArg, PathMappingsArgOpt);
  CLANGD_SET(InputMirrorFile, InputMirrorFileOpt);
  CLANGD_SET(LogLevel, LogLevelOpt);
  CLANGD_SET(ForceOffsetEncoding, ForceOffsetEncodingOpt);
  CLANGD_SET(PrettyPrint, PrettyPrintOpt);
#undef CLANGD_SET
#undef CLANGD_SET_F

  // Retired flags: warn, then ignore.
#define CLANGD_RETIRED_WARN(Opt, Name)                                         \
  if (O->specified<&Opt>())                                                    \
    llvm::errs() << "The flag `-" << Name << "` is obsolete and ignored.\n";
  CLANGD_RETIRED_WARN(RetiredIndexOpt, "index")
  CLANGD_RETIRED_WARN(RetiredSuggestMissingIncludesOpt,
                      "suggest-missing-includes")
  CLANGD_RETIRED_WARN(RetiredRecoveryASTOpt, "recovery-ast")
  CLANGD_RETIRED_WARN(RetiredRecoveryASTTypeOpt, "recovery-ast-type")
  CLANGD_RETIRED_WARN(RetiredAsyncPreambleOpt, "async-preamble")
  CLANGD_RETIRED_WARN(RetiredCollectMainFileRefsOpt, "collect-main-file-refs")
  CLANGD_RETIRED_WARN(RetiredCrossFileRenameOpt, "cross-file-rename")
  CLANGD_RETIRED_WARN(RetiredInlayHintsOpt, "inlay-hints")
  CLANGD_RETIRED_WARN(RetiredFoldingRangesOpt, "folding-ranges")
  CLANGD_RETIRED_WARN(RetiredIncludeCleanerStdlibOpt, "include-cleaner-stdlib")
  CLANGD_RETIRED_WARN(RetiredClangTidyChecksOpt, "clang-tidy-checks")
#undef CLANGD_RETIRED_WARN
}

#if defined(__GLIBC__) && CLANGD_MALLOC_TRIM
std::function<void()> getMemoryCleanupFunction() {
  if (!EnableMallocTrim)
    return nullptr;
  constexpr size_t MallocTrimPad = 20'000'000;
  return []() {
    if (malloc_trim(MallocTrimPad))
      vlog("Released memory via malloc_trim");
  };
}
#else
std::function<void()> getMemoryCleanupFunction() { return nullptr; }
#endif

/// Supports a test URI scheme with relaxed constraints for lit tests.
/// The path in a test URI will be combined with a platform-specific fake
/// directory to form an absolute path. For example, test:///a.cpp is resolved
/// C:\clangd-test\a.cpp on Windows and /clangd-test/a.cpp on Unix.
class TestScheme : public URIScheme {
public:
  llvm::Expected<std::string>
  getAbsolutePath(llvm::StringRef /*Authority*/, llvm::StringRef Body,
                  llvm::StringRef /*HintPath*/) const override {
    using namespace llvm::sys;
    // Still require "/" in body to mimic file scheme, as we want lengths of an
    // equivalent URI in both schemes to be the same.
    if (!Body.starts_with("/"))
      return error(
          "Expect URI body to be an absolute path starting with '/': {0}",
          Body);
    Body = Body.ltrim('/');
    llvm::SmallString<16> Path(Body);
    path::native(Path);
    path::make_absolute(testDir(), Path);
    return std::string(Path);
  }

  llvm::Expected<URI>
  uriFromAbsolutePath(llvm::StringRef AbsolutePath) const override {
    llvm::StringRef Body = AbsolutePath;
    if (!Body.consume_front(testDir()))
      return error("Path {0} doesn't start with root {1}", AbsolutePath,
                   testDir());

    return URI("test", /*Authority=*/"",
               llvm::sys::path::convert_to_slash(Body));
  }

private:
  static llvm::StringRef testDir() {
#ifdef _WIN32
    static const std::string TestDir = llvm::sys::path::native("C:/clangd-test");
    return TestDir;
#else
    return "/clangd-test";
#endif
  }
};

std::unique_ptr<SymbolIndex>
loadExternalIndex(const Config::ExternalIndexSpec &External,
                  AsyncTaskRunner *Tasks, bool SupportContainedRefs) {
  static const trace::Metric RemoteIndexUsed("used_remote_index",
                                             trace::Metric::Value, "address");
  switch (External.Kind) {
  case Config::ExternalIndexSpec::None:
    break;
  case Config::ExternalIndexSpec::Server:
    RemoteIndexUsed.record(1, External.Location);
    log("Associating {0} with remote index at {1}.", External.MountPoint,
        External.Location);
    return remote::getClient(External.Location, External.MountPoint);
  case Config::ExternalIndexSpec::File:
    log("Associating {0} with monolithic index at {1}.", External.MountPoint,
        External.Location);
    auto NewIndex = std::make_unique<SwapIndex>(std::make_unique<MemIndex>());
    auto IndexLoadTask = [File = External.Location,
                          PlaceHolder = NewIndex.get(), SupportContainedRefs] {
      if (auto Idx = loadIndex(File, SymbolOrigin::Static, /*UseDex=*/true,
                               SupportContainedRefs))
        PlaceHolder->reset(std::move(Idx));
    };
    if (Tasks) {
      Tasks->runAsync("Load-index:" + External.Location,
                      std::move(IndexLoadTask));
    } else {
      IndexLoadTask();
    }
    return std::move(NewIndex);
  }
  llvm_unreachable("Invalid ExternalIndexKind.");
}

std::optional<bool> shouldEnableFunctionArgSnippets() {
  std::string Val = EnableFunctionArgSnippets;
  // Accept the same values that a bool option parser would, but also accept
  // -1 to indicate "unspecified", in which case the ArgumentListsPolicy
  // config option will be respected.
  if (Val == "1" || Val == "true" || Val == "True" || Val == "TRUE")
    return true;
  if (Val == "0" || Val == "false" || Val == "False" || Val == "FALSE")
    return false;
  if (Val != "-1")
    elog("Value specified by --function-arg-placeholders is invalid. Provide a "
         "boolean value or leave unspecified to use ArgumentListsPolicy from "
         "config instead.");
  return std::nullopt;
}

class FlagsConfigProvider : public config::Provider {
private:
  config::CompiledFragment Frag;

  std::vector<config::CompiledFragment>
  getFragments(const config::Params &,
               config::DiagnosticCallback) const override {
    return {Frag};
  }

public:
  FlagsConfigProvider() {
    std::optional<Config::CDBSearchSpec> CDBSearch;
    std::optional<Config::ExternalIndexSpec> IndexSpec;
    std::optional<Config::BackgroundPolicy> BGPolicy;
    std::optional<Config::ArgumentListsPolicy> ArgumentLists;

    // If --compile-commands-dir arg was invoked, check value and override
    // default path.
    if (!CompileCommandsDir.empty()) {
      if (llvm::sys::fs::exists(CompileCommandsDir)) {
        // We support passing both relative and absolute paths to the
        // --compile-commands-dir argument, but we assume the path is absolute
        // in the rest of clangd so we make sure the path is absolute before
        // continuing.
        llvm::SmallString<128> Path(CompileCommandsDir);
        if (std::error_code EC = llvm::sys::fs::make_absolute(Path)) {
          elog("Error while converting the relative path specified by "
               "--compile-commands-dir to an absolute path: {0}. The argument "
               "will be ignored.",
               EC.message());
        } else {
          CDBSearch = {Config::CDBSearchSpec::FixedDir, Path.str().str()};
        }
      } else {
        elog("Path specified by --compile-commands-dir does not exist. The "
             "argument will be ignored.");
      }
    }
    if (!IndexFile.empty()) {
      Config::ExternalIndexSpec Spec;
      Spec.Kind = Spec.File;
      Spec.Location = IndexFile;
      IndexSpec = std::move(Spec);
    }
#if CLANGD_ENABLE_REMOTE
    if (!RemoteIndexAddress.empty()) {
      assert(!ProjectRoot.empty() && IndexFile.empty());
      Config::ExternalIndexSpec Spec;
      Spec.Kind = Spec.Server;
      Spec.Location = RemoteIndexAddress;
      Spec.MountPoint = ProjectRoot;
      IndexSpec = std::move(Spec);
      BGPolicy = Config::BackgroundPolicy::Skip;
    }
#endif
    if (!EnableBackgroundIndex) {
      BGPolicy = Config::BackgroundPolicy::Skip;
    }

    if (std::optional<bool> Enable = shouldEnableFunctionArgSnippets()) {
      ArgumentLists = *Enable ? Config::ArgumentListsPolicy::FullPlaceholders
                              : Config::ArgumentListsPolicy::Delimiters;
    }

    Frag = [=](const config::Params &, Config &C) {
      if (CDBSearch)
        C.CompileFlags.CDBSearch = *CDBSearch;
      if (IndexSpec)
        C.Index.External = *IndexSpec;
      if (BGPolicy)
        C.Index.Background = *BGPolicy;
      if (ArgumentLists)
        C.Completion.ArgumentLists = *ArgumentLists;
      if (HeaderInsertionSet)
        C.Completion.HeaderInsertion = HeaderInsertion;
      if (AllScopesCompletionSet)
        C.Completion.AllScopes = AllScopesCompletion;

      if (Test)
        C.Index.StandardLibrary = false;
      return true;
    };
  }
};
} // namespace

enum class ErrorResultCode : int {
  NoShutdownRequest = 1,
  CantRunAsXPCService = 2,
  CheckFailed = 3
};

int clangdMain(int argc, char *argv[]) {
  // Clang could run on the main thread. e.g., when the flag '-check' or '-sync'
  // is enabled.
  clang::noteBottomOfStack();
  llvm::InitLLVM X(argc, argv);
  llvm::InitializeAllTargetInfos();
  llvm::sys::AddSignalHandler(
      [](void *) {
        ThreadCrashReporter::runCrashHandlers();
        // Ensure ThreadCrashReporter and PrintStackTrace output is visible.
        llvm::errs().flush();
      },
      nullptr);
  llvm::sys::SetInterruptFunction(&requestShutdown);
  llvm::cl::SetVersionPrinter([](llvm::raw_ostream &OS) {
    OS << versionString() << "\n"
       << "Features: " << featureString() << "\n"
       << "Platform: " << platformString() << "\n";
  });
  const char *FlagsEnvVar = "CLANGD_FLAGS";
  const char *Overview =
      R"(clangd is a language server that provides IDE-like features to editors.

It should be used via an editor plugin rather than invoked directly. For more information, see:
	https://clangd.llvm.org/
	https://microsoft.github.io/language-server-protocol/

clangd accepts flags on the commandline, and in the CLANGD_FLAGS environment variable.
)";
  // Expand CLANGD_FLAGS before parsing.
  llvm::SmallVector<const char *, 20> ExpandedArgv;
  llvm::BumpPtrAllocator EnvAlloc;
  llvm::StringSaver EnvSaver(EnvAlloc);
  ExpandedArgv.push_back(argv[0]);
  if (auto EnvValue = llvm::sys::Process::GetEnv(FlagsEnvVar))
    llvm::cl::TokenizeGNUCommandLine(*EnvValue, EnvSaver, ExpandedArgv);
  for (int I = 1; I < argc; ++I)
    ExpandedArgv.push_back(argv[I]);
  llvm::clv2::OptionParser P;
  P.add<&ClangdReg>();
  registerCheckOptions(P);
  llvm::RegisterAllLLVMOptions(P);
  P.hideUnrelatedOptions(ClangdCategories);
  auto OptsCtx = P.parse(static_cast<int>(ExpandedArgv.size()),
                         ExpandedArgv.data(), Overview);
  if (!OptsCtx)
    return 1;
  applyClangdOptions(*OptsCtx);
  applyCheckOptions(*OptsCtx);
  if (Test) {
    if (!SyncSet)
      Sync = true;
    if (!CrashPragmasSet)
      CrashPragmas = true;
    InputStyle = JSONStreamStyle::Delimited;
    LogLevel = Logger::Verbose;
    PrettyPrint = true;
    if (!EnableConfigSet)
      EnableConfig = false;
    if (!EnableBackgroundIndexSet)
      EnableBackgroundIndex = false;
    else if (EnableBackgroundIndex)
      BackgroundQueue::preventThreadStarvationInTests();
  }
  if (Test || EnableTestScheme) {
    static URISchemeRegistry::Add<TestScheme> X(
        "test", "Test scheme for clangd lit tests.");
  }
  if (CrashPragmas)
    allowCrashPragmasForTest();

  if (!Sync && WorkerThreadsCount == 0) {
    llvm::errs() << "A number of worker threads cannot be 0. Did you mean to "
                    "specify -sync?";
    return 1;
  }

  if (Sync) {
    if (WorkerThreadsCountSet)
      llvm::errs() << "Ignoring -j because -sync is set.\n";
    WorkerThreadsCount = 0;
  }
  if (FallbackStyleSet)
    clang::format::DefaultFallbackStyle = FallbackStyle.c_str();

  // Validate command line arguments.
  std::optional<llvm::raw_fd_ostream> InputMirrorStream;
  if (!InputMirrorFile.empty()) {
    std::error_code EC;
    InputMirrorStream.emplace(InputMirrorFile, /*ref*/ EC,
                              llvm::sys::fs::FA_Read | llvm::sys::fs::FA_Write);
    if (EC) {
      InputMirrorStream.reset();
      llvm::errs() << "Error while opening an input mirror file: "
                   << EC.message();
    } else {
      InputMirrorStream->SetUnbuffered();
    }
  }

#if !CLANGD_DECISION_FOREST
  if (RankingModel == clangd::CodeCompleteOptions::DecisionForest) {
    llvm::errs() << "Clangd was compiled without decision forest support.\n";
    return 1;
  }
#endif

  // Setup tracing facilities if CLANGD_TRACE is set. In practice enabling a
  // trace flag in your editor's config is annoying, launching with
  // `CLANGD_TRACE=trace.json vim` is easier.
  std::optional<llvm::raw_fd_ostream> TracerStream;
  std::unique_ptr<trace::EventTracer> Tracer;
  const char *JSONTraceFile = getenv("CLANGD_TRACE");
  const char *MetricsCSVFile = getenv("CLANGD_METRICS");
  const char *TracerFile = JSONTraceFile ? JSONTraceFile : MetricsCSVFile;
  if (TracerFile) {
    std::error_code EC;
    TracerStream.emplace(TracerFile, /*ref*/ EC,
                         llvm::sys::fs::FA_Read | llvm::sys::fs::FA_Write);
    if (EC) {
      TracerStream.reset();
      llvm::errs() << "Error while opening trace file " << TracerFile << ": "
                   << EC.message();
    } else {
      Tracer = (TracerFile == JSONTraceFile)
                   ? trace::createJSONTracer(*TracerStream, PrettyPrint)
                   : trace::createCSVMetricTracer(*TracerStream);
    }
  }

  std::optional<trace::Session> TracingSession;
  if (Tracer)
    TracingSession.emplace(*Tracer);

  // If a user ran `clangd` in a terminal without redirecting anything,
  // it's somewhat likely they're confused about how to use clangd.
  // Show them the help overview, which explains.
  if (llvm::outs().is_displayed() && llvm::errs().is_displayed() &&
      !CheckFileSet)
    llvm::errs() << Overview << "\n";
  // Use buffered stream to stderr (we still flush each log message). Unbuffered
  // stream can cause significant (non-deterministic) latency for the logger.
  llvm::errs().SetBuffered();
  StreamLogger Logger(llvm::errs(), LogLevel);
  LoggingSession LoggingSession(Logger);
  // Write some initial logs before we start doing any real work.
  log("{0}", versionString());
  log("Features: {0}", featureString());
  log("PID: {0}", llvm::sys::Process::getProcessId());
  {
    SmallString<128> CWD;
    if (auto Err = llvm::sys::fs::current_path(CWD))
      log("Working directory unknown: {0}", Err.message());
    else
      log("Working directory: {0}", CWD);
  }
  for (int I = 0; I < argc; ++I)
    log("argv[{0}]: {1}", I, argv[I]);
  if (auto EnvFlags = llvm::sys::Process::GetEnv(FlagsEnvVar))
    log("{0}: {1}", FlagsEnvVar, *EnvFlags);
  // Log environment variables that influence how clangd finds system headers.
  // This helps diagnose missing-include issues, especially on Windows.
  for (const char *EnvVar : {
           // MSVC environment variables (set by vcvarsall.bat)
           "INCLUDE",
           "LIB",
           "LIBPATH",
           "CL",
           "_CL_",
           // GCC/Clang environment variables
           "CPATH",
           "C_INCLUDE_PATH",
           "CPLUS_INCLUDE_PATH",
           "OBJC_INCLUDE_PATH",
           "LIBRARY_PATH",
           "GCC_EXEC_PREFIX",
       }) {
    if (auto Val = llvm::sys::Process::GetEnv(EnvVar))
      log("Env {0}: {1}", EnvVar, *Val);
  }

  ClangdLSPServer::Options Opts;
  Opts.UseDirBasedCDB = (CompileArgsFromVal == FilesystemCompileArgs);
  Opts.EnableExperimentalModulesSupport = ExperimentalModulesSupport;

  switch (PCHStorage) {
  case PCHStorageFlag::Memory:
    Opts.StorePreamblesInMemory = true;
    break;
  case PCHStorageFlag::Disk:
    Opts.StorePreamblesInMemory = false;
    break;
  }
  if (!ResourceDir.empty())
    Opts.ResourceDir = ResourceDir;
  Opts.StrongWorkspaceMode = StrongWorkspaceMode;
  Opts.BuildDynamicSymbolIndex = true;
#if CLANGD_ENABLE_REMOTE
  if (RemoteIndexAddress.empty() != ProjectRoot.empty()) {
    llvm::errs() << "remote-index-address and project-path have to be "
                    "specified at the same time.";
    return 1;
  }
  if (!RemoteIndexAddress.empty()) {
    if (IndexFile.empty()) {
      log("Connecting to remote index at {0}", RemoteIndexAddress);
    } else {
      elog("When enabling remote index, IndexFile should not be specified. "
           "Only one can be used at time. Remote index will ignored.");
    }
  }
#endif
  Opts.BackgroundIndex = EnableBackgroundIndex;
  Opts.BackgroundIndexPriority = BackgroundIndexPriority;
  Opts.ReferencesLimit = ReferencesLimit;
  Opts.Rename.LimitFiles = RenameFileLimit;
  auto PAI = createProjectAwareIndex(
      [SupportContainedRefs = Opts.EnableOutgoingCalls](
          const Config::ExternalIndexSpec &External, AsyncTaskRunner *Tasks) {
        return loadExternalIndex(External, Tasks, SupportContainedRefs);
      },
      Sync);
  Opts.StaticIndex = PAI.get();
  Opts.AsyncThreadsCount = WorkerThreadsCount;
  Opts.MemoryCleanup = getMemoryCleanupFunction();

  Opts.CodeComplete.IncludeIneligibleResults = IncludeIneligibleResults;
  Opts.CodeComplete.Limit = LimitResults;
  if (CompletionStyleSet)
    Opts.CodeComplete.BundleOverloads = CompletionStyle != Detailed;
  Opts.CodeComplete.ShowOrigins = ShowOrigins;
  Opts.CodeComplete.InsertIncludes = HeaderInsertion;
  Opts.CodeComplete.ImportInsertions = ImportInsertions;
  if (!HeaderInsertionDecorators) {
    Opts.CodeComplete.IncludeIndicator.Insert.clear();
    Opts.CodeComplete.IncludeIndicator.NoInsert.clear();
  }
  Opts.CodeComplete.RunParser = CodeCompletionParse;
  Opts.CodeComplete.RankingModel = RankingModel;
  // FIXME: If we're using C++20 modules, force the lookup process to load
  // external decls, since currently the index doesn't support C++20 modules.
  Opts.CodeComplete.ForceLoadPreamble = ExperimentalModulesSupport;

  RealThreadsafeFS TFS;
  std::vector<std::unique_ptr<config::Provider>> ProviderStack;
  std::unique_ptr<config::Provider> Config;
  if (EnableConfig) {
    ProviderStack.push_back(
        config::Provider::fromAncestorRelativeYAMLFiles(".clangd", TFS));
    llvm::SmallString<256> UserConfig;
    if (llvm::sys::path::user_config_directory(UserConfig)) {
      llvm::sys::path::append(UserConfig, "clangd", "config.yaml");
      vlog("User config file is {0}", UserConfig);
      ProviderStack.push_back(config::Provider::fromYAMLFile(
          UserConfig, /*Directory=*/"", TFS, /*Trusted=*/true));
    } else {
      elog("Couldn't determine user config file, not loading");
    }
  }
  ProviderStack.push_back(std::make_unique<FlagsConfigProvider>());
  std::vector<const config::Provider *> ProviderPointers;
  for (const auto &P : ProviderStack)
    ProviderPointers.push_back(P.get());
  Config = config::Provider::combine(std::move(ProviderPointers));
  Opts.ConfigProvider = Config.get();

  // Create an empty clang-tidy option.
  TidyProvider ClangTidyOptProvider;
  if (EnableClangTidy) {
    std::vector<TidyProvider> Providers;
    Providers.reserve(4 + EnableConfig);
    Providers.push_back(provideEnvironment());
    Providers.push_back(provideClangTidyFiles(TFS));
    if (EnableConfig)
      Providers.push_back(provideClangdConfig());
    Providers.push_back(provideDefaultChecks());
    Providers.push_back(disableUnusableChecks());
    ClangTidyOptProvider = combine(std::move(Providers));
    Opts.ClangTidyProvider = ClangTidyOptProvider;
  }
  Opts.UseDirtyHeaders = UseDirtyHeaders;
  Opts.PreambleParseForwardingFunctions = PreambleParseForwardingFunctions;
  Opts.SkipPreambleBuild = SkipPreambleBuild;
  Opts.ImportInsertions = ImportInsertions;
  Opts.QueryDriverGlobs = std::move(QueryDriverGlobs);
  Opts.TweakFilter = [&](const Tweak &T) {
    if (T.hidden() && !HiddenFeatures)
      return false;
    if (TweakListSet)
      return llvm::is_contained(TweakList, T.id());
    return true;
  };
  if (ForceOffsetEncoding != OffsetEncoding::UnsupportedEncoding)
    Opts.Encoding = ForceOffsetEncoding;

  if (CheckFileSet) {
    llvm::SmallString<256> Path;
    if (auto Error =
            llvm::sys::fs::real_path(CheckFile, Path, /*expand_tilde=*/true)) {
      elog("Failed to resolve path {0}: {1}", CheckFile, Error.message());
      return 1;
    }
    log("Entering check mode (no LSP server)");
    return check(Path, TFS, Opts)
               ? 0
               : static_cast<int>(ErrorResultCode::CheckFailed);
  }

  FeatureModuleSet ModuleSet = FeatureModuleSet::fromRegistry();
  if (ModuleSet.begin() != ModuleSet.end())
    Opts.FeatureModules = &ModuleSet;

  // Initialize and run ClangdLSPServer.
  // Change stdin to binary to not lose \r\n on windows.
  llvm::sys::ChangeStdinToBinary();
  std::unique_ptr<Transport> TransportLayer;
  if (getenv("CLANGD_AS_XPC_SERVICE")) {
#if CLANGD_BUILD_XPC
    log("Starting LSP over XPC service");
    TransportLayer = newXPCTransport();
#else
    llvm::errs() << "This clangd binary wasn't built with XPC support.\n";
    return static_cast<int>(ErrorResultCode::CantRunAsXPCService);
#endif
  } else {
    log("Starting LSP over stdin/stdout");
    TransportLayer = newJSONTransport(
        stdin, llvm::outs(), InputMirrorStream ? &*InputMirrorStream : nullptr,
        PrettyPrint, InputStyle);
  }
  if (!PathMappingsArg.empty()) {
    auto Mappings = parsePathMappings(PathMappingsArg);
    if (!Mappings) {
      elog("Invalid -path-mappings: {0}", Mappings.takeError());
      return 1;
    }
    TransportLayer = createPathMappingTransport(std::move(TransportLayer),
                                                std::move(*Mappings));
  }

  ClangdLSPServer LSPServer(*TransportLayer, TFS, Opts);
  llvm::set_thread_name("clangd.main");
  int ExitCode = LSPServer.run()
                     ? 0
                     : static_cast<int>(ErrorResultCode::NoShutdownRequest);
  log("LSP finished, exiting with status {0}", ExitCode);

  // There may still be lingering background threads (e.g. slow requests
  // whose results will be dropped, background index shutting down).
  //
  // These should terminate quickly, and ~ClangdLSPServer blocks on them.
  // However if a bug causes them to run forever, we want to ensure the process
  // eventually exits. As clangd isn't directly user-facing, an editor can
  // "leak" clangd processes. Crashing in this case contains the damage.
  abortAfterTimeout(std::chrono::minutes(5));

  return ExitCode;
}

} // namespace clangd
} // namespace clang
