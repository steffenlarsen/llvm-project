//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
///  \file This file implements a clang-tidy tool.
///
///  This tool uses the Clang Tooling infrastructure, see
///    https://clang.llvm.org/docs/HowToSetupToolingForLLVM.html
///  for details on setting it up with LLVM source tree.
///
//===----------------------------------------------------------------------===//

#include "ClangTidyMain.h"
#include "../ClangTidy.h"
#include "../ClangTidyForceLinker.h" // IWYU pragma: keep
#include "../GlobList.h"
#include "clang-tools-extra/ClangToolsExtraOptionsOptInfos.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/PluginLoader.h" // IWYU pragma: keep
#include "llvm/Support/Process.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/WithColor.h"
#include "llvm/TargetParser/Host.h"
#include <optional>

using namespace clang::tooling;
using namespace llvm;

static cl::OptionCategory ClangTidyCategory("clang-tidy options");
static constexpr clv2::OptionCategory
    Clv2ClangTidyCategory("clang-tidy options");

static constexpr llvm::StringLiteral ClangTidyParameterFileHelpText{R"(
Parameters files:
  A large number of options or source files can be passed as parameter files
  by use '@parameter-file' in the command line.
)"};
static constexpr llvm::StringLiteral ClangTidyHelpText{R"(
Configuration files:
  clang-tidy attempts to read configuration for each source file from a
  .clang-tidy file located in the closest parent directory of the source
  file. The .clang-tidy file is specified in YAML format. If any configuration
  options have a corresponding command-line option, command-line option takes
  precedence.

  The following configuration options may be used in a .clang-tidy file:

  CheckOptions                 - List of key-value pairs defining check-specific
                                 options. Example:
                                   CheckOptions:
                                     some-check.SomeOption: 'some value'
  Checks                       - Same as '--checks'. Additionally, the list of
                                 globs can be specified as a list instead of a
                                 string.
  CustomChecks                 - Array of user defined checks based on
                                 Clang-Query syntax.
  ExcludeHeaderFilterRegex     - Same as '--exclude-header-filter'.
  ExtraArgs                    - Same as '--extra-arg'.
  ExtraArgsBefore              - Same as '--extra-arg-before'.
  FormatStyle                  - Same as '--format-style'.
  HeaderFileExtensions         - File extensions to consider to determine if a
                                 given diagnostic is located in a header file.
  HeaderFilterRegex            - Same as '--header-filter'.
  ImplementationFileExtensions - File extensions to consider to determine if a
                                 given diagnostic is located in an
                                 implementation file.
  InheritParentConfig          - If this option is true in a config file, the
                                 configuration file in the parent directory
                                 (if any exists) will be taken and the current
                                 config file will be applied on top of the
                                 parent one.
  RemovedArgs                  - Same as '--removed-arg'.
  SystemHeaders                - Same as '--system-headers'.
  UseColor                     - Same as '--use-color'.
  User                         - Specifies the name or e-mail of the user
                                 running clang-tidy. This option is used, for
                                 example, to place the correct user name in
                                 TODO() comments in the relevant check.
  WarningsAsErrors             - Same as '--warnings-as-errors'.

  The effective configuration can be inspected using --dump-config:

    $ clang-tidy --dump-config
    ---
    Checks:                       '-*,some-check'
    WarningsAsErrors:             ''
    HeaderFileExtensions:         ['', 'h','hh','hpp','hxx']
    ImplementationFileExtensions: ['c','cc','cpp','cxx']
    HeaderFilterRegex:            '.*'
    FormatStyle:                  none
    InheritParentConfig:          true
    User:                         user
    CheckOptions:
      some-check.SomeOption: 'some value'
    ...

)"};

const char DefaultChecks[] = // Enable these checks by default:
    "clang-diagnostic-*";    //   * compiler diagnostics

// Option value storage.
static std::string Checks = "";
static bool ChecksSet = false;
static std::string WarningsAsErrors = "";
static bool WarningsAsErrorsSet = false;
static std::string HeaderFilter = ".*";
static bool HeaderFilterSet = false;
static std::string ExcludeHeaderFilter = "";
static bool ExcludeHeaderFilterSet = false;
static bool SystemHeaders = false;
static bool SystemHeadersSet = false;
static std::string LineFilter = "";
static bool Fix = false;
static bool FixErrors = false;
static bool FixNotes = false;
static std::string FormatStyle = "none";
static bool FormatStyleSet = false;
static bool ListChecks = false;
static bool ExplainConfig = false;
static std::string Config = "";
static bool ConfigSet = false;
static std::string ConfigFile = "";
static bool ConfigFileSet = false;
static bool DumpConfig = false;
static bool EnableCheckProfile = false;
static std::string StoreCheckProfile = "";
static bool AllowEnablingAnalyzerAlphaCheckers = false;
static bool EnableModuleHeadersParsing = false;
static std::string ExportFixes = "";
static bool Quiet = false;
static std::string VfsOverlay = "";
static bool UseColor = false;
static bool UseColorSet = false;
static bool VerifyConfig = false;
static bool AllowNoChecks = false;
static bool ExperimentalCustomChecks = false;
static std::vector<std::string> RemovedArgs;
static bool RemovedArgsSet = false;

inline constexpr clv2::OptionsRegistry<
    &clv2::CTE_CT_Checks, &clv2::CTE_CT_WarningsAsErrors,
    &clv2::CTE_CT_HeaderFilter, &clv2::CTE_CT_ExcludeHeaderFilter,
    &clv2::CTE_CT_SystemHeaders, &clv2::CTE_CT_LineFilter, &clv2::CTE_CT_Fix,
    &clv2::CTE_CT_FixErrors, &clv2::CTE_CT_FixNotes, &clv2::CTE_CT_FormatStyle,
    &clv2::CTE_CT_ListChecks, &clv2::CTE_CT_ExplainConfig, &clv2::CTE_CT_Config,
    &clv2::CTE_CT_ConfigFile, &clv2::CTE_CT_DumpConfig,
    &clv2::CTE_CT_EnableCheckProfile, &clv2::CTE_CT_StoreCheckProfile,
    &clv2::CTE_CT_AllowEnablingAnalyzerAlphaCheckers,
    &clv2::CTE_CT_EnableModuleHeadersParsing, &clv2::CTE_CT_ExportFixes,
    &clv2::CTE_CT_Quiet, &clv2::CTE_CT_VfsOverlay, &clv2::CTE_CT_UseColor,
    &clv2::CTE_CT_VerifyConfig, &clv2::CTE_CT_AllowNoChecks,
    &clv2::CTE_CT_ExperimentalCustomChecks, &clv2::CTE_CT_RemovedArgs>
    ToolOptsReg;

static void applyToolOpts(const decltype(ToolOptsReg)::ParsedOptionsT &Opts) {
  Checks = Opts.get<&clv2::CTE_CT_Checks>();
  ChecksSet = Opts.specified<&clv2::CTE_CT_Checks>();
  WarningsAsErrors = Opts.get<&clv2::CTE_CT_WarningsAsErrors>();
  WarningsAsErrorsSet = Opts.specified<&clv2::CTE_CT_WarningsAsErrors>();
  HeaderFilter = Opts.get<&clv2::CTE_CT_HeaderFilter>();
  HeaderFilterSet = Opts.specified<&clv2::CTE_CT_HeaderFilter>();
  ExcludeHeaderFilter = Opts.get<&clv2::CTE_CT_ExcludeHeaderFilter>();
  ExcludeHeaderFilterSet = Opts.specified<&clv2::CTE_CT_ExcludeHeaderFilter>();
  SystemHeaders = Opts.get<&clv2::CTE_CT_SystemHeaders>();
  SystemHeadersSet = Opts.specified<&clv2::CTE_CT_SystemHeaders>();
  LineFilter = Opts.get<&clv2::CTE_CT_LineFilter>();
  Fix = Opts.get<&clv2::CTE_CT_Fix>();
  FixErrors = Opts.get<&clv2::CTE_CT_FixErrors>();
  FixNotes = Opts.get<&clv2::CTE_CT_FixNotes>();
  FormatStyle = Opts.get<&clv2::CTE_CT_FormatStyle>();
  FormatStyleSet = Opts.specified<&clv2::CTE_CT_FormatStyle>();
  ListChecks = Opts.get<&clv2::CTE_CT_ListChecks>();
  ExplainConfig = Opts.get<&clv2::CTE_CT_ExplainConfig>();
  Config = Opts.get<&clv2::CTE_CT_Config>();
  ConfigSet = Opts.specified<&clv2::CTE_CT_Config>();
  ConfigFile = Opts.get<&clv2::CTE_CT_ConfigFile>();
  ConfigFileSet = Opts.specified<&clv2::CTE_CT_ConfigFile>();
  DumpConfig = Opts.get<&clv2::CTE_CT_DumpConfig>();
  EnableCheckProfile = Opts.get<&clv2::CTE_CT_EnableCheckProfile>();
  StoreCheckProfile = Opts.get<&clv2::CTE_CT_StoreCheckProfile>();
  AllowEnablingAnalyzerAlphaCheckers =
      Opts.get<&clv2::CTE_CT_AllowEnablingAnalyzerAlphaCheckers>();
  EnableModuleHeadersParsing =
      Opts.get<&clv2::CTE_CT_EnableModuleHeadersParsing>();
  ExportFixes = Opts.get<&clv2::CTE_CT_ExportFixes>();
  Quiet = Opts.get<&clv2::CTE_CT_Quiet>();
  VfsOverlay = Opts.get<&clv2::CTE_CT_VfsOverlay>();
  UseColor = Opts.get<&clv2::CTE_CT_UseColor>();
  UseColorSet = Opts.specified<&clv2::CTE_CT_UseColor>();
  VerifyConfig = Opts.get<&clv2::CTE_CT_VerifyConfig>();
  AllowNoChecks = Opts.get<&clv2::CTE_CT_AllowNoChecks>();
  ExperimentalCustomChecks = Opts.get<&clv2::CTE_CT_ExperimentalCustomChecks>();
  RemovedArgs = Opts.get<&clv2::CTE_CT_RemovedArgs>();
  RemovedArgsSet = Opts.specified<&clv2::CTE_CT_RemovedArgs>();
}

// The plugin is loaded as a side effect of parsing; Callback runs once per
// occurrence, after the value has been stored.
static std::string LoadedPluginPath;
static unsigned LoadPluginCount = 0;
static void loadTidyPlugin(const std::string &Path) {
  llvm::PluginLoader PL;
  PL = Path;
}
static constexpr clv2::OptionInfo<std::string> OI_LoadPlugin{
    "load",
    "Load the specified plugin",
    clv2::value_desc("pluginfilename"),
    clv2::ZeroOrMore,
    clv2::cat(ClangTidyCategory),
    clv2::Callback<std::string>{&loadTidyPlugin}};

static void configureParser(clv2::OptionParser &P) {
  using ParsedT = decltype(ToolOptsReg)::ParsedOptionsT;
  auto *Storage = new ParsedT();
  decltype(ToolOptsReg)::applyDefaultsTo(*Storage);
  std::vector<clv2::detail::OptionEntry> Entries;
  std::vector<clv2::detail::AliasEntry> Aliases;
  std::vector<clv2::detail::SubCommandSpec> SubSpecs;
  decltype(ToolOptsReg)::staticBuildInto(*Storage, Entries, Aliases, SubSpecs);
  for (auto &E : Entries) {
    if (!E.Cat)
      E.Cat = &ClangTidyCategory;
    P.addDynamicEntry(std::move(E));
  }
  clv2::registerDynamicPostParseCallback(
      [Storage]() { applyToolOpts(*Storage); });
  std::string FullExtraHelp = clang::tooling::CommonOptionsParser::HelpMessage;
  FullExtraHelp += ClangTidyParameterFileHelpText;
  FullExtraHelp += ClangTidyHelpText;
  P.setExtraHelp(FullExtraHelp);
  P.addDynamicEntry(
      clv2::makeEntry<&OI_LoadPlugin>(LoadedPluginPath, LoadPluginCount));
}

namespace clang::tidy {

static void printStats(const ClangTidyStats &Stats) {
  if (Stats.errorsIgnored()) {
    llvm::errs() << "Suppressed " << Stats.errorsIgnored() << " warnings (";
    StringRef Separator = "";
    if (Stats.ErrorsIgnoredNonUserCode) {
      llvm::errs() << Stats.ErrorsIgnoredNonUserCode << " in non-user code";
      Separator = ", ";
    }
    if (Stats.ErrorsIgnoredLineFilter) {
      llvm::errs() << Separator << Stats.ErrorsIgnoredLineFilter
                   << " due to line filter";
      Separator = ", ";
    }
    if (Stats.ErrorsIgnoredNOLINT) {
      llvm::errs() << Separator << Stats.ErrorsIgnoredNOLINT << " NOLINT";
      Separator = ", ";
    }
    if (Stats.ErrorsIgnoredCheckFilter)
      llvm::errs() << Separator << Stats.ErrorsIgnoredCheckFilter
                   << " with check filters";
    llvm::errs() << ").\n";
    if (Stats.ErrorsIgnoredNonUserCode)
      llvm::errs() << "Use -header-filter=.* or leave it as default to display "
                      "errors from all non-system headers. Use -system-headers "
                      "to display errors from system headers as well.\n";
  }
}

static std::unique_ptr<ClangTidyOptionsProvider>
createOptionsProvider(llvm::IntrusiveRefCntPtr<vfs::FileSystem> FS,
                      const CommonOptionsParser &OptionsParser) {
  ClangTidyGlobalOptions GlobalOptions;
  if (const std::error_code Err = parseLineFilter(LineFilter, GlobalOptions)) {
    llvm::errs() << "Invalid LineFilter: " << Err.message() << "\n\nUsage:\n";
    OptionsParser.printHelp(llvm::errs());
    return nullptr;
  }

  ClangTidyOptions DefaultOptions;
  DefaultOptions.Checks = DefaultChecks;
  DefaultOptions.WarningsAsErrors = "";
  DefaultOptions.HeaderFilterRegex = HeaderFilter;
  DefaultOptions.ExcludeHeaderFilterRegex = ExcludeHeaderFilter;
  DefaultOptions.SystemHeaders = SystemHeaders;
  DefaultOptions.FormatStyle = FormatStyle;
  DefaultOptions.User = llvm::sys::Process::GetEnv("USER");
  // USERNAME is used on Windows.
  if (!DefaultOptions.User)
    DefaultOptions.User = llvm::sys::Process::GetEnv("USERNAME");

  ClangTidyOptions OverrideOptions;
  if (ChecksSet)
    OverrideOptions.Checks = Checks;
  if (WarningsAsErrorsSet)
    OverrideOptions.WarningsAsErrors = WarningsAsErrors;
  if (HeaderFilterSet)
    OverrideOptions.HeaderFilterRegex = HeaderFilter;
  if (ExcludeHeaderFilterSet)
    OverrideOptions.ExcludeHeaderFilterRegex = ExcludeHeaderFilter;
  if (SystemHeadersSet)
    OverrideOptions.SystemHeaders = SystemHeaders;
  if (FormatStyleSet)
    OverrideOptions.FormatStyle = FormatStyle;
  if (UseColorSet)
    OverrideOptions.UseColor = UseColor;
  if (RemovedArgsSet)
    OverrideOptions.RemovedArgs = RemovedArgs;

  const auto LoadConfig =
      [&](StringRef Configuration,
          StringRef Source) -> std::unique_ptr<ClangTidyOptionsProvider> {
    llvm::ErrorOr<ClangTidyOptions> ParsedConfig =
        parseConfiguration(MemoryBufferRef(Configuration, Source));
    if (ParsedConfig)
      return std::make_unique<ConfigOptionsProvider>(
          std::move(GlobalOptions),
          ClangTidyOptions::getDefaults().merge(DefaultOptions, 0),
          std::move(*ParsedConfig), std::move(OverrideOptions), std::move(FS));
    llvm::errs() << "Error: invalid configuration specified.\n"
                 << ParsedConfig.getError().message() << "\n";
    return nullptr;
  };

  if (ConfigFileSet) {
    if (ConfigSet) {
      llvm::errs() << "Error: --config-file and --config are "
                      "mutually exclusive. Specify only one.\n";
      return nullptr;
    }

    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> Text =
        llvm::MemoryBuffer::getFile(ConfigFile);
    if (const std::error_code EC = Text.getError()) {
      llvm::errs() << "Error: can't read config-file '" << ConfigFile
                   << "': " << EC.message() << "\n";
      return nullptr;
    }

    return LoadConfig((*Text)->getBuffer(), ConfigFile);
  }

  if (ConfigSet)
    return LoadConfig(Config, "<command-line-config>");

  return std::make_unique<FileOptionsProvider>(
      std::move(GlobalOptions), std::move(DefaultOptions),
      std::move(OverrideOptions), std::move(FS));
}

static llvm::IntrusiveRefCntPtr<vfs::FileSystem>
getVfsFromFile(const std::string &OverlayFile, vfs::FileSystem &BaseFS) {
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> Buffer =
      BaseFS.getBufferForFile(OverlayFile);
  if (!Buffer) {
    llvm::errs() << "Can't load virtual filesystem overlay file '"
                 << OverlayFile << "': " << Buffer.getError().message()
                 << ".\n";
    return nullptr;
  }

  IntrusiveRefCntPtr<vfs::FileSystem> FS = vfs::getVFSFromYAML(
      std::move(Buffer.get()), /*DiagHandler*/ nullptr, OverlayFile);
  if (!FS) {
    llvm::errs() << "Error: invalid virtual filesystem overlay file '"
                 << OverlayFile << "'.\n";
    return nullptr;
  }
  return FS;
}

static StringRef closest(StringRef Value, const StringSet<> &Allowed) {
  unsigned MaxEdit = 5U;
  StringRef Closest;
  for (const auto Item : Allowed.keys()) {
    const unsigned Cur = Value.edit_distance_insensitive(Item, true, MaxEdit);
    if (Cur < MaxEdit) {
      Closest = Item;
      MaxEdit = Cur;
    }
  }
  return Closest;
}

static constexpr StringLiteral VerifyConfigWarningEnd = " [-verify-config]\n";

static bool verifyChecks(const StringSet<> &AllChecks, StringRef CheckGlob,
                         StringRef Source) {
  const GlobList Globs(CheckGlob);
  bool AnyInvalid = false;
  for (const auto &Item : Globs.getItems()) {
    if (Item.Text.starts_with("clang-diagnostic"))
      continue;
    if (llvm::none_of(AllChecks.keys(),
                      [&Item](StringRef S) { return Item.Regex.match(S); })) {
      AnyInvalid = true;
      if (Item.Text.contains('*')) {
        llvm::WithColor::warning(llvm::errs(), Source)
            << "check glob '" << Item.Text << "' doesn't match any known check"
            << VerifyConfigWarningEnd;
      } else {
        llvm::raw_ostream &Output =
            llvm::WithColor::warning(llvm::errs(), Source)
            << "unknown check '" << Item.Text << '\'';
        const StringRef Closest = closest(Item.Text, AllChecks);
        if (!Closest.empty())
          Output << "; did you mean '" << Closest << '\'';
        Output << VerifyConfigWarningEnd;
      }
    }
  }
  return AnyInvalid;
}

static bool verifyFileExtensions(
    const std::vector<std::string> &HeaderFileExtensions,
    const std::vector<std::string> &ImplementationFileExtensions,
    StringRef Source) {
  bool AnyInvalid = false;
  for (const auto &HeaderExtension : HeaderFileExtensions) {
    for (const auto &ImplementationExtension : ImplementationFileExtensions) {
      if (HeaderExtension == ImplementationExtension) {
        AnyInvalid = true;
        auto &Output = llvm::WithColor::warning(llvm::errs(), Source)
                       << "HeaderFileExtension '" << HeaderExtension << '\''
                       << " is the same as ImplementationFileExtension '"
                       << ImplementationExtension << '\'';
        Output << VerifyConfigWarningEnd;
      }
    }
  }
  return AnyInvalid;
}

static bool verifyOptions(const llvm::StringSet<> &ValidOptions,
                          const ClangTidyOptions::OptionMap &OptionMap,
                          StringRef Source) {
  bool AnyInvalid = false;
  for (const auto Key : OptionMap.keys()) {
    if (ValidOptions.contains(Key))
      continue;
    AnyInvalid = true;
    auto &Output = llvm::WithColor::warning(llvm::errs(), Source)
                   << "unknown check option '" << Key << '\'';
    const StringRef Closest = closest(Key, ValidOptions);
    if (!Closest.empty())
      Output << "; did you mean '" << Closest << '\'';
    Output << VerifyConfigWarningEnd;
  }
  return AnyInvalid;
}

static SmallString<256> makeAbsolute(StringRef Input) {
  if (Input.empty())
    return {};
  SmallString<256> AbsolutePath(Input);
  if (const std::error_code EC = llvm::sys::fs::make_absolute(AbsolutePath)) {
    llvm::errs() << "Can't make absolute path from " << Input << ": "
                 << EC.message() << "\n";
  }
  return AbsolutePath;
}

static llvm::IntrusiveRefCntPtr<vfs::OverlayFileSystem> createBaseFS() {
  llvm::IntrusiveRefCntPtr<vfs::OverlayFileSystem> BaseFS(
      new vfs::OverlayFileSystem(vfs::getRealFileSystem()));

  if (!VfsOverlay.empty()) {
    IntrusiveRefCntPtr<vfs::FileSystem> VfsFromFile =
        getVfsFromFile(VfsOverlay, *BaseFS);
    if (!VfsFromFile)
      return nullptr;
    BaseFS->pushOverlay(std::move(VfsFromFile));
  }
  return BaseFS;
}

int clangTidyMain(int argc, const char **argv) {
  const llvm::InitLLVM X(argc, argv);
  SmallVector<const char *> Args{argv, argv + argc};

  // expand parameters file to argc and argv.
  llvm::BumpPtrAllocator Alloc;
  llvm::cl::TokenizerCallback Tokenizer =
      llvm::Triple(llvm::sys::getProcessTriple()).isOSWindows()
          ? llvm::cl::TokenizeWindowsCommandLine
          : llvm::cl::TokenizeGNUCommandLine;
  llvm::cl::ExpansionContext ECtx(Alloc, Tokenizer);
  if (llvm::Error Err = ECtx.expandResponseFiles(Args)) {
    llvm::WithColor::error() << llvm::toString(std::move(Err)) << "\n";
    return 1;
  }
  argc = static_cast<int>(Args.size());
  argv = Args.data();

  llvm::Expected<CommonOptionsParser> OptionsParser =
      CommonOptionsParser::create(argc, argv, ClangTidyCategory,
                                  configureParser, cl::ZeroOrMore);
  if (!OptionsParser) {
    llvm::WithColor::error() << llvm::toString(OptionsParser.takeError());
    return 1;
  }

  const llvm::IntrusiveRefCntPtr<vfs::OverlayFileSystem> BaseFS =
      createBaseFS();
  if (!BaseFS)
    return 1;

  auto OwningOptionsProvider = createOptionsProvider(BaseFS, *OptionsParser);
  auto *OptionsProvider = OwningOptionsProvider.get();
  if (!OptionsProvider)
    return 1;

  const SmallString<256> ProfilePrefix = makeAbsolute(StoreCheckProfile);

  StringRef FileName("dummy");
  auto PathList = OptionsParser->getSourcePathList();
  if (!PathList.empty())
    FileName = PathList.front();

  const SmallString<256> FilePath = makeAbsolute(FileName);
  ClangTidyOptions EffectiveOptions = OptionsProvider->getOptions(FilePath);

  const std::vector<std::string> EnabledChecks =
      getCheckNames(EffectiveOptions, AllowEnablingAnalyzerAlphaCheckers,
                    ExperimentalCustomChecks);

  if (ExplainConfig) {
    // FIXME: Show other ClangTidyOptions' fields, like ExtraArg.
    std::vector<ClangTidyOptionsProvider::OptionsSource> RawOptions =
        OptionsProvider->getRawOptions(FilePath);
    for (const std::string &Check : EnabledChecks) {
      for (const auto &[Opts, Source] : llvm::reverse(RawOptions)) {
        if (Opts.Checks && GlobList(*Opts.Checks).contains(Check)) {
          llvm::outs() << "'" << Check << "' is enabled in the " << Source
                       << ".\n";
          break;
        }
      }
    }
    return 0;
  }

  if (ListChecks) {
    if (EnabledChecks.empty() && !AllowNoChecks) {
      llvm::errs() << "No checks enabled.\n";
      return 1;
    }
    llvm::outs() << "Enabled checks:";
    for (const auto &CheckName : EnabledChecks)
      llvm::outs() << "\n    " << CheckName;
    llvm::outs() << "\n\n";
    return 0;
  }

  if (DumpConfig) {
    EffectiveOptions.CheckOptions =
        getCheckOptions(EffectiveOptions, AllowEnablingAnalyzerAlphaCheckers,
                        ExperimentalCustomChecks);
    ClangTidyOptions OptionsToDump =
        ClangTidyOptions::getDefaults().merge(EffectiveOptions, 0);
    filterCheckOptions(OptionsToDump, EnabledChecks);
    llvm::outs() << configurationAsText(OptionsToDump) << "\n";
    return 0;
  }

  if (VerifyConfig) {
    const std::vector<ClangTidyOptionsProvider::OptionsSource> RawOptions =
        OptionsProvider->getRawOptions(FileName);
    const ChecksAndOptions Valid = getAllChecksAndOptions(
        AllowEnablingAnalyzerAlphaCheckers, ExperimentalCustomChecks);
    bool AnyInvalid = false;
    for (const auto &[Opts, Source] : RawOptions) {
      if (Opts.Checks)
        AnyInvalid |= verifyChecks(Valid.Checks, *Opts.Checks, Source);
      if (Opts.HeaderFileExtensions && Opts.ImplementationFileExtensions)
        AnyInvalid |=
            verifyFileExtensions(*Opts.HeaderFileExtensions,
                                 *Opts.ImplementationFileExtensions, Source);
      AnyInvalid |= verifyOptions(Valid.Options, Opts.CheckOptions, Source);
    }
    if (AnyInvalid)
      return 1;
    llvm::outs() << "No config errors detected.\n";
    return 0;
  }

  if (EnabledChecks.empty() && !AllowNoChecks) {
    llvm::errs() << "Error: no checks enabled.\n";
    OptionsParser->printHelp(llvm::errs());
    return 1;
  }

  if (PathList.empty()) {
    llvm::errs() << "Error: no input files specified.\n";
    OptionsParser->printHelp(llvm::errs());
    return 1;
  }

  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmParsers();

  ClangTidyContext Context(
      std::move(OwningOptionsProvider), AllowEnablingAnalyzerAlphaCheckers,
      EnableModuleHeadersParsing, ExperimentalCustomChecks);
  std::vector<ClangTidyError> Errors =
      runClangTidy(Context, OptionsParser->getCompilations(), PathList, BaseFS,
                   FixNotes, OptionsParser->getOptionsContext(),
                   EnableCheckProfile, ProfilePrefix, Quiet);
  const bool FoundErrors = llvm::any_of(Errors, [](const ClangTidyError &E) {
    return E.DiagLevel == ClangTidyError::Error;
  });

  // --fix-errors and --fix-notes imply --fix.
  const FixBehaviour Behaviour = FixNotes             ? FB_FixNotes
                                 : (Fix || FixErrors) ? FB_Fix
                                                      : FB_NoFix;

  const bool DisableFixes = FoundErrors && !FixErrors;

  unsigned WErrorCount = 0;

  handleErrors(Errors, Context, DisableFixes ? FB_NoFix : Behaviour,
               WErrorCount, BaseFS);

  if (!ExportFixes.empty() && !Errors.empty()) {
    std::error_code EC;
    llvm::raw_fd_ostream OS(ExportFixes, EC, llvm::sys::fs::OF_None);
    if (EC) {
      llvm::errs() << "Error opening output file: " << EC.message() << '\n';
      return 1;
    }
    exportReplacements(FilePath.str(), Errors, OS);
  }

  if (!Quiet) {
    printStats(Context.getStats());
    if (DisableFixes && Behaviour != FB_NoFix)
      llvm::errs()
          << "Found compiler errors, but -fix-errors was not specified.\n"
             "Fixes have NOT been applied.\n\n";
  }

  if (WErrorCount) {
    if (!Quiet) {
      const StringRef Plural = WErrorCount == 1 ? "" : "s";
      llvm::errs() << WErrorCount << " warning" << Plural << " treated as error"
                   << Plural << "\n";
    }
    return 1;
  }

  if (FoundErrors) {
    // TODO: Figure out when zero exit code should be used with -fix-errors:
    //   a. when a fix has been applied for an error
    //   b. when a fix has been applied for all errors
    //   c. some other condition.
    // For now always returning zero when -fix-errors is used.
    if (FixErrors)
      return 0;
    if (!Quiet)
      llvm::errs() << "Found compiler error(s).\n";
    return 1;
  }

  return 0;
}

} // namespace clang::tidy
