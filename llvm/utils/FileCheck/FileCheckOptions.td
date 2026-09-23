//===- FileCheckOptions.td - Option defs for the FileCheck tool -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def FileCheckOptions : OptionsStruct<"llvm">,
    PositionalField<"std::string", "CheckFilename"> {}

// =========================================================================
// -dump-input / -dump-input-filter enums
//
// Member order matters: it determines precedence (highest ordinal wins) for
// -dump-input and -dump-input-filter, each of which may be specified more
// than once.
// =========================================================================

def DumpInputValueEnum : OptionEnum<"DumpInputValue", [
    EnumMember<"Never", "never">,
    EnumMember<"Fail", "fail">,
    EnumMember<"Always", "always">,
    EnumMember<"Help", "help">]>;

def DumpInputFilterValueEnum : OptionEnum<"DumpInputFilterValue", [
    EnumMember<"Error", "error">,
    EnumMember<"Annotation", "annotation">,
    EnumMember<"AnnotationFull", "annotation-full">,
    EnumMember<"All", "all">]>;

// =========================================================================
// Options
// =========================================================================

defm InputFilename : ValueField<"input-file", "std::string", "\"-\"",
    "File to check (defaults to stdin)", /*hidden=*/0, /*metaVar=*/"filename">;

defm CheckPrefixes : ListField<"check-prefixes", "std::string",
    "Comma separated list of prefixes to use from check "
    "file\n(defaults to 'CHECK')">;

def CheckPrefix : Joined<["-", "--"], "check-prefix=">,
    Alias<CheckPrefixes>, HelpText<"Alias for -check-prefixes">;
def CheckPrefix_sep : Separate<["-", "--"], "check-prefix">,
    Flags<[HelpHidden]>, Alias<CheckPrefixes>;

defm CommentPrefixes : ListField<"comment-prefixes", "std::string",
    "Comma-separated list of comment prefixes to use from check file\n"
    "(defaults to 'COM,RUN'). Please avoid using this feature in\n"
    "LLVM's LIT-based test suites, which should be easier to\n"
    "maintain if they all follow a consistent comment style. This\n"
    "feature is meant for non-LIT test suites using FileCheck.",
    /*commaJoined=*/1, /*appendEquals=*/1, /*hidden=*/1>;

defm NoCanonicalizeWhiteSpace : BoolField<"strict-whitespace", "false",
    "Do not treat all horizontal whitespace as equivalent">;

defm IgnoreCase : BoolField<"ignore-case", "false",
    "Use case-insensitive matching">;

defm ImplicitCheckNot : ListField<"implicit-check-not", "std::string",
    "Add an implicit negative check with this pattern to every\n"
    "positive check. This can be used to ensure that no instances of\n"
    "this pattern occur which are not matched by a positive pattern",
    /*commaJoined=*/0, /*appendEquals=*/1, /*hidden=*/0, /*metaVar=*/"pattern">;

defm GlobalDefines : ListField<"D", "std::string",
    "Define a variable to be used in capture patterns.",
    /*commaJoined=*/0, /*appendEquals=*/0, /*hidden=*/0,
    /*metaVar=*/"VAR=VALUE">;

defm AllowEmptyInput : BoolField<"allow-empty", "false",
    "Allow the input file to be empty. This is useful when making\n"
    "checks that some error message does not occur, for example.">;

defm AllowUnusedPrefixes : BoolField<"allow-unused-prefixes", "false",
    "Allow prefixes to be specified but not appear in the test.">;

defm MatchFullLines : BoolField<"match-full-lines", "false",
    "Require all positive matches to cover an entire input line.\n"
    "Allows leading and trailing whitespace if --strict-whitespace\n"
    "is not also passed.">;

defm EnableVarScope : BoolField<"enable-var-scope", "false",
    "Enables scope for regex variables. Variables with names that\n"
    "do not start with '$' will be reset at the beginning of\n"
    "each CHECK-LABEL block.">;

defm AllowDeprecatedDagOverlap : BoolField<"allow-deprecated-dag-overlap",
    "false",
    "Enable overlapping among matches in a group of consecutive\n"
    "CHECK-DAG directives.  This option is deprecated and is only\n"
    "provided for convenience as old tests are migrated to the new\n"
    "non-overlapping CHECK-DAG implementation.\n">;

defm Verbose : BoolField<"v", "false",
    "Print directive pattern matches, or add them to the input dump\n"
    "if enabled.\n">;

defm VerboseVerbose : BoolField<"vv", "false",
    "Print information helpful in diagnosing internal FileCheck\n"
    "issues, or add it to the input dump if enabled.  Implies\n"
    "-v.\n">;

defm DumpInputs : EnumListField<"dump-input", DumpInputValueEnum,
    "Dump input to stderr, adding annotations representing\n"
    "currently enabled diagnostics.  When there are multiple\n"
    "occurrences of this option, the <value> that appears earliest\n"
    "in the list below has precedence.  The default is 'fail'.\n"
    "  =help    - Explain input dump and quit\n"
    "  =always  - Always dump input\n"
    "  =fail    - Dump input on failure\n"
    "  =never   - Never dump input\n">;

defm DumpInputFilters : EnumListField<"dump-input-filter",
    DumpInputFilterValueEnum,
    "In the dump requested by -dump-input, print only input lines of\n"
    "kind <value> plus any context specified by -dump-input-context.\n"
    "When there are multiple occurrences of this option, the <value>\n"
    "that appears earliest in the list below has precedence.  The\n"
    "default is 'error' when -dump-input=fail, and it's 'all' when\n"
    "-dump-input=always.\n"
    "  =all              - All input lines\n"
    "  =annotation-full  - Input lines with annotations\n"
    "  =annotation       - Input lines with starting points of annotations\n"
    "  =error            - Input lines with starting points of error "
    "annotations\n">;

defm DumpInputContexts : ListField<"dump-input-context", "unsigned",
    "In the dump requested by -dump-input, print <N> input lines\n"
    "before and <N> input lines after any lines specified by\n"
    "-dump-input-filter.  When there are multiple occurrences of\n"
    "this option, the largest specified <N> has precedence.  The\n"
    "default is 5.\n",
    /*commaJoined=*/0, /*appendEquals=*/1, /*hidden=*/0, /*metaVar=*/"N">;

defm DumpInputLabelWidth : ValueField<"dump-input-label-width", "unsigned",
    "0",
    "In the dump requested by -dump-input, set <N> as the minimum "
    "width for the initial label column.  When there are multiple "
    "occurrences of this option, the last specified has precedence. "
    "The default is 0, meaning that the actual labels fully "
    "determine the width.  FileCheck's own test suite uses this "
    "option to avoid a fluctuating column width when checking input "
    "dumps.  This option is not expected to be useful elsewhere.",
    /*hidden=*/1, /*metaVar=*/"N">;
