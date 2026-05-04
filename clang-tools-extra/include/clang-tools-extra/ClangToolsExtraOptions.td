//===- ClangToolsExtraOptions.td - CLV2 option defs ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// TableGen definitions for clang-tools-extra clv2 options.
//
// Naming convention: CTE_<Tool>_<OptName>
//   Tool abbreviations:
//     AR  = clang-apply-replacements
//     FAS = find-all-symbols
//     IF  = clang-include-fixer
//     PPT = pp-trace
//     CT  = clang-tidy
//     MOD = modularize
//     CN  = clang-change-namespace
//     IC  = clang-include-cleaner
//     CM  = clang-move
//     RF  = clang-reorder-fields
//     CQ  = clang-query
//     CD  = clang-doc
//     D   = clangd (main)
//     DC  = clangd check
//     DM  = clangd modules-builder
//     DR  = clangd remap
//     DI  = clangd indexer
//     DX  = dexp
//     DS  = clangd remote server
//
// Three options are NOT defined here because they require #ifdef guards:
//   CTE_D_MallocTrim       (__GLIBC__ && CLANGD_MALLOC_TRIM)
//   CTE_D_RemoteIndexAddress (CLANGD_ENABLE_REMOTE)
//   CTE_D_ProjectRoot       (CLANGD_ENABLE_REMOTE)
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def CTEOptsReg : Registry<"llvm::cte_opts"> {
  let ContextTypes = [];
}

// =========================================================================
// Enum types
// =========================================================================

// --- CTEIncludeFixerDBFormat (clang-include-fixer) ---

def IFDBFixed : EnumValue<"fixed", "Hard-coded mapping"> {
  let CppName = "Fixed";
  let Value = 0;
}
def IFDBYaml : EnumValue<"yaml",
    "Yaml database created by find-all-symbols"> {
  let CppName = "Yaml";
  let Value = 1;
}
def IFDBFuzzyYaml : EnumValue<"fuzzyYaml",
    "Yaml database, with fuzzy-matched names"> {
  let CppName = "FuzzyYaml";
  let Value = 2;
}

def CTEIncludeFixerDBFormatEnum : EnumType<"CTEIncludeFixerDBFormat",
    [IFDBFixed, IFDBYaml, IFDBFuzzyYaml]>;

// --- ICPrintStyle (clang-include-cleaner) ---

def ICPrintChanges : EnumValue<"changes", "Print symbolic changes"> {
  let CppName = "Changes";
}
def ICPrintFinal : EnumValue<"", "Print final code"> {
  let CppName = "Final";
}

def ICPrintStyleEnum : EnumType<"ICPrintStyle",
    [ICPrintChanges, ICPrintFinal]>;

// --- CTEClangDocFormat (clang-doc) ---

def CDFormatYaml : EnumValue<"yaml", "Documentation in YAML format."> {
  let CppName = "Yaml";
  let Value = 0;
}
def CDFormatMd : EnumValue<"md", "Documentation in MD format."> {
  let CppName = "Md";
  let Value = 1;
}
def CDFormatHtml : EnumValue<"html", "Documentation in HTML format."> {
  let CppName = "Html";
  let Value = 2;
}
def CDFormatJson : EnumValue<"json", "Documentation in JSON format"> {
  let CppName = "Json";
  let Value = 3;
}
def CDFormatMdMustache : EnumValue<"md_mustache",
    "Documentation in MD format."> {
  let CppName = "MdMustache";
  let Value = 4;
}

def CTEClangDocFormatEnum : EnumType<"CTEClangDocFormat",
    [CDFormatYaml, CDFormatMd, CDFormatHtml, CDFormatJson,
     CDFormatMdMustache]>;

// --- CTEClangdCompileArgsFrom ---

def CompileArgsLSP : EnumValue<"lsp",
    "All compile commands come from LSP and "
    "'compile_commands.json' files are ignored"> {
  let CppName = "LSP";
  let Value = 0;
}
def CompileArgsFilesystem : EnumValue<"filesystem",
    "All compile commands come from the 'compile_commands.json' files"> {
  let CppName = "Filesystem";
  let Value = 1;
}

def CTEClangdCompileArgsFromEnum : EnumType<"CTEClangdCompileArgsFrom",
    [CompileArgsLSP, CompileArgsFilesystem]>;

// --- CTEClangdBGIndexPriority ---

def BGIdxBackground : EnumValue<"background",
    "Minimum priority, runs on idle CPUs."> {
  let CppName = "Background";
  let Value = 0;
}
def BGIdxLow : EnumValue<"low",
    "Reduced priority compared to interactive work."> {
  let CppName = "Low";
  let Value = 1;
}
def BGIdxNormal : EnumValue<"normal",
    "Same priority as other clangd work."> {
  let CppName = "Normal";
  let Value = 2;
}

def CTEClangdBGIndexPriorityEnum : EnumType<"CTEClangdBGIndexPriority",
    [BGIdxBackground, BGIdxLow, BGIdxNormal]>;

// --- CTEClangdCompletionParse ---

def CompletionAlwaysParse : EnumValue<"always",
    "Block until the parser can be used"> {
  let CppName = "AlwaysParse";
  let Value = 0;
}
def CompletionParseIfReady : EnumValue<"auto",
    "Use text-based completion if the parser is not ready"> {
  let CppName = "ParseIfReady";
  let Value = 1;
}
def CompletionNeverParse : EnumValue<"never",
    "Always used text-based completion"> {
  let CppName = "NeverParse";
  let Value = 2;
}

def CTEClangdCompletionParseEnum : EnumType<"CTEClangdCompletionParse",
    [CompletionAlwaysParse, CompletionParseIfReady, CompletionNeverParse]>;

// --- CTEClangdRankingModel ---

def RankingHeuristics : EnumValue<"heuristics",
    "Use heuristics to rank code completion items"> {
  let CppName = "Heuristics";
  let Value = 0;
}
def RankingDecisionForest : EnumValue<"decision_forest",
    "Use Decision Forest model to rank completion items"> {
  let CppName = "DecisionForest";
  let Value = 1;
}

def CTEClangdRankingModelEnum : EnumType<"CTEClangdRankingModel",
    [RankingHeuristics, RankingDecisionForest]>;

// --- CTEClangdCompletionStyle ---

def CompletionDetailed : EnumValue<"detailed",
    "One completion item for each semantically distinct completion"> {
  let CppName = "Detailed";
  let Value = 0;
}
def CompletionBundled : EnumValue<"bundled",
    "Similar completion items are combined"> {
  let CppName = "Bundled";
  let Value = 1;
}

def CTEClangdCompletionStyleEnum : EnumType<"CTEClangdCompletionStyle",
    [CompletionDetailed, CompletionBundled]>;

// --- CTEClangdHeaderInsertion ---

def HeaderInsertionIWYU : EnumValue<"iwyu",
    "Include what you use. Insert the owning header for top-level symbols."> {
  let CppName = "IWYU";
  let Value = 0;
}
def HeaderInsertionNever : EnumValue<"never",
    "Never insert #include directives as part of code completion"> {
  let CppName = "NeverInsert";
  let Value = 1;
}

def CTEClangdHeaderInsertionEnum : EnumType<"CTEClangdHeaderInsertion",
    [HeaderInsertionIWYU, HeaderInsertionNever]>;

// --- CTEClangdPCHStorage ---

def PCHStorageDisk : EnumValue<"disk", "store PCHs on disk"> {
  let CppName = "Disk";
  let Value = 0;
}
def PCHStorageMemory : EnumValue<"memory", "store PCHs in memory"> {
  let CppName = "Memory";
  let Value = 1;
}

def CTEClangdPCHStorageEnum : EnumType<"CTEClangdPCHStorage",
    [PCHStorageDisk, PCHStorageMemory]>;

// --- CTEClangdInputStyle ---

def InputStyleStandard : EnumValue<"standard", "usual LSP protocol"> {
  let CppName = "Standard";
  let Value = 0;
}
def InputStyleDelimited : EnumValue<"delimited",
    "messages delimited by --- lines, with # comment support"> {
  let CppName = "Delimited";
  let Value = 1;
}

def CTEClangdInputStyleEnum : EnumType<"CTEClangdInputStyle",
    [InputStyleStandard, InputStyleDelimited]>;

// --- CTEClangdLogLevel ---

def LogError : EnumValue<"error", "Error messages only"> {
  let CppName = "Error";
  let Value = 0;
}
def LogInfo : EnumValue<"info", "High level execution tracing"> {
  let CppName = "Info";
  let Value = 1;
}
def LogVerbose : EnumValue<"verbose", "Low level details"> {
  let CppName = "Verbose";
  let Value = 2;
}

def CTEClangdLogLevelEnum : EnumType<"CTEClangdLogLevel",
    [LogError, LogInfo, LogVerbose]>;

// --- CTEClangdOffsetEncoding ---

def OffsetUTF8 : EnumValue<"utf-8", "Offsets are in UTF-8 bytes"> {
  let CppName = "UTF8";
  let Value = 0;
}
def OffsetUTF16 : EnumValue<"utf-16",
    "Offsets are in UTF-16 code units"> {
  let CppName = "UTF16";
  let Value = 1;
}
def OffsetUTF32 : EnumValue<"utf-32",
    "Offsets are in unicode codepoints"> {
  let CppName = "UTF32";
  let Value = 2;
}
def OffsetUnsupported : EnumValue<"unsupported",
    "No forced offset encoding (sentinel)"> {
  let CppName = "Unsupported";
  let Value = 3;
  let Sentinel = 1;
}

def CTEClangdOffsetEncodingEnum : EnumType<"CTEClangdOffsetEncoding",
    [OffsetUTF8, OffsetUTF16, OffsetUTF32, OffsetUnsupported]>;

// --- CTEIndexerFormat (clangd-indexer) ---

def IndexerYAML : EnumValue<"yaml", "human-readable YAML format"> {
  let CppName = "YAML";
  let Value = 0;
}
def IndexerRIFF : EnumValue<"binary", "binary RIFF format"> {
  let CppName = "RIFF";
  let Value = 1;
}

def CTEIndexerFormatEnum : EnumType<"CTEIndexerFormat",
    [IndexerYAML, IndexerRIFF]>;

// =========================================================================
// clang-apply-replacements options
// =========================================================================

def CTE_AR_Directory : Option<"",
    "<Search Root Directory>", "std::string"> {
  let Registry = CTEOptsReg;
  let GetterName = "getArDirectory";
  let Positional = 1;
  let Required = 1;
}

def CTE_AR_RemoveChangeDescFiles : Option<"remove-change-desc-files",
    "Remove the change description files regardless of successful\n"
    "merging/replacing.", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_AR_IgnoreInsertConflict : Option<"ignore-insert-conflict",
    "Ignore insert conflict and keep running to fix.", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_AR_Format : Option<"format",
    "Enable formatting of code changed by applying replacements.\n"
    "Use -style to choose formatting style.", "bool"> {
  let Registry = CTEOptsReg;
  let GetterName = "getArFormat";
  let Default = "false";
}

def CTE_AR_StyleConfig : Option<"style-config",
    "Path to a directory containing a .clang-format file\n"
    "describing a formatting style to use for formatting\n"
    "code when -style=file.", "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_AR_Style : Option<"style",
    "Set coding style. <string> can be:\n"
    "1. A preset: LLVM, GNU, Google, Chromium, Microsoft,\n"
    "   Mozilla, WebKit.\n"
    "2. 'file' to load style configuration from a\n"
    "   .clang-format file in one of the parent directories\n"
    "   of the source file (for stdin, see --assume-filename).\n"
    "   If no .clang-format file is found, falls back to\n"
    "   --fallback-style.\n"
    "   --style=file is the default.\n"
    "3. 'file:<format_file_path>' to explicitly specify\n"
    "   the configuration file.\n"
    "4. \"{key: value, ...}\" to set specific parameters, e.g.:\n"
    "   --style=\"{BasedOnStyle: llvm, IndentWidth: 8}\"",
    "std::string"> {
  let Registry = CTEOptsReg;
  let GetterName = "getArStyle";
  let Default = "\"LLVM\"";
}

// =========================================================================
// find-all-symbols options
// =========================================================================

def CTE_FAS_OutputDir : Option<"output-dir",
    "\nThe output directory for saving the results.",
    "std::string"> {
  let Registry = CTEOptsReg;
  let Default = "\".\"";
}

def CTE_FAS_MergeDir : Option<"merge-dir",
    "\nThe directory for merging symbols.",
    "std::string"> {
  let Registry = CTEOptsReg;
}

// =========================================================================
// clang-include-fixer options
// =========================================================================

def CTE_IF_DB : Option<"db", "Specify input format",
    "CTEIncludeFixerDBFormat"> {
  let Registry = CTEOptsReg;
  let Default = "yaml";
}

def CTE_IF_Input : Option<"input",
    "String to initialize the database",
    "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_IF_QuerySymbol : Option<"query-symbol",
    "Query a given symbol (e.g. \"a::b::foo\") in\n"
    "database directly without parsing the file.",
    "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_IF_MinimizePaths : Option<"minimize-paths",
    "Whether to minimize added include paths",
    "bool"> {
  let Registry = CTEOptsReg;
  let Default = "true";
}

def CTE_IF_Quiet : Option<"q", "Reduce terminal output", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_IF_Stdin : Option<"stdin",
    "Override source file's content (in the overlaying\n"
    "virtual file system) with input from <stdin> and run\n"
    "the tool on the new content with the compilation\n"
    "options of the source file. This mode is currently\n"
    "used for editor integration.", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_IF_OutputHeaders : Option<"output-headers",
    "Print the symbol being queried and all its relevant headers in\n"
    "JSON format to stdout:\n"
    "  {\n"
    "    \"FilePath\": \"/path/to/foo.cc\",\n"
    "    \"QuerySymbolInfos\": [\n"
    "       {\"RawIdentifier\": \"foo\",\n"
    "        \"Range\": {\"Offset\": 0, \"Length\": 3}}\n"
    "    ],\n"
    "    \"HeaderInfos\": [ {\"Header\": \"\\\"foo_a.h\\\"\",\n"
    "                      \"QualifiedName\": \"a::foo\"} ]\n"
    "  }", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_IF_InsertHeader : Option<"insert-header",
    "Insert a specific header. This should run with STDIN mode.\n"
    "The result is written to stdout. It is currently used for\n"
    "editor integration. Support YAML/JSON format:\n"
    "  -insert-header=\"{\n"
    "     FilePath: \"/path/to/foo.cc\",\n"
    "     QuerySymbolInfos: [\n"
    "       {RawIdentifier: foo,\n"
    "        Range: {Offset: 0, Length: 3}}\n"
    "     ],\n"
    "     HeaderInfos: [ {Headers: \"\\\"foo_a.h\\\"\",\n"
    "                     QualifiedName: \"a::foo\"} ]}\"",
    "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_IF_Style : Option<"style",
    "Fallback style for reformatting after inserting new\n"
    "headers if there is no clang-format config file found.",
    "std::string"> {
  let Registry = CTEOptsReg;
  let GetterName = "getIfStyle";
  let Default = "\"llvm\"";
}

// =========================================================================
// pp-trace options
// =========================================================================

def CTE_PPT_Callbacks : Option<"callbacks",
    "Comma-separated list of globs describing the list of callbacks to "
    "output. Globs are processed in order of appearance. Globs with the "
    "'-' prefix remove callbacks from the set. e.g. '*,-Macro*'.",
    "std::string"> {
  let Registry = CTEOptsReg;
  let Default = "\"*\"";
}

def CTE_PPT_Output : Option<"output",
    "Output trace to the given file name or '-' for stdout.",
    "std::string"> {
  let Registry = CTEOptsReg;
  let GetterName = "getPptOutput";
  let Default = "\"-\"";
}

// =========================================================================
// clang-tidy options
// =========================================================================

def CTE_CT_Checks : Option<"checks",
    "Comma-separated list of globs with optional '-'\n"
    "prefix. Globs are processed in order of\n"
    "appearance in the list. Globs without '-'\n"
    "prefix add checks with matching names to the\n"
    "set, globs with the '-' prefix remove checks\n"
    "with matching names from the set of enabled\n"
    "checks. This option's value is appended to the\n"
    "value of the 'Checks' option in .clang-tidy\n"
    "file, if any.\n", "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_CT_WarningsAsErrors : Option<"warnings-as-errors",
    "Upgrades warnings to errors. Same format as\n"
    "'-checks'.\n"
    "This option's value is appended to the value of\n"
    "the 'WarningsAsErrors' option in .clang-tidy\n"
    "file, if any.\n", "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_CT_HeaderFilter : Option<"header-filter",
    "Regular expression matching the names of the\n"
    "headers to output diagnostics from. The default\n"
    "value is '.*', i.e. diagnostics from all non-system\n"
    "headers are displayed by default. Diagnostics\n"
    "from the main file of each translation unit are\n"
    "always displayed.\n"
    "Can be used together with -line-filter.\n"
    "This option overrides the 'HeaderFilterRegex'\n"
    "option in .clang-tidy file, if any.\n",
    "std::string"> {
  let Registry = CTEOptsReg;
  let Default = "\".*\"";
}

def CTE_CT_ExcludeHeaderFilter : Option<"exclude-header-filter",
    "Regular expression matching the names of the\n"
    "headers to exclude diagnostics from. Diagnostics\n"
    "from the main file of each translation unit are\n"
    "always displayed.\n"
    "Must be used together with --header-filter.\n"
    "Can be used together with -line-filter.\n"
    "This option overrides the 'ExcludeHeaderFilterRegex'\n"
    "option in .clang-tidy file, if any.\n",
    "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_CT_SystemHeaders : Option<"system-headers",
    "Display the errors from system headers.\n"
    "This option overrides the 'SystemHeaders' option\n"
    "in .clang-tidy file, if any.\n", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_CT_LineFilter : Option<"line-filter",
    "List of files and line ranges to output diagnostics from.\n"
    "The range is inclusive on both ends. Can be used together\n"
    "with -header-filter. The format of the list is a JSON\n"
    "array of objects. For example:\n"
    "\n"
    "  [\n"
    "    {\"name\":\"file1.cpp\",\"lines\":[[1,3],[5,7]]},\n"
    "    {\"name\":\"file2.h\"}\n"
    "  ]\n"
    "\n"
    "This will output diagnostics from 'file1.cpp' only for\n"
    "the line ranges [1,3] and [5,7], as well as all from the\n"
    "entire 'file2.h'.\n", "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_CT_Fix : Option<"fix",
    "Apply suggested fixes. Without -fix-errors\n"
    "clang-tidy will bail out if any compilation\n"
    "errors were found.\n", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_CT_FixErrors : Option<"fix-errors",
    "Apply suggested fixes even if compilation\n"
    "errors were found. If compiler errors have\n"
    "attached fix-its, clang-tidy will apply them as\n"
    "well.\n", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_CT_FixNotes : Option<"fix-notes",
    "If a warning has no fix, but a single fix can\n"
    "be found through an associated diagnostic note,\n"
    "apply the fix.\n"
    "Specifying this flag will implicitly enable the\n"
    "'--fix' flag.\n", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_CT_FormatStyle : Option<"format-style",
    "Style for formatting code around applied fixes:\n"
    "  - 'none' (default) turns off formatting\n"
    "  - 'file' (literally 'file', not a placeholder)\n"
    "    uses .clang-format file in the closest parent\n"
    "    directory\n"
    "  - '{ <json> }' specifies options inline, e.g.\n"
    "    -format-style='{BasedOnStyle: llvm, IndentWidth: 8}'\n"
    "  - 'llvm', 'google', 'webkit', 'mozilla'\n"
    "See clang-format documentation for the up-to-date\n"
    "information about formatting styles and options.\n"
    "This option overrides the 'FormatStyle` option in\n"
    ".clang-tidy file, if any.\n", "std::string"> {
  let Registry = CTEOptsReg;
  let Default = "\"none\"";
}

def CTE_CT_ListChecks : Option<"list-checks",
    "List all enabled checks and exit. Use with\n"
    "-checks=* to list all available checks.\n", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_CT_ExplainConfig : Option<"explain-config",
    "For each enabled check explains, where it is\n"
    "enabled, i.e. in clang-tidy binary, command\n"
    "line or a specific configuration file.\n", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_CT_Config : Option<"config",
    "Specifies a configuration in YAML/JSON format:\n"
    "  -config=\"{Checks: '*',\n"
    "            CheckOptions: {x: y}}\"\n"
    "When the value is empty, clang-tidy will\n"
    "attempt to find a file named .clang-tidy for\n"
    "each source file in its parent directories.\n",
    "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_CT_ConfigFile : Option<"config-file",
    "Specify the path of .clang-tidy or custom config file:\n"
    " e.g. --config-file=/some/path/myTidyConfigFile\n"
    "This option internally works exactly the same way as\n"
    " --config option after reading specified config file.\n"
    "Use either --config-file or --config, not both.\n",
    "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_CT_DumpConfig : Option<"dump-config",
    "Dumps configuration in the YAML format to\n"
    "stdout. This option can be used along with a\n"
    "file name (and '--' if the file is outside of a\n"
    "project with configured compilation database).\n"
    "The configuration used for this file will be\n"
    "printed.\n"
    "Use along with -checks=* to include\n"
    "configuration of all checks.\n", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_CT_EnableCheckProfile : Option<"enable-check-profile",
    "Enable per-check timing profiles, and print a\n"
    "report to stderr.\n", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_CT_StoreCheckProfile : Option<"store-check-profile",
    "By default reports are printed in tabulated\n"
    "format to stderr. When this option is passed,\n"
    "these per-TU profiles are instead stored as JSON.\n",
    "std::string"> {
  let Registry = CTEOptsReg;
  let ValueDesc = "prefix";
  let ValueRequired = 1;
}

def CTE_CT_AllowEnablingAnalyzerAlphaCheckers : Option<
    "allow-enabling-analyzer-alpha-checkers", "", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def CTE_CT_EnableModuleHeadersParsing : Option<
    "enable-module-headers-parsing",
    "Enables preprocessor-level module header parsing\n"
    "for C++20 and above, empowering specific checks\n"
    "to detect macro definitions within modules. This\n"
    "feature may cause performance and parsing issues\n"
    "and is therefore considered experimental.\n", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_CT_ExportFixes : Option<"export-fixes",
    "YAML file to store suggested fixes in. The\n"
    "stored fixes can be applied to the input source\n"
    "code with clang-apply-replacements.\n",
    "std::string"> {
  let Registry = CTEOptsReg;
  let ValueDesc = "filename";
  let ValueRequired = 1;
}

def CTE_CT_Quiet : Option<"quiet",
    "Run clang-tidy in quiet mode. This suppresses\n"
    "printing statistics about ignored warnings and\n"
    "warnings treated as errors if the respective\n"
    "options are specified.\n", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_CT_VfsOverlay : Option<"vfsoverlay",
    "Overlay the virtual filesystem described by file\n"
    "over the real file system.\n", "std::string"> {
  let Registry = CTEOptsReg;
  let ValueDesc = "filename";
  let ValueRequired = 1;
}

def CTE_CT_UseColor : Option<"use-color",
    "Use colors in diagnostics. If not set, colors\n"
    "will be used if the terminal connected to\n"
    "standard output supports colors.\n"
    "This option overrides the 'UseColor' option in\n"
    ".clang-tidy file, if any.\n", "bool"> {
  let Registry = CTEOptsReg;
  let GetterName = "getCtUseColor";
  let Default = "false";
}

def CTE_CT_VerifyConfig : Option<"verify-config",
    "Check the config files to ensure each check and\n"
    "option is recognized without running any checks.\n", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_CT_AllowNoChecks : Option<"allow-no-checks",
    "Allow empty enabled checks. This suppresses\n"
    "the \"no checks enabled\" error when disabling\n"
    "all of the checks.\n", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_CT_ExperimentalCustomChecks : Option<
    "experimental-custom-checks",
    "Enable experimental clang-query based\n"
    "custom checks.\n"
    "see https://clang.llvm.org/extra/clang-tidy/QueryBasedCustomChecks.html.\n",
    "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_CT_RemovedArgs : ListOption<"removed-arg",
    "List of arguments to remove from the command\n"
    "line sent to the compiler. Please note that\n"
    "removing arguments might change the semantic\n"
    "of the analyzed code, possibly leading to\n"
    "compiler errors, false positives or\n"
    "false negatives. This option is applied \n"
    "before --extra-arg and --extra-arg-before",
    "std::string"> {
  let Registry = CTEOptsReg;
  let ValueRequired = 1;
}

// =========================================================================
// modularize options
// =========================================================================

def CTE_MOD_Prefix : Option<"prefix",
    "Prepend header file paths with this prefix."
    " If not specified,"
    " the files are considered to be relative to the header list file.",
    "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_MOD_ModuleMapPath : Option<"module-map-path",
    "Turn on module map output and specify output path or file name."
    " If no path is specified and if prefix option is specified,"
    " use prefix for file path.", "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_MOD_ProblemFilesList : Option<"problem-files-list",
    "List of files with compilation or modularization problems for"
    " assistant mode.  This will be excluded.", "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_MOD_RootModule : Option<"root-module",
    "Specify the name of the root module.", "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_MOD_BlockCheckHeaderListOnly : Option<
    "block-check-header-list-only",
    "Only warn if #include directives are inside extern or namespace "
    "blocks if the included header is in the header list.", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_MOD_IncludePaths : ListOption<"I",
    "Include path for coverage check.", "std::string"> {
  let Registry = CTEOptsReg;
  let ValueDesc = "path";
  let ValueRequired = 1;
  let Prefix = 1;  // -Ipath as well as -I path
}

def CTE_MOD_NoCoverageCheck : Option<"no-coverage-check",
    "Don't do the coverage check.", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_MOD_CoverageCheckOnly : Option<"coverage-check-only",
    "Only do the coverage check.", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_MOD_ListFileNames : ListOption<"",
    "<list of one or more header list files>", "std::string"> {
  let Registry = CTEOptsReg;
  let GetterName = "getModListFileNames";
  let Positional = 1;
  let ValueDesc = "list";
  let CommaSeparated = 1;
}

def CTE_MOD_CC1Arguments : ListOption<"",
    "<arguments to be passed to front end>...", "std::string"> {
  let Registry = CTEOptsReg;
  let GetterName = "getModCc1Arguments";
  // Everything after the positional file list goes to the frontend verbatim.
  let ConsumeAfter = 1;
}

def CTE_MOD_DisplayFileLists : Option<"display-file-lists",
    "Display lists of good files (no compile errors), problem files,"
    " and a combined list with problem files preceded by a '#'.", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

// =========================================================================
// clang-change-namespace options
// =========================================================================

def CTE_CN_OldNamespace : Option<"old_namespace",
    "Old namespace.", "std::string"> {
  let Registry = CTEOptsReg;
  let ValueRequired = 1;
  let Required = 1;
}

def CTE_CN_NewNamespace : Option<"new_namespace",
    "New namespace.", "std::string"> {
  let Registry = CTEOptsReg;
  let ValueRequired = 1;
  let Required = 1;
}

def CTE_CN_FilePattern : Option<"file_pattern",
    "Only rename namespaces in files that match the given pattern.",
    "std::string"> {
  let Registry = CTEOptsReg;
  let ValueRequired = 1;
  let Required = 1;
}

def CTE_CN_Inplace : Option<"i",
    "Inplace edit <file>s, if specified.", "bool"> {
  let Registry = CTEOptsReg;
  let GetterName = "getCnInplace";
  let Default = "false";
}

def CTE_CN_DumpResult : Option<"dump_result",
    "Dump new file contents in YAML, if specified.", "bool"> {
  let Registry = CTEOptsReg;
  let GetterName = "getCnDumpResult";
  let Default = "false";
}

def CTE_CN_Style : Option<"style",
    "The style name used for reformatting.",
    "std::string"> {
  let Registry = CTEOptsReg;
  let GetterName = "getCnStyle";
  let Default = "\"LLVM\"";
}

def CTE_CN_AllowedFile : Option<"allowed_file",
    "A file containing regexes of symbol names that are not expected "
    "to be updated when changing namespaces around them.",
    "std::string"> {
  let Registry = CTEOptsReg;
}

// =========================================================================
// clang-include-cleaner options
// =========================================================================

def CTE_IC_Html : Option<"html",
    "Specify an output filename for an HTML report. "
    "This describes both recommendations and reasons for changes.",
    "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_IC_OnlyHeaders : Option<"only-headers",
    "A comma-separated list of regexes to match against suffix of a "
    "header. Only headers that match will be analyzed.",
    "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_IC_IgnoreHeaders : Option<"ignore-headers",
    "A comma-separated list of regexes to match against suffix of a "
    "header, and disable analysis if matched.",
    "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_IC_Print : Option<"print",
    "Print the list of headers to insert and remove",
    "ICPrintStyle"> {
  let Registry = CTEOptsReg;
  let ValueOptional = 1;
  let Default = "ICPrintStyle::Final";
}

def CTE_IC_Edit : Option<"edit",
    "Apply edits to analyzed source files", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_IC_Insert : Option<"insert",
    "Allow header insertions (deprecated. Use -disable-insert instead)",
    "bool"> {
  let Registry = CTEOptsReg;
  let Default = "true";
}

def CTE_IC_Remove : Option<"remove",
    "Allow header removals (deprecated. Use -disable-remove instead)",
    "bool"> {
  let Registry = CTEOptsReg;
  let Default = "true";
}

def CTE_IC_DisableInsert : Option<"disable-insert",
    "Disable header insertions", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_IC_DisableRemove : Option<"disable-remove",
    "Disable header removals", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

// =========================================================================
// clang-move options
// =========================================================================

def CTE_CM_Names : ListOption<"names",
    "The list of the names of classes being moved, "
    "e.g. \"Foo,a::Foo,b::Foo\".", "std::string"> {
  let Registry = CTEOptsReg;
  let ValueRequired = 1;
  let CommaSeparated = 1;
}

def CTE_CM_OldHeader : Option<"old_header",
    "The relative/absolute file path of old header.",
    "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_CM_OldCC : Option<"old_cc",
    "The relative/absolute file path of old cc.",
    "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_CM_NewHeader : Option<"new_header",
    "The relative/absolute file path of new header.",
    "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_CM_NewCC : Option<"new_cc",
    "The relative/absolute file path of new cc.",
    "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_CM_OldDependOnNew : Option<"old_depend_on_new",
    "Whether old header will depend on new header. If true, clang-move will "
    "add #include of new header to old header.", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_CM_NewDependOnOld : Option<"new_depend_on_old",
    "Whether new header will depend on old header. If true, clang-move will "
    "add #include of old header to new header.", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_CM_Style : Option<"style",
    "The style name used for reformatting. Default is \"llvm\"",
    "std::string"> {
  let Registry = CTEOptsReg;
  let GetterName = "getCmStyle";
  let Default = "\"llvm\"";
}

def CTE_CM_DumpResult : Option<"dump_result",
    "Dump results in JSON format to stdout.", "bool"> {
  let Registry = CTEOptsReg;
  let GetterName = "getCmDumpResult";
  let Default = "false";
}

def CTE_CM_DumpDecls : Option<"dump_decls",
    "Dump all declarations in old header (JSON format) to stdout. "
    "If the option is specified, other command options will be ignored. "
    "An empty JSON will be returned if old header isn't specified.",
    "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

// =========================================================================
// clang-reorder-fields options
// =========================================================================

def CTE_RF_RecordName : Option<"record-name",
    "The name of the struct/class.", "std::string"> {
  let Registry = CTEOptsReg;
  let Required = 1;
}

def CTE_RF_FieldsOrder : ListOption<"fields-order",
    "The desired fields order.", "std::string"> {
  let Registry = CTEOptsReg;
  let ValueRequired = 1;
  let CommaSeparated = 1;
  let OneOrMore = 1;
}

def CTE_RF_Inplace : Option<"i",
    "Overwrite edited files.", "bool"> {
  let Registry = CTEOptsReg;
  let GetterName = "getRfInplace";
  let Default = "false";
}

// =========================================================================
// clang-query options
// =========================================================================

def CTE_CQ_UseColor : Option<"use-color",
    "Use colors in detailed AST output. If not set, colors\n"
    "will be used if the terminal connected to\n"
    "standard output supports colors.", "bool"> {
  let Registry = CTEOptsReg;
  let GetterName = "getCqUseColor";
  let Default = "false";
}

def CTE_CQ_Command : ListOption<"c",
    "Specify command to run", "std::string"> {
  let Registry = CTEOptsReg;
  let GetterName = "getCqCommand";
  let ValueDesc = "command";
  let ValueRequired = 1;
}

def CTE_CQ_CommandFile : ListOption<"f",
    "Read commands from file", "std::string"> {
  let Registry = CTEOptsReg;
  let ValueDesc = "file";
  let ValueRequired = 1;
}

def CTE_CQ_Preload : Option<"preload",
    "Preload commands from file and start interactive mode",
    "std::string"> {
  let Registry = CTEOptsReg;
  let ValueDesc = "file";
}

// =========================================================================
// clang-doc options
// =========================================================================

def CTE_CD_Format : Option<"format",
    "Format for outputted docs.", "CTEClangDocFormat"> {
  let Registry = CTEOptsReg;
  let GetterName = "getCdFormat";
  let Default = "yaml";
}

def CTE_CD_ProjectName : Option<"project-name",
    "Name of project.", "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_CD_IgnoreMapErrors : Option<"ignore-map-errors",
    "Continue if files are not mapped correctly.", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "true";
}

def CTE_CD_Output : Option<"output",
    "Directory for outputting generated files.", "std::string"> {
  let Registry = CTEOptsReg;
  let GetterName = "getCdOutput";
  let Default = "\"docs\"";
}

def CTE_CD_Base : Option<"base",
    "Base Directory for generated documentation.\n"
    "URLs will be rooted at this directory for HTML links.",
    "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_CD_Public : Option<"public",
    "Document only public declarations.", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_CD_Doxygen : Option<"doxygen",
    "Use only doxygen-style comments to generate docs.", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_CD_Stylesheets : ListOption<"stylesheets",
    "CSS stylesheets to extend the default styles.", "std::string"> {
  let Registry = CTEOptsReg;
  let ValueRequired = 1;
  let CommaSeparated = 1;
}

def CTE_CD_Asset : Option<"asset",
    "User supplied asset path to override the default css and js files "
    "for html output", "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_CD_SourceRoot : Option<"source-root",
    "\nDirectory where processed files are stored.\n"
    "Links to definition locations will only be\n"
    "generated if the file is in this dir.",
    "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_CD_Repository : Option<"repository",
    "\nURL of repository that hosts code.\n"
    "Used for links to definition locations.",
    "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_CD_RepositoryLinePrefix : Option<"repository-line-prefix",
    "Prefix of line code for repository.", "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_CD_FTimeTrace : Option<"ftime-trace",
    "\nTurn on time profiler. Generates clang-doc-tracing.json",
    "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_CD_PrettyJson : Option<"pretty-json",
    "Serialize JSON with whitespace.", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

// =========================================================================
// clangd main options
// =========================================================================

def CTE_D_CompileArgsFrom : Option<"compile_args_from",
    "The source of compile commands",
    "CTEClangdCompileArgsFrom"> {
  let Registry = CTEOptsReg;
  let Default = "filesystem";
  let Hidden = 1;
}

def CTE_D_CompileCommandsDir : Option<"compile-commands-dir",
    "Specify a path to look for compile_commands.json.",
    "std::string"> {
  let Registry = CTEOptsReg;
  let ValueRequired = 1;
}

def CTE_D_ResourceDir : Option<"resource-dir",
    "Directory for system clang headers", "std::string"> {
  let Registry = CTEOptsReg;
  let Hidden = 1;
}

def CTE_D_QueryDriver : ListOption<"query-driver",
    "Comma separated list of globs for white-listing gcc-compatible "
    "drivers that are safe to execute.", "std::string"> {
  let Registry = CTEOptsReg;
  let GetterName = "getDQueryDriver";
  let ValueRequired = 1;
  let CommaSeparated = 1;
}

def CTE_D_AllScopesCompletion : Option<"all-scopes-completion",
    "If set to true, code completion will include index symbols that are "
    "not defined in the scopes visible from the code completion point.",
    "bool"> {
  let Registry = CTEOptsReg;
  let Default = "true";
}

def CTE_D_DebugOrigin : Option<"debug-origin",
    "Show origins of completion items", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def CTE_D_BackgroundIndex : Option<"background-index",
    "Index project code in the background and persist index on disk.",
    "bool"> {
  let Registry = CTEOptsReg;
  let Default = "true";
}

def CTE_D_BackgroundIndexPriority : Option<"background-index-priority",
    "Thread priority for building the background index.",
    "CTEClangdBGIndexPriority"> {
  let Registry = CTEOptsReg;
  let Default = "low";
}

def CTE_D_ClangTidy : Option<"clang-tidy",
    "Enable clang-tidy diagnostics", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "true";
}

def CTE_D_CompletionParse : Option<"completion-parse",
    "Whether the clang-parser is used for code-completion",
    "CTEClangdCompletionParse"> {
  let Registry = CTEOptsReg;
  let Default = "always";
  let Hidden = 1;
}

def CTE_D_RankingModel : Option<"ranking-model",
    "Model to use to rank code-completion items",
    "CTEClangdRankingModel"> {
  let Registry = CTEOptsReg;
  let Default = "decision_forest";
  let Hidden = 1;
}

def CTE_D_CompletionStyle : Option<"completion-style",
    "Granularity of code completion suggestions",
    "CTEClangdCompletionStyle"> {
  let Registry = CTEOptsReg;
  let Default = "detailed";
}

def CTE_D_FallbackStyle : Option<"fallback-style",
    "clang-format style to apply by default when no .clang-format file "
    "is found", "std::string"> {
  let Registry = CTEOptsReg;
  let ValueRequired = 1;
}

def CTE_D_FunctionArgPlaceholders : Option<"function-arg-placeholders",
    "When disabled (0), completions contain only parentheses for function "
    "calls. When enabled (1), completions also contain placeholders for "
    "method parameters", "std::string"> {
  let Registry = CTEOptsReg;
  let ValueRequired = 1;
}

def CTE_D_HeaderInsertion : Option<"header-insertion",
    "Add #include directives when accepting code completions",
    "CTEClangdHeaderInsertion"> {
  let Registry = CTEOptsReg;
  let Default = "iwyu";
}

def CTE_D_ImportInsertions : Option<"import-insertions",
    "If header insertion is enabled, add #import directives when accepting "
    "code completions or fixing includes in Objective-C code", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_D_HeaderInsertionDecorators : Option<
    "header-insertion-decorators",
    "Prepend a circular dot or space before the completion label",
    "bool"> {
  let Registry = CTEOptsReg;
  let Default = "true";
}

def CTE_D_HiddenFeatures : Option<"hidden-features",
    "Enable hidden features mostly useful to clangd developers",
    "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def CTE_D_IncludeIneligibleResults : Option<
    "include-ineligible-results",
    "Include ineligible completion results (e.g. private members)",
    "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def CTE_D_LimitResults : Option<"limit-results",
    "Limit the number of results returned by clangd. 0 means no limit",
    "int"> {
  let Registry = CTEOptsReg;
  let GetterName = "getDLimitResults";
  let Default = "100";
}

def CTE_D_LimitReferences : Option<"limit-references",
    "Limit the number of references returned by clangd. 0 means no limit",
    "int"> {
  let Registry = CTEOptsReg;
  let Default = "1000";
}

def CTE_D_RenameFileLimit : Option<"rename-file-limit",
    "Limit the number of files to be affected by symbol renaming. "
    "0 means no limit", "int"> {
  let Registry = CTEOptsReg;
  let Default = "50";
}

def CTE_D_Tweaks : ListOption<"tweaks",
    "Specify a list of Tweaks to enable (only for clangd developers).",
    "std::string"> {
  let Registry = CTEOptsReg;
  let Hidden = 1;
  let ValueRequired = 1;
  let CommaSeparated = 1;
}

def CTE_D_WorkerThreads : Option<"j",
    "Number of async workers used by clangd. Background index also "
    "uses this many workers.", "unsigned"> {
  let Registry = CTEOptsReg;
  let GetterName = "getDWorkerThreads";
  let Default = "0u";
}

def CTE_D_IndexFile : Option<"index-file",
    "Index file to build the static index.", "std::string"> {
  let Registry = CTEOptsReg;
  let Hidden = 1;
}

def CTE_D_LitTest : Option<"lit-test",
    "Abbreviation for -input-style=delimited -pretty -sync "
    "-enable-test-scheme -enable-config=0 -log=verbose -crash-pragmas.",
    "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def CTE_D_CrashPragmas : Option<"crash-pragmas",
    "Respect `#pragma clang __debug crash` and friends.", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def CTE_D_Check : Option<"check",
    "Parse one file in isolation instead of acting as a language server.",
    "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_D_PCHStorage : Option<"pch-storage",
    "Storing PCHs in memory increases memory usages, but may improve "
    "performance", "CTEClangdPCHStorage"> {
  let Registry = CTEOptsReg;
  let Default = "disk";
}

def CTE_D_Sync : Option<"sync",
    "Handle client requests on main thread.", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def CTE_D_InputStyle : Option<"input-style",
    "Input JSON stream encoding", "CTEClangdInputStyle"> {
  let Registry = CTEOptsReg;
  let Default = "standard";
  let Hidden = 1;
}

def CTE_D_EnableTestScheme : Option<"enable-test-uri-scheme",
    "Enable 'test:' URI scheme. Only use in lit tests", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def CTE_D_PathMappings : Option<"path-mappings",
    "Translates between client paths and server paths. "
    "Comma separated list of '<client_path>=<server_path>' pairs.",
    "std::string"> {
  let Registry = CTEOptsReg;
  let GetterName = "getDPathMappings";
  let ValueRequired = 1;
}

def CTE_D_InputMirrorFile : Option<"input-mirror-file",
    "Mirror all LSP input to the specified file.", "std::string"> {
  let Registry = CTEOptsReg;
  let Hidden = 1;
}

def CTE_D_Log : Option<"log",
    "Verbosity of log messages written to stderr",
    "CTEClangdLogLevel"> {
  let Registry = CTEOptsReg;
  let GetterName = "getDLog";
  let Default = "info";
}

def CTE_D_OffsetEncoding : Option<"offset-encoding",
    "Force the offsetEncoding used for character positions.",
    "CTEClangdOffsetEncoding"> {
  let Registry = CTEOptsReg;
  let Default = "CTEClangdOffsetEncoding::Unsupported";
}

def CTE_D_PrettyPrint : Option<"pretty",
    "Pretty-print JSON output", "bool"> {
  let Registry = CTEOptsReg;
  let GetterName = "getDPrettyPrint";
  let Default = "false";
}

def CTE_D_EnableConfig : Option<"enable-config",
    "Read user and project configuration from YAML files.", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "true";
}

def CTE_D_StrongWorkspaceMode : Option<"strong-workspace-mode",
    "An alternate mode of operation for clangd.", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def CTE_D_UseDirtyHeaders : Option<"use-dirty-headers",
    "Use files open in the editor when parsing headers instead of reading "
    "from the disk", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def CTE_D_ParseForwardingFunctions : Option<"parse-forwarding-functions",
    "Parse all emplace-like functions in included headers", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def CTE_D_SkipPreambleBuild : Option<"skip-preamble-build",
    "If true, skip preamble build", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def CTE_D_ExperimentalModulesSupport : Option<
    "experimental-modules-support",
    "Experimental support for standard c++ modules", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

// =========================================================================
// clangd --check sub-options
// =========================================================================

def CTE_DC_CheckTidyTime : Option<"check-tidy-time",
    "Print the overhead of checks matching this glob",
    "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_DC_CheckLines : Option<"check-lines",
    "Limits the range of tokens in -check file on which various "
    "features are tested. Example --check-lines=3-7.",
    "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_DC_CheckLocations : Option<"check-locations",
    "Runs certain features (e.g. hover) at each point in the file.",
    "bool"> {
  let Registry = CTEOptsReg;
  let Default = "true";
}

def CTE_DC_CheckCompletion : Option<"check-completion",
    "Run code-completion at each point (slow)", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_DC_CheckWarnings : Option<"check-warnings",
    "Print warnings as well as errors", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

// =========================================================================
// clangd ModulesBuilder options
// =========================================================================

def CTE_DM_DebugModulesBuilder : Option<"debug-modules-builder",
    "Don't remove clangd's built module files for debugging.", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_DM_VersionedGCThreshold : Option<
    "modules-builder-versioned-gc-threshold-seconds",
    "Delete versioned copy-on-read module files whose last access time "
    "is older than this many seconds.", "unsigned"> {
  let Registry = CTEOptsReg;
  let Default = "static_cast<unsigned>(3 * 24 * 60 * 60)";
}

// =========================================================================
// clangd-remap options
// =========================================================================

def CTE_DR_PathMappings : Option<"path-mappings",
    "List of path mappings applied to every string in each background "
    "index shard. Format: /old/path=/new/path[,/old2=/new2,...]",
    "std::string"> {
  let Registry = CTEOptsReg;
  let GetterName = "getDrPathMappings";
}

def CTE_DR_NumThreads : Option<"j",
    "Number of worker threads (0 = all)", "unsigned"> {
  let Registry = CTEOptsReg;
  let GetterName = "getDrNumThreads";
  let Default = "0u";
  let Prefix = 1;  // -j8 as well as -j 8
}

def CTE_DR_Log : Option<"log",
    "Verbosity of log messages written to stderr",
    "CTEClangdLogLevel"> {
  let Registry = CTEOptsReg;
  let GetterName = "getDrLog";
  let Default = "info";
}

// =========================================================================
// clangd-indexer options
// =========================================================================

def CTE_DI_Format : Option<"format",
    "Format of the index to be written",
    "CTEIndexerFormat"> {
  let Registry = CTEOptsReg;
  let GetterName = "getDiFormat";
  let Default = "binary";
}

def CTE_DI_QueryDriver : ListOption<"query-driver",
    "Comma separated list of globs for white-listing gcc-compatible "
    "drivers that are safe to execute.", "std::string"> {
  let Registry = CTEOptsReg;
  let GetterName = "getDiQueryDriver";
  let ValueRequired = 1;
  let CommaSeparated = 1;
}

// =========================================================================
// dexp options
// =========================================================================

def CTE_DX_ExecCommand : Option<"c",
    "Command to execute and then exit.", "std::string"> {
  let Registry = CTEOptsReg;
  let GetterName = "getDxExecCommand";
}

def CTE_DX_ProjectRoot : Option<"project-root",
    "Path to the project. Required when connecting using remote index.",
    "std::string"> {
  let Registry = CTEOptsReg;
}

// =========================================================================
// clangd remote server options
// =========================================================================

def CTE_DS_Log : Option<"log",
    "Verbosity of log messages written to stderr",
    "CTEClangdLogLevel"> {
  let Registry = CTEOptsReg;
  let GetterName = "getDsLog";
  let Default = "info";
}

def CTE_DS_LogPublic : Option<"log-public",
    "Avoid logging potentially-sensitive request details", "bool"> {
  let Registry = CTEOptsReg;
  let Default = "false";
}

def CTE_DS_LogPrefix : Option<"log-prefix",
    "A string that'll be prepended to all log statements.",
    "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_DS_TraceFile : Option<"trace-file",
    "Path to the file where tracer logs will be stored",
    "std::string"> {
  let Registry = CTEOptsReg;
}

def CTE_DS_PrettyPrint : Option<"pretty",
    "Pretty-print JSON output in the trace", "bool"> {
  let Registry = CTEOptsReg;
  let GetterName = "getDsPrettyPrint";
  let Default = "false";
}

def CTE_DS_ServerAddress : Option<"server-address",
    "Address of the invoked server. Defaults to 0.0.0.0:50051",
    "std::string"> {
  let Registry = CTEOptsReg;
  let Default = "\"0.0.0.0:50051\"";
}

def CTE_DS_IdleTimeout : Option<"idle-timeout",
    "Maximum time a channel may stay idle until server closes "
    "the connection, in seconds.", "unsigned"> {
  let Registry = CTEOptsReg;
  let Default = "static_cast<unsigned>(8 * 60)";
}

def CTE_DS_LimitResults : Option<"limit-results",
    "Maximum number of results to stream as a response to "
    "single request.", "unsigned"> {
  let Registry = CTEOptsReg;
  let GetterName = "getDsLimitResults";
  let Default = "10000u";
}
