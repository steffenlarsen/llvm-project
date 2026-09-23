//===- IPOOptions.td - Option defs for the IPO library -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def IPOOptions : OptionsStruct<"llvm"> {}

// =========================================================================
// External enum: llvm::ExpandVariadicsMode (from ExpandVariadicsMode.h)
// =========================================================================

def ExpandVariadicsModeEnum : OptionEnum<"llvm::ExpandVariadicsMode", [
    EnumMember<"Unspecified", "unspecified">,
    EnumMember<"Disable", "disable">,
    EnumMember<"Optimize", "optimize">,
    EnumMember<"Lowering", "lowering">]> {
  let External = 1;
  let Ident = "ExpandVariadicsMode";
}

// =========================================================================
// External enum: llvm::ReplayInlinerScope (from ReplayInlinerEnums.h)
// =========================================================================

def ReplayInlinerScopeEnum : OptionEnum<"llvm::ReplayInlinerScope", [
    EnumMember<"Function", "Function">,
    EnumMember<"Module", "Module">]> {
  let External = 1;
  let Ident = "ReplayInlinerScope";
}

// =========================================================================
// External enum: llvm::ReplayInlinerFallback (from ReplayInlinerEnums.h)
// =========================================================================

def ReplayInlinerFallbackEnum : OptionEnum<"llvm::ReplayInlinerFallback", [
    EnumMember<"Original", "Original">,
    EnumMember<"AlwaysInline", "AlwaysInline">,
    EnumMember<"NeverInline", "NeverInline">]> {
  let External = 1;
  let Ident = "ReplayInlinerFallback";
}

// =========================================================================
// External enum: llvm::CallSiteFormatKind (from ReplayInlinerEnums.h)
// =========================================================================

def CallSiteFormatKindEnum : OptionEnum<"llvm::CallSiteFormatKind", [
    EnumMember<"Line", "Line">,
    EnumMember<"LineColumn", "LineColumn">,
    EnumMember<"LineDiscriminator", "LineDiscriminator">,
    EnumMember<"LineColumnDiscriminator", "LineColumnDiscriminator">]> {
  let External = 1;
  let Ident = "CallSiteFormatKind";
}

// =========================================================================
// External enum: llvm::PassSummaryAction (from Transforms/IPO.h)
// =========================================================================

def PassSummaryActionEnum : OptionEnum<"llvm::PassSummaryAction", [
    EnumMember<"None", "none">,
    EnumMember<"Import", "import">,
    EnumMember<"Export", "export">]> {
  let External = 1;
  let Ident = "PassSummaryAction";
}

// =========================================================================
// Inline enum: DotScope (defined in MemProfContextDisambiguation.cpp)
// =========================================================================

def DotScopeEnum : OptionEnum<"DotScope", [
    EnumMember<"All", "all">,
    EnumMember<"Alloc", "alloc">,
    EnumMember<"Context", "context">]>;

// =========================================================================
// Inline enum: WPDCheckMode (defined in WholeProgramDevirt.cpp)
// =========================================================================

def WPDCheckModeEnum : OptionEnum<"WPDCheckMode", [
    EnumMember<"None", "none">,
    EnumMember<"Trap", "trap">,
    EnumMember<"Fallback", "fallback">]>;


// =========================================================================
// Options -- Attributor.cpp
// =========================================================================

defm IPO_SetFixpointIterations : ValueField<
    "attributor-max-iterations", "unsigned", "32",
    "Maximal number of fixpoint iterations.", /*hidden=*/1>;

defm IPO_MaxSpecializationPerCB : OptionalField<
    "attributor-max-specializations-per-call-base", "unsigned",
    "Maximal number of callees specialized for a call base", /*hidden=*/1>;

defm IPO_MaxInitializationChainLengthX : ValueField<
    "attributor-max-initialization-chain-length", "unsigned", "1024",
    "Maximal number of chained initializations (to avoid stack overflows)", /*hidden=*/1>;

defm IPO_AnnotateDeclarationCallSites : BoolField<
    "attributor-annotate-decl-cs", "false",
    "Annotate call sites of function declarations.", /*hidden=*/1>;

defm IPO_EnableHeapToStack : BoolField<
    "enable-heap-to-stack-conversion", "true",
    "", /*hidden=*/1>;

defm IPO_AllowShallowWrappers : BoolField<
    "attributor-allow-shallow-wrappers", "false",
    "Allow the Attributor to create shallow wrappers for non-exact definitions.", /*hidden=*/1>;

defm IPO_AllowDeepWrapper : BoolField<
    "attributor-allow-deep-wrappers", "false",
    "Allow the Attributor to use IP information derived from non-exact functions via cloning", /*hidden=*/1>;

defm IPO_SeedAllowList : ListField<
    "attributor-seed-allow-list", "std::string",
    "Comma separated list of attribute names that are allowed to be seeded.", /*commaJoined=*/1, /*appendEquals=*/1, /*hidden=*/1>;

defm IPO_FunctionSeedAllowList : ListField<
    "attributor-function-seed-allow-list", "std::string",
    "Comma separated list of function names that are allowed to be seeded.", /*commaJoined=*/1, /*appendEquals=*/1, /*hidden=*/1>;

defm IPO_DumpDepGraph : BoolField<
    "attributor-dump-dep-graph", "false",
    "Dump the dependency graph to dot files.", /*hidden=*/1>;

defm IPO_DepGraphDotFileNamePrefix : ValueField<
    "attributor-depgraph-dot-filename-prefix", "std::string", "\"\"",
    "The prefix used for the CallGraph dot file names.", /*hidden=*/1>;

defm IPO_ViewDepGraph : BoolField<
    "attributor-view-dep-graph", "false",
    "View the dependency graph.", /*hidden=*/1>;

defm IPO_PrintDependencies : BoolField<
    "attributor-print-dep", "false",
    "Print attribute dependencies", /*hidden=*/1>;

defm IPO_EnableCallSiteSpecific : BoolField<
    "attributor-enable-call-site-specific-deduction", "false",
    "Allow the Attributor to do call site specific analysis", /*hidden=*/1>;

defm IPO_PrintCallGraph : BoolField<
    "attributor-print-call-graph", "false",
    "Print Attributor's internal call graph", /*hidden=*/1>;

defm IPO_SimplifyAllLoads : BoolField<
    "attributor-simplify-all-loads", "true",
    "Try to simplify all loads.", /*hidden=*/1>;

def IPO_CloseWorldAssumption : Flag<["-", "--"], "attributor-assume-closed-world">, HelpText<"Should a closed world be assumed, or not. Default if not set.">,
    Flags<[HelpHidden]>,
    Field<"std::optional<bool>", "IPO_CloseWorldAssumption", "std::nullopt">,
    Assign<"IPO_CloseWorldAssumption", "true">;
def IPO_CloseWorldAssumption_no : Flag<["-", "--"], "no-attributor-assume-closed-world">, Flags<[HelpHidden]>,
    Assign<"IPO_CloseWorldAssumption", "false">;
def IPO_CloseWorldAssumption_eq : Joined<["-", "--"], "attributor-assume-closed-world=">, Flags<[HelpHidden]>,
    BoolAssign<"IPO_CloseWorldAssumption">;


// =========================================================================
// Options -- AttributorAttributes.cpp
// =========================================================================

defm IPO_ManifestInternal : BoolField<
    "attributor-manifest-internal", "false",
    "Manifest Attributor internal string attributes.", /*hidden=*/1>;

defm IPO_MaxHeapToStackSize : ValueField<
    "max-heap-to-stack-size", "int", "128",
    "", /*hidden=*/1>;

defm IPO_MaxPotentialValues : ValueField<
    "attributor-max-potential-values", "unsigned", "7",
    "Maximum number of potential values to be tracked for each position.", /*hidden=*/1>;

defm IPO_MaxPotentialValuesIterations : ValueField<
    "attributor-max-potential-values-iterations", "int", "64",
    "Maximum number of iterations we keep dismantling potential values.", /*hidden=*/1>;


// =========================================================================
// Options -- BlockExtractor.cpp
// =========================================================================

defm IPO_BlockExtractorFile : ValueField<
    "extract-blocks-file", "std::string", "\"\"",
    "A file containing list of basic blocks to extract", /*hidden=*/1, /*metaVar=*/"filename">;

defm IPO_BlockExtractorEraseFuncs : BoolField<
    "extract-blocks-erase-funcs", "false",
    "Erase the existing functions", /*hidden=*/1>;


// =========================================================================
// Options -- CalledValuePropagation.cpp
// =========================================================================

defm IPO_MaxFunctionsPerValue : ValueField<
    "cvp-max-functions-per-value", "unsigned", "4",
    "The maximum number of functions to track per lattice value", /*hidden=*/1>;


// =========================================================================
// Options -- ElimAvailExtern.cpp
// =========================================================================

defm IPO_ConvertToLocal : BoolField<
    "avail-extern-to-local", "false",
    "Convert available_externally into locals, renaming them to avoid link-time clashes.", /*hidden=*/1>;

defm IPO_ConvertGlobalVariableInAddrSpace : OptionalField<
    "avail-extern-gv-in-addrspace-to-local", "unsigned",
    "Convert available_externally global variables into locals if they are in specificed addrspace, renaming them to avoid link-time clashes.", /*hidden=*/1>;


// =========================================================================
// Options -- ExpandVariadics.cpp
// =========================================================================

defm IPO_ExpandVariadicsModeOption : EnumField<
    "expand-variadics-override", ExpandVariadicsModeEnum, "llvm::ExpandVariadicsMode::Unspecified",
    "Override the behaviour of expand-variadics">;


// =========================================================================
// Options -- ForceFunctionAttrs.cpp
// =========================================================================

defm IPO_ForceAttributes : ListField<
    "force-attribute", "std::string",
    "Add an attribute to a function. This can be a pair of 'function-name:attribute-name', to apply an attribute to a specific function. For example -force-attribute=foo:noinline. Specifying only an attribute will apply the attribute to every function in the module. This option can be specified multiple times.", /*commaJoined=*/0, /*appendEquals=*/1, /*hidden=*/1>;

defm IPO_ForceRemoveAttributes : ListField<
    "force-remove-attribute", "std::string",
    "Remove an attribute from a function. This can be a pair of 'function-name:attribute-name' to remove an attribute from a specific function. For example -force-remove-attribute=foo:noinline. Specifying only an attribute will remove the attribute from all functions in the module. This option can be specified multiple times.", /*commaJoined=*/0, /*appendEquals=*/1, /*hidden=*/1>;

defm IPO_CSVFilePath : ValueField<
    "forceattrs-csv-path", "std::string", "\"\"",
    "Path to CSV file containing lines of function names and attributes to add to them in the form of `f1,attr1` or `f2,attr2=str`.", /*hidden=*/1>;


// =========================================================================
// Options -- FunctionAttrs.cpp
// =========================================================================

defm IPO_EnablePoisonArgAttrPropagation : BoolField<
    "enable-poison-arg-attr-prop", "true",
    "Try to propagate nonnull and nofpclass argument attributes from callsites to caller functions.", /*hidden=*/1>;

defm IPO_DisableNoUnwindInference : BoolField<
    "disable-nounwind-inference", "false",
    "Stop inferring nounwind attribute during function-attrs pass", /*hidden=*/1>;

defm IPO_DisableNoFreeInference : BoolField<
    "disable-nofree-inference", "false",
    "Stop inferring nofree attribute during function-attrs pass", /*hidden=*/1>;

defm IPO_DisableThinLTOPropagation : BoolField<
    "disable-thinlto-funcattrs", "true",
    "Don't propagate function-attrs in thinLTO", /*hidden=*/1>;


// =========================================================================
// Options -- FunctionImport.cpp
// =========================================================================

defm IPO_ForceImportAll : BoolField<
    "force-import-all", "false",
    "Import functions with noinline attribute", /*hidden=*/1>;

defm IPO_ImportInstrLimit : ValueField<
    "import-instr-limit", "unsigned", "100",
    "Only import functions with less than N instructions", /*hidden=*/1, /*metaVar=*/"N">;

defm IPO_ImportCutoff : ValueField<
    "import-cutoff", "int", "-1",
    "Only import first N functions if N>=0 (default -1)", /*hidden=*/1, /*metaVar=*/"N">;

defm IPO_ImportInstrFactor : ValueField<
    "import-instr-evolution-factor", "float", "0.7",
    "As we import functions, multiply the `import-instr-limit` threshold by this factor before processing newly imported functions", /*hidden=*/1, /*metaVar=*/"x">;

defm IPO_ImportHotInstrFactor : ValueField<
    "import-hot-evolution-factor", "float", "1.0",
    "As we import functions called from hot callsite, multiply the `import-instr-limit` threshold by this factor before processing newly imported functions", /*hidden=*/1, /*metaVar=*/"x">;

defm IPO_ImportHotMultiplier : ValueField<
    "import-hot-multiplier", "float", "10.0",
    "Multiply the `import-instr-limit` threshold for hot callsites", /*hidden=*/1, /*metaVar=*/"x">;

defm IPO_ImportCriticalMultiplier : ValueField<
    "import-critical-multiplier", "float", "100.0",
    "Multiply the `import-instr-limit` threshold for critical callsites", /*hidden=*/1, /*metaVar=*/"x">;

defm IPO_ImportColdMultiplier : ValueField<
    "import-cold-multiplier", "float", "0",
    "Multiply the `import-instr-limit` threshold for cold callsites", /*hidden=*/1, /*metaVar=*/"N">;

defm IPO_PrintImports : BoolField<
    "print-imports", "false",
    "Print imported functions", /*hidden=*/1>;

defm IPO_PrintImportFailures : BoolField<
    "print-import-failures", "false",
    "Print information for functions rejected for importing", /*hidden=*/1>;

defm IPO_ComputeDead : BoolField<
    "compute-dead", "true",
    "Compute dead symbols", /*hidden=*/1>;

defm IPO_EnableImportMetadata : BoolField<
    "enable-import-metadata", "false",
    "Enable import metadata like 'thinlto_src_module' and 'thinlto_src_file'", /*hidden=*/1>;

defm IPO_SummaryFile : ValueField<
    "summary-file", "std::string", "\"\"",
    "The summary file to use for function importing.">;

defm IPO_ImportAllIndex : BoolField<
    "import-all-index", "false",
    "Import all external functions in index.">;

defm IPO_ImportDeclaration : BoolField<
    "import-declaration", "false",
    "If true, import function declaration as fallback if the function definition is not imported.", /*hidden=*/1>;

defm IPO_WorkloadDefinitions : ValueField<
    "thinlto-workload-def", "std::string", "\"\"",
    "Pass a workload definition. This is a file containing a JSON dictionary. The keys are root functions, the values are lists of functions to import in the module defining the root. It is assumed -funique-internal-linkage-names was used, to ensure local linkage functions have unique names.", /*hidden=*/1>;

defm IPO_CtxprofMoveRootsToOwnModule : BoolField<
    "thinlto-move-ctxprof-trees", "false",
    "Move contextual profiling roots and the graphs under them in their own module.", /*hidden=*/1>;


// =========================================================================
// Options -- FunctionSpecialization.cpp
// =========================================================================

defm IPO_ForceSpecialization : BoolField<
    "force-specialization", "false",
    "Force function specialization for every call site with a constant argument", /*hidden=*/1>;

defm IPO_MaxClones : ValueField<
    "funcspec-max-clones", "unsigned", "3",
    "The maximum number of clones allowed for a single function specialization", /*hidden=*/1>;

defm IPO_MaxDiscoveryIterations : ValueField<
    "funcspec-max-discovery-iterations", "unsigned", "100",
    "The maximum number of iterations allowed when searching for transitive phis", /*hidden=*/1>;

defm IPO_MaxIncomingPhiValues : ValueField<
    "funcspec-max-incoming-phi-values", "unsigned", "8",
    "The maximum number of incoming values a PHI node can have to be considered during the specialization bonus estimation", /*hidden=*/1>;

defm IPO_MaxBlockPredecessors : ValueField<
    "funcspec-max-block-predecessors", "unsigned", "2",
    "The maximum number of predecessors a basic block can have to be considered during the estimation of dead code", /*hidden=*/1>;

defm IPO_MinFunctionSize : ValueField<
    "funcspec-min-function-size", "unsigned", "500",
    "Don't specialize functions that have less than this number of instructions", /*hidden=*/1>;

defm IPO_MaxCodeSizeGrowth : ValueField<
    "funcspec-max-codesize-growth", "unsigned", "3",
    "Maximum codesize growth allowed per function", /*hidden=*/1>;

defm IPO_MinCodeSizeSavings : ValueField<
    "funcspec-min-codesize-savings", "unsigned", "20",
    "Reject specializations whose codesize savings are less than this much percent of the original function size", /*hidden=*/1>;

defm IPO_MinLatencySavings : ValueField<
    "funcspec-min-latency-savings", "unsigned", "20",
    "Reject specializations whose latency savings are less than this much percent of the original function size", /*hidden=*/1>;

defm IPO_MinInliningBonus : ValueField<
    "funcspec-min-inlining-bonus", "unsigned", "300",
    "Reject specializations whose inlining bonus is less than this much percent of the original function size", /*hidden=*/1>;

defm IPO_SpecializeOnAddress : BoolField<
    "funcspec-on-address", "false",
    "Enable function specialization on the address of global values", /*hidden=*/1>;

defm IPO_SpecializeLiteralConstant : BoolField<
    "funcspec-for-literal-constant", "true",
    "Enable specialization of functions that take a literal constant as an argument", /*hidden=*/1>;


// =========================================================================
// Options -- GlobalDCE.cpp
// =========================================================================

defm IPO_ClEnableVFE : BoolField<
    "enable-vfe", "true",
    "Enable virtual function elimination", /*hidden=*/1>;


// =========================================================================
// Options -- GlobalOpt.cpp
// =========================================================================

defm IPO_OptimizeNonFMVCallers : BoolField<
    "optimize-non-fmv-callers", "true",
    "Statically resolve calls to versioned functions from non-versioned callers.", /*hidden=*/1>;

defm IPO_MaxIFuncVersions : ValueField<
    "max-ifunc-versions", "unsigned", "5",
    "Maximum number of caller/callee versions that is allowed for using the expensive (cubic) static resolution algorithm.", /*hidden=*/1>;

defm IPO_EnableColdCCStressTest : BoolField<
    "enable-coldcc-stress-test", "false",
    "Enable stress test of coldcc by adding calling conv to all internal functions.", /*hidden=*/1>;

defm IPO_ColdCCRelFreq : ValueField<
    "coldcc-rel-freq", "int", "2",
    "Maximum block frequency, expressed as a percentage of caller's entry frequency, for a call site to be considered cold for enabling coldcc", /*hidden=*/1>;


// =========================================================================
// Options -- HotColdSplitting.cpp
// =========================================================================

defm IPO_EnableStaticAnalysis : BoolField<
    "hot-cold-static-analysis", "true",
    "", /*hidden=*/1>;

defm IPO_SplittingThreshold : ValueField<
    "hotcoldsplit-threshold", "int", "2",
    "Base penalty for splitting cold code (as a multiple of TCC_Basic)", /*hidden=*/1>;

defm IPO_EnableColdSection : BoolField<
    "enable-cold-section", "false",
    "Enable placement of extracted cold functions into a separate section after hot-cold splitting.", /*hidden=*/1>;

defm IPO_ColdSectionName : ValueField<
    "hotcoldsplit-cold-section-name", "std::string", "\"__llvm_cold\"",
    "Name for the section containing cold functions extracted by hot-cold splitting.", /*hidden=*/1>;

defm IPO_MaxParametersForSplit : ValueField<
    "hotcoldsplit-max-params", "int", "4",
    "Maximum number of parameters for a split function", /*hidden=*/1>;

defm IPO_ColdBranchProbDenom : OptionalField<
    "hotcoldsplit-cold-probability-denom", "int",
    "Divisor of cold branch probability.BranchProbability = 1/ColdBranchProbDenom", /*hidden=*/1>;


// =========================================================================
// Options -- Inliner.cpp
// =========================================================================

defm IPO_IntraSCCCostMultiplier : ValueField<
    "intra-scc-cost-multiplier", "int", "2",
    "Cost multiplier to multiply onto inlined call sites where the new call was previously an intra-SCC call (not relevant when the original call was already intra-SCC). This can accumulate over multiple inlinings (e.g. if a call site already had a cost multiplier and one of its inlined calls was also subject to this, the inlined call would have the original multiplier multiplied by intra-scc-cost-multiplier). This is to prevent tons of inlining through a child SCC which can cause terrible compile times", /*hidden=*/1>;

defm IPO_InlinerForwardingScanLimit : ValueField<
    "inliner-forwarding-scan-limit", "unsigned", "16",
    "Maximum number of instructions to scan backward for store-to-load forwarding in subsequent inlining decisions. DefMaxInstsToScan=6 is not enough and misses inlining opportunities (e.g. when class stores into mutiple members in ctor and afterwards calls a function reading those members)", /*hidden=*/1>;

defm IPO_KeepAdvisorForPrinting : BoolField<
    "keep-inline-advisor-for-printing", "false",
    "", /*hidden=*/1>;

defm IPO_EnablePostSCCAdvisorPrinting : BoolField<
    "enable-scc-inline-advisor-printing", "false",
    "", /*hidden=*/1>;

defm IPO_CGSCCInlineReplayFile : ValueField<
    "cgscc-inline-replay", "std::string", "\"\"",
    "Optimization remarks file containing inline remarks to be replayed by cgscc inlining.", /*hidden=*/1, /*metaVar=*/"filename">;

defm IPO_CGSCCInlineReplayScope : EnumField<
    "cgscc-inline-replay-scope", ReplayInlinerScopeEnum, "llvm::ReplayInlinerScope::Function",
    "Whether inline replay should be applied to the entire Module or just the Functions (default) that are present as callers in remarks during cgscc inlining.", /*hidden=*/1>;

defm IPO_CGSCCInlineReplayFallback : EnumField<
    "cgscc-inline-replay-fallback", ReplayInlinerFallbackEnum, "llvm::ReplayInlinerFallback::Original",
    "How cgscc inline replay treats sites that don't come from the replay. Original: defers to original advisor, AlwaysInline: inline all sites not in replay, NeverInline: inline no sites not in replay", /*hidden=*/1>;

defm IPO_CGSCCInlineReplayFormat : EnumField<
    "cgscc-inline-replay-format", CallSiteFormatKindEnum, "llvm::CallSiteFormatKind::LineColumnDiscriminator",
    "How cgscc inline replay file is formatted", /*hidden=*/1>;


// =========================================================================
// Options -- Instrumentor.cpp
// =========================================================================

defm IPO_InstrumentorOutputConfigFile : ValueField<
    "instrumentor-write-config-file", "std::string", "\"\"",
    "Write the instrumentor configuration into the specified JSON file">;

defm IPO_InstrumentorConfigFiles : ListField<
    "instrumentor-read-config-files", "std::string",
    "Read the instrumentor configuration from the specified JSON files (comma separated)", /*commaJoined=*/1, /*appendEquals=*/1, /*hidden=*/0>;

defm IPO_InstrumentorConfigPathsFile : ValueField<
    "instrumentor-read-config-paths-file", "std::string", "\"\"",
    "Read the instrumentor configuration file paths from the specified file (newline separated)">;


// =========================================================================
// Options -- Internalize.cpp
// =========================================================================

defm IPO_APIFile : ValueField<
    "internalize-public-api-file", "std::string", "\"\"",
    "A file containing list of symbol names to preserve", /*hidden=*/0,
    /*metaVar=*/"filename">;

defm IPO_APIList : ListField<
    "internalize-public-api-list", "std::string",
    "A list of symbol names to preserve", /*commaJoined=*/1, /*appendEquals=*/1, /*hidden=*/0, /*metaVar=*/"list">;


// =========================================================================
// Options -- LowerTypeTests.cpp
// =========================================================================

defm IPO_AvoidReuse : BoolField<
    "lowertypetests-avoid-reuse", "true",
    "Try to avoid reuse of byte array addresses using aliases", /*hidden=*/1>;

defm IPO_LowerTypeTestsSummaryAction : EnumField<
    "lowertypetests-summary-action", PassSummaryActionEnum, "llvm::PassSummaryAction::None",
    "What to do with the summary when running this pass", /*hidden=*/1>;

defm IPO_LowerTypeTestsReadSummary : ValueField<
    "lowertypetests-read-summary", "std::string", "\"\"",
    "Read summary from given YAML file before running pass", /*hidden=*/1>;

defm IPO_LowerTypeTestsWriteSummary : ValueField<
    "lowertypetests-write-summary", "std::string", "\"\"",
    "Write summary to given YAML file after running pass", /*hidden=*/1>;

defm IPO_EnableJumpTableDebugInfo : BoolField<
    "lowertypetests-jump-table-debug-info", "true",
    "Enable debug info generation for jump tables", /*hidden=*/1>;


// =========================================================================
// Options -- MemProfContextDisambiguation.cpp
// =========================================================================

defm IPO_DotFilePathPrefix : ValueField<
    "memprof-dot-file-path-prefix", "std::string", "\"\"",
    "Specify the path prefix of the MemProf dot files.", /*hidden=*/1, /*metaVar=*/"filename">;

defm IPO_ExportToDot : BoolField<
    "memprof-export-to-dot", "false",
    "Export graph to dot files.", /*hidden=*/1>;

defm IPO_DoMergeIteration : BoolField<
    "memprof-merge-iteration", "true",
    "Iteratively apply merging on a node to catch new callers", /*hidden=*/1>;

defm IPO_DotGraphScope : EnumField<
    "memprof-dot-scope", DotScopeEnum, "DotScope::All",
    "Scope of graph to export to dot", /*hidden=*/1>;

defm IPO_AllocIdForDot : OptionalField<
    "memprof-dot-alloc-id", "unsigned",
    "Id of alloc to export if -memprof-dot-scope=alloc or to highlight if -memprof-dot-scope=all", /*hidden=*/1>;

defm IPO_ContextIdForDot : OptionalField<
    "memprof-dot-context-id", "unsigned",
    "Id of context to export if -memprof-dot-scope=context or to highlight otherwise", /*hidden=*/1>;

defm IPO_DumpCCG : BoolField<
    "memprof-dump-ccg", "false",
    "Dump CallingContextGraph to stdout after each stage.", /*hidden=*/1>;

defm IPO_VerifyCCG : BoolField<
    "memprof-verify-ccg", "false",
    "Perform verification checks on CallingContextGraph.", /*hidden=*/1>;

defm IPO_VerifyNodes : BoolField<
    "memprof-verify-nodes", "false",
    "Perform frequent verification checks on nodes.", /*hidden=*/1>;

defm IPO_MemProfImportSummary : ValueField<
    "memprof-import-summary", "std::string", "\"\"",
    "Import summary to use for testing the ThinLTO backend via opt", /*hidden=*/1>;

defm IPO_TailCallSearchDepth : ValueField<
    "memprof-tail-call-search-depth", "unsigned", "5",
    "Max depth to recursively search for missing frames through tail calls.", /*hidden=*/1>;

defm IPO_AllowRecursiveCallsites : BoolField<
    "memprof-allow-recursive-callsites", "true",
    "Allow cloning of callsites involved in recursive cycles", /*hidden=*/1>;

defm IPO_CloneRecursiveContexts : BoolField<
    "memprof-clone-recursive-contexts", "true",
    "Allow cloning of contexts through recursive cycles", /*hidden=*/1>;

defm IPO_MergeClones : BoolField<
    "memprof-merge-clones", "true",
    "Merge clones before assigning functions", /*hidden=*/1>;

defm IPO_AllowRecursiveContexts : BoolField<
    "memprof-allow-recursive-contexts", "true",
    "Allow cloning of contexts having recursive cycles", /*hidden=*/1>;

defm IPO_MemProfICPNoInlineThreshold : ValueField<
    "memprof-icp-noinline-threshold", "unsigned", "0",
    "Minimum absolute count for promoted target to be inlinable", /*hidden=*/1>;

defm IPO_EnableMemProfContextDisambiguation : BoolField<
    "enable-memprof-context-disambiguation", "false",
    "Enable MemProf context disambiguation", /*hidden=*/1>;

defm IPO_SupportsHotColdNew : BoolField<
    "supports-hot-cold-new", "false",
    "Linking with hot/cold operator new interfaces", /*hidden=*/1>;

defm IPO_MemProfRequireDefinitionForPromotion : BoolField<
    "memprof-require-definition-for-promotion", "false",
    "Require target function definition when promoting indirect calls", /*hidden=*/1>;

defm IPO_MemProfTopNImportant : ValueField<
    "memprof-top-n-important", "unsigned", "10",
    "Number of largest cold contexts to consider important", /*hidden=*/1>;

defm IPO_MemProfFixupImportant : BoolField<
    "memprof-fixup-important", "true",
    "Enables edge fixup for important contexts", /*hidden=*/1>;


// =========================================================================
// Options -- MergeFunctions.cpp
// =========================================================================

defm IPO_NumFunctionsForVerificationCheck : ValueField<
    "mergefunc-verify", "unsigned", "0",
    "How many functions in a module could be used for MergeFunctions to pass a basic correctness check. '0' disables this check. Works only with '-debug' key.", /*hidden=*/1>;

defm IPO_MergeFunctionsPDI : BoolField<
    "mergefunc-preserve-debug-info", "false",
    "Preserve debug info in thunk when mergefunc transformations are made.", /*hidden=*/1>;

defm IPO_MergeFunctionsAliases : BoolField<
    "mergefunc-use-aliases", "false",
    "Allow mergefunc to create aliases", /*hidden=*/1>;


// =========================================================================
// Options -- ModuleInliner.cpp
// =========================================================================

defm IPO_CtxProfPromoteAlwaysInline : BoolField<
    "ctx-prof-promote-alwaysinline", "false",
    "If using a contextual profile in this module, and an indirect call target is marked as alwaysinline, perform indirect call promotion for that target. If multiple targets for an indirect call site fit this description, they are all promoted.", /*hidden=*/1>;


// =========================================================================
// Options -- OpenMPOpt.cpp
// =========================================================================

defm IPO_DisableOpenMPOptimizations : BoolField<
    "openmp-opt-disable", "false",
    "Disable OpenMP specific optimizations.", /*hidden=*/1>;

defm IPO_EnableParallelRegionMerging : BoolField<
    "openmp-opt-enable-merging", "false",
    "Enable the OpenMP region merging optimization.", /*hidden=*/1>;

defm IPO_DisableInternalization : BoolField<
    "openmp-opt-disable-internalization", "false",
    "Disable function internalization.", /*hidden=*/1>;

defm IPO_DeduceICVValues : BoolField<
    "openmp-deduce-icv-values", "false",
    "", /*hidden=*/1>;

defm IPO_PrintICVValues : BoolField<
    "openmp-print-icv-values", "false",
    "", /*hidden=*/1>;

defm IPO_PrintOpenMPKernels : BoolField<
    "openmp-print-gpu-kernels", "false",
    "", /*hidden=*/1>;

defm IPO_HideMemoryTransferLatency : BoolField<
    "openmp-hide-memory-transfer-latency", "false",
    "[WIP] Tries to hide the latency of host to device memory transfers", /*hidden=*/1>;

defm IPO_DisableOpenMPOptDeglobalization : BoolField<
    "openmp-opt-disable-deglobalization", "false",
    "Disable OpenMP optimizations involving deglobalization.", /*hidden=*/1>;

defm IPO_DisableOpenMPOptSPMDization : BoolField<
    "openmp-opt-disable-spmdization", "false",
    "Disable OpenMP optimizations involving SPMD-ization.", /*hidden=*/1>;

defm IPO_DisableOpenMPOptFolding : BoolField<
    "openmp-opt-disable-folding", "false",
    "Disable OpenMP optimizations involving folding.", /*hidden=*/1>;

defm IPO_DisableOpenMPOptStateMachineRewrite : BoolField<
    "openmp-opt-disable-state-machine-rewrite", "false",
    "Disable OpenMP optimizations that replace the state machine.", /*hidden=*/1>;

defm IPO_DisableOpenMPOptBarrierElimination : BoolField<
    "openmp-opt-disable-barrier-elimination", "false",
    "Disable OpenMP optimizations that eliminate barriers.", /*hidden=*/1>;

defm IPO_PrintModuleAfterOptimizations : BoolField<
    "openmp-opt-print-module-after", "false",
    "Print the current module after OpenMP optimizations.", /*hidden=*/1>;

defm IPO_PrintModuleBeforeOptimizations : BoolField<
    "openmp-opt-print-module-before", "false",
    "Print the current module before OpenMP optimizations.", /*hidden=*/1>;

defm IPO_AlwaysInlineDeviceFunctions : BoolField<
    "openmp-opt-inline-device", "false",
    "Inline all applicable functions on the device.", /*hidden=*/1>;

defm IPO_EnableVerboseRemarks : BoolField<
    "openmp-opt-verbose-remarks", "false",
    "Enables more verbose remarks.", /*hidden=*/1>;

defm IPO_OpenMPSetFixpointIterations : ValueField<
    "openmp-opt-max-iterations", "unsigned", "256",
    "Maximal number of attributor iterations.", /*hidden=*/1>;

defm IPO_SharedMemoryLimit : ValueField<
    "openmp-opt-shared-limit", "unsigned", "std::numeric_limits<unsigned>::max()",
    "Maximum amount of shared memory to use.", /*hidden=*/1>;

defm IPO_MaxCalleesForSpecialization : ValueField<
    "openmp-opt-max-callees-for-specialization", "unsigned", "3",
    "Number of possible callees above which an indirect call site is left alone rather than specialized into an if-cascade.", /*hidden=*/1>;


// =========================================================================
// Options -- PartialInlining.cpp
// =========================================================================

defm IPO_DisablePartialInlining : BoolField<
    "disable-partial-inlining", "false",
    "Disable partial inlining", /*hidden=*/1>;

defm IPO_DisableMultiRegionPartialInline : BoolField<
    "disable-mr-partial-inlining", "false",
    "Disable multi-region partial inlining", /*hidden=*/1>;

defm IPO_ForceLiveExit : BoolField<
    "pi-force-live-exit-outline", "false",
    "Force outline regions with live exits", /*hidden=*/1>;

defm IPO_MarkOutlinedColdCC : BoolField<
    "pi-mark-coldcc", "false",
    "Mark outline function calls with ColdCC", /*hidden=*/1>;

defm IPO_SkipCostAnalysis : BoolField<
    "skip-partial-inlining-cost-analysis", "false",
    "Skip Cost Analysis", /*hidden=*/1>;

defm IPO_MinRegionSizeRatio : ValueField<
    "min-region-size-ratio", "float", "0.1",
    "Minimum ratio comparing relative sizes of each outline candidate and original function", /*hidden=*/1>;

defm IPO_MinBlockCounterExecution : ValueField<
    "min-block-execution", "unsigned", "100",
    "Minimum block executions to consider its BranchProbabilityInfo valid", /*hidden=*/1>;

defm IPO_ColdBranchRatio : ValueField<
    "cold-branch-ratio", "float", "0.1",
    "Minimum BranchProbability to consider a region cold.", /*hidden=*/1>;

defm IPO_MaxNumInlineBlocks : ValueField<
    "max-num-inline-blocks", "unsigned", "5",
    "Max number of blocks to be partially inlined", /*hidden=*/1>;

defm IPO_MaxNumPartialInlining : ValueField<
    "max-partial-inlining", "int", "-1",
    "Max number of partial inlining. The default is unlimited", /*hidden=*/1>;

defm IPO_OutlineRegionFreqPercent : ValueField<
    "outline-region-freq-percent", "int", "75",
    "Relative frequency of outline region to the entry block", /*hidden=*/1>;

defm IPO_ExtraOutliningPenalty : ValueField<
    "partial-inlining-extra-penalty", "unsigned", "0",
    "A debug option to add additional penalty to the computed one.", /*hidden=*/1>;


// =========================================================================
// Options -- SCCP.cpp
// =========================================================================

defm IPO_FuncSpecMaxIters : ValueField<
    "funcspec-max-iters", "unsigned", "10",
    "The maximum number of iterations function specialization is run", /*hidden=*/1>;


// =========================================================================
// Options -- SampleProfile.cpp
// =========================================================================

defm IPO_SampleProfileFile : ValueField<
    "sample-profile-file", "std::string", "\"\"",
    "Profile file loaded by -sample-profile", /*hidden=*/1, /*metaVar=*/"filename">;

defm IPO_SampleProfileRemappingFile : ValueField<
    "sample-profile-remapping-file", "std::string", "\"\"",
    "Profile remapping file loaded by -sample-profile", /*hidden=*/1, /*metaVar=*/"filename">;

def IPO_SalvageStaleProfile : Flag<["-", "--"], "salvage-stale-profile">, HelpText<"Salvage stale profile by fuzzy matching and use the remapped location for sample profile query.">,
    Flags<[HelpHidden]>,
    Field<"std::optional<bool>", "IPO_SalvageStaleProfile", "std::nullopt">,
    Assign<"IPO_SalvageStaleProfile", "true">;
def IPO_SalvageStaleProfile_no : Flag<["-", "--"], "no-salvage-stale-profile">, Flags<[HelpHidden]>,
    Assign<"IPO_SalvageStaleProfile", "false">;
def IPO_SalvageStaleProfile_eq : Joined<["-", "--"], "salvage-stale-profile=">, Flags<[HelpHidden]>,
    BoolAssign<"IPO_SalvageStaleProfile">;

def IPO_SalvageUnusedProfile : Flag<["-", "--"], "salvage-unused-profile">, HelpText<"Salvage unused profile by matching with new functions on call graph.">,
    Flags<[HelpHidden]>,
    Field<"std::optional<bool>", "IPO_SalvageUnusedProfile", "std::nullopt">,
    Assign<"IPO_SalvageUnusedProfile", "true">;
def IPO_SalvageUnusedProfile_no : Flag<["-", "--"], "no-salvage-unused-profile">, Flags<[HelpHidden]>,
    Assign<"IPO_SalvageUnusedProfile", "false">;
def IPO_SalvageUnusedProfile_eq : Joined<["-", "--"], "salvage-unused-profile=">, Flags<[HelpHidden]>,
    BoolAssign<"IPO_SalvageUnusedProfile">;

defm IPO_ReportProfileStaleness : BoolField<
    "report-profile-staleness", "false",
    "Compute and report stale profile statistical metrics.", /*hidden=*/1>;

defm IPO_PersistProfileStaleness : BoolField<
    "persist-profile-staleness", "false",
    "Compute stale profile statistical metrics and write it into the native object file(.llvm_stats section).", /*hidden=*/1>;

defm IPO_ProfileSampleAccurate : BoolField<
    "profile-sample-accurate", "false",
    "If the sample profile is accurate, we will mark all un-sampled callsite and function as having 0 samples. Otherwise, treat un-sampled callsites and functions conservatively as unknown.", /*hidden=*/1>;

defm IPO_ProfileSampleBlockAccurate : BoolField<
    "profile-sample-block-accurate", "false",
    "If the sample profile is accurate, we will mark all un-sampled branches and calls as having 0 samples. Otherwise, treat them conservatively as unknown.", /*hidden=*/1>;

defm IPO_ProfileAccurateForSymsInList : BoolField<
    "profile-accurate-for-symsinlist", "true",
    "For symbols in profile symbol list, regard their profiles to be accurate. It may be overridden by profile-sample-accurate.", /*hidden=*/1>;

def IPO_ProfileMergeInlinee : Flag<["-", "--"], "sample-profile-merge-inlinee">, HelpText<"Merge past inlinee's profile to outline version if sample profile loader decided not to inline a call site. It will only be enabled when top-down order of profile loading is enabled.">,
    Flags<[HelpHidden]>,
    Field<"std::optional<bool>", "IPO_ProfileMergeInlinee", "std::nullopt">,
    Assign<"IPO_ProfileMergeInlinee", "true">;
def IPO_ProfileMergeInlinee_no : Flag<["-", "--"], "no-sample-profile-merge-inlinee">, Flags<[HelpHidden]>,
    Assign<"IPO_ProfileMergeInlinee", "false">;
def IPO_ProfileMergeInlinee_eq : Joined<["-", "--"], "sample-profile-merge-inlinee=">, Flags<[HelpHidden]>,
    BoolAssign<"IPO_ProfileMergeInlinee">;

defm IPO_ProfileTopDownLoad : BoolField<
    "sample-profile-top-down-load", "true",
    "Do profile annotation and inlining for functions in top-down order of call graph during sample profile loading. It only works for new pass manager.", /*hidden=*/1>;

def IPO_UseProfiledCallGraph : Flag<["-", "--"], "use-profiled-call-graph">, HelpText<"Process functions in a top-down order defined by the profiled call graph when -sample-profile-top-down-load is on.">,
    Flags<[HelpHidden]>,
    Field<"std::optional<bool>", "IPO_UseProfiledCallGraph", "std::nullopt">,
    Assign<"IPO_UseProfiledCallGraph", "true">;
def IPO_UseProfiledCallGraph_no : Flag<["-", "--"], "no-use-profiled-call-graph">, Flags<[HelpHidden]>,
    Assign<"IPO_UseProfiledCallGraph", "false">;
def IPO_UseProfiledCallGraph_eq : Joined<["-", "--"], "use-profiled-call-graph=">, Flags<[HelpHidden]>,
    BoolAssign<"IPO_UseProfiledCallGraph">;

def IPO_ProfileSizeInline : Flag<["-", "--"], "sample-profile-inline-size">, HelpText<"Inline cold call sites in profile loader if it's beneficial for code size.">,
    Flags<[HelpHidden]>,
    Field<"std::optional<bool>", "IPO_ProfileSizeInline", "std::nullopt">,
    Assign<"IPO_ProfileSizeInline", "true">;
def IPO_ProfileSizeInline_no : Flag<["-", "--"], "no-sample-profile-inline-size">, Flags<[HelpHidden]>,
    Assign<"IPO_ProfileSizeInline", "false">;
def IPO_ProfileSizeInline_eq : Joined<["-", "--"], "sample-profile-inline-size=">, Flags<[HelpHidden]>,
    BoolAssign<"IPO_ProfileSizeInline">;

def IPO_DisableSampleLoaderInlining : Flag<["-", "--"], "disable-sample-loader-inlining">, HelpText<"If true, artificially skip inline transformation in sample-loader pass, and merge (or scale) profiles (as configured by --sample-profile-merge-inlinee).">,
    Flags<[HelpHidden]>,
    Field<"std::optional<bool>", "IPO_DisableSampleLoaderInlining", "std::nullopt">,
    Assign<"IPO_DisableSampleLoaderInlining", "true">;
def IPO_DisableSampleLoaderInlining_no : Flag<["-", "--"], "no-disable-sample-loader-inlining">, Flags<[HelpHidden]>,
    Assign<"IPO_DisableSampleLoaderInlining", "false">;
def IPO_DisableSampleLoaderInlining_eq : Joined<["-", "--"], "disable-sample-loader-inlining=">, Flags<[HelpHidden]>,
    BoolAssign<"IPO_DisableSampleLoaderInlining">;

defm IPO_SortProfiledSCC : BoolField<
    "sort-profiled-scc-member", "true",
    "Sort profiled recursion by edge weights.", /*hidden=*/1>;

defm IPO_ProfileInlineGrowthLimit : ValueField<
    "sample-profile-inline-growth-limit", "int", "12",
    "The size growth ratio limit for proirity-based sample profile loader inlining.", /*hidden=*/1>;

defm IPO_ProfileInlineLimitMin : OptionalField<
    "sample-profile-inline-limit-min", "int",
    "The lower bound of size growth limit for proirity-based sample profile loader inlining.", /*hidden=*/1>;

defm IPO_ProfileInlineLimitMax : OptionalField<
    "sample-profile-inline-limit-max", "int",
    "The upper bound of size growth limit for proirity-based sample profile loader inlining.", /*hidden=*/1>;

defm IPO_SampleHotCallSiteThreshold : OptionalField<
    "sample-profile-hot-inline-threshold", "int",
    "Hot callsite threshold for proirity-based sample profile loader inlining.", /*hidden=*/1>;

defm IPO_SampleColdCallSiteThreshold : OptionalField<
    "sample-profile-cold-inline-threshold", "int",
    "Threshold for inlining cold callsites", /*hidden=*/1>;

defm IPO_ProfileICPRelativeHotness : ValueField<
    "sample-profile-icp-relative-hotness", "unsigned", "25",
    "Relative hotness percentage threshold for indirect call promotion in proirity-based sample profile loader inlining.", /*hidden=*/1>;

defm IPO_ProfileICPRelativeHotnessSkip : ValueField<
    "sample-profile-icp-relative-hotness-skip", "unsigned", "1",
    "Skip relative hotness check for ICP up to given number of targets.", /*hidden=*/1>;

defm IPO_HotFuncCutoffForStalenessError : ValueField<
    "hot-func-cutoff-for-staleness-error", "unsigned", "800000",
    "A function is considered hot for staleness error check if its total sample count is above the specified percentile", /*hidden=*/1>;

defm IPO_MinfuncsForStalenessError : ValueField<
    "min-functions-for-staleness-error", "unsigned", "50",
    "Skip the check if the number of hot functions is smaller than the specified number.", /*hidden=*/1>;

defm IPO_PrecentMismatchForStalenessError : ValueField<
    "precent-mismatch-for-staleness-error", "unsigned", "80",
    "Reject the profile if the mismatch percent is higher than the given number.", /*hidden=*/1>;

def IPO_CallsitePrioritizedInline : Flag<["-", "--"], "sample-profile-prioritized-inline">, HelpText<"Use call site prioritized inlining for sample profile loader. Currently only CSSPGO is supported.">,
    Flags<[HelpHidden]>,
    Field<"std::optional<bool>", "IPO_CallsitePrioritizedInline", "std::nullopt">,
    Assign<"IPO_CallsitePrioritizedInline", "true">;
def IPO_CallsitePrioritizedInline_no : Flag<["-", "--"], "no-sample-profile-prioritized-inline">, Flags<[HelpHidden]>,
    Assign<"IPO_CallsitePrioritizedInline", "false">;
def IPO_CallsitePrioritizedInline_eq : Joined<["-", "--"], "sample-profile-prioritized-inline=">, Flags<[HelpHidden]>,
    BoolAssign<"IPO_CallsitePrioritizedInline">;

def IPO_UsePreInlinerDecision : Flag<["-", "--"], "sample-profile-use-preinliner">, HelpText<"Use the preinliner decisions stored in profile context.">,
    Flags<[HelpHidden]>,
    Field<"std::optional<bool>", "IPO_UsePreInlinerDecision", "std::nullopt">,
    Assign<"IPO_UsePreInlinerDecision", "true">;
def IPO_UsePreInlinerDecision_no : Flag<["-", "--"], "no-sample-profile-use-preinliner">, Flags<[HelpHidden]>,
    Assign<"IPO_UsePreInlinerDecision", "false">;
def IPO_UsePreInlinerDecision_eq : Joined<["-", "--"], "sample-profile-use-preinliner=">, Flags<[HelpHidden]>,
    BoolAssign<"IPO_UsePreInlinerDecision">;

def IPO_AllowRecursiveInline : Flag<["-", "--"], "sample-profile-recursive-inline">, HelpText<"Allow sample loader inliner to inline recursive calls.">,
    Flags<[HelpHidden]>,
    Field<"std::optional<bool>", "IPO_AllowRecursiveInline", "std::nullopt">,
    Assign<"IPO_AllowRecursiveInline", "true">;
def IPO_AllowRecursiveInline_no : Flag<["-", "--"], "no-sample-profile-recursive-inline">, Flags<[HelpHidden]>,
    Assign<"IPO_AllowRecursiveInline", "false">;
def IPO_AllowRecursiveInline_eq : Joined<["-", "--"], "sample-profile-recursive-inline=">, Flags<[HelpHidden]>,
    BoolAssign<"IPO_AllowRecursiveInline">;

defm IPO_RemoveProbeAfterProfileAnnotation : BoolField<
    "sample-profile-remove-probe", "false",
    "Remove pseudo-probe after sample profile annotation.", /*hidden=*/1>;

defm IPO_ProfileInlineReplayFile : ValueField<
    "sample-profile-inline-replay", "std::string", "\"\"",
    "Optimization remarks file containing inline remarks to be replayed by inlining from sample profile loader.", /*hidden=*/1, /*metaVar=*/"filename">;

defm IPO_ProfileInlineReplayScope : EnumField<
    "sample-profile-inline-replay-scope", ReplayInlinerScopeEnum, "llvm::ReplayInlinerScope::Function",
    "Whether inline replay should be applied to the entire Module or just the Functions (default) that are present as callers in remarks during sample profile inlining.", /*hidden=*/1>;

defm IPO_ProfileInlineReplayFallback : EnumField<
    "sample-profile-inline-replay-fallback", ReplayInlinerFallbackEnum, "llvm::ReplayInlinerFallback::Original",
    "How sample profile inline replay treats sites that don't come from the replay. Original: defers to original advisor, AlwaysInline: inline all sites not in replay, NeverInline: inline no sites not in replay", /*hidden=*/1>;

defm IPO_ProfileInlineReplayFormat : EnumField<
    "sample-profile-inline-replay-format", CallSiteFormatKindEnum, "llvm::CallSiteFormatKind::LineColumnDiscriminator",
    "How sample profile inline replay file is formatted", /*hidden=*/1>;

defm IPO_MaxNumPromotions : ValueField<
    "sample-profile-icp-max-prom", "unsigned", "3",
    "Max number of promotions for a single indirect call callsite in sample profile loader", /*hidden=*/1>;

defm IPO_OverwriteExistingWeights : BoolField<
    "overwrite-existing-weights", "false",
    "Ignore existing branch weights on IR and always overwrite.", /*hidden=*/1>;

defm IPO_AnnotateSampleProfileInlinePhase : BoolField<
    "annotate-sample-profile-inline-phase", "false",
    "Annotate LTO phase (prelink / postlink), or main (no LTO) for sample-profile inline pass name.", /*hidden=*/1>;


// =========================================================================
// Options -- SampleProfileMatcher.cpp
// =========================================================================

defm IPO_FuncProfileSimilarityThreshold : ValueField<
    "func-profile-similarity-threshold", "unsigned", "80",
    "Consider a profile matches a function if the similarity of their callee sequences is above the specified percentile.", /*hidden=*/1>;

defm IPO_MinFuncCountForCGMatching : ValueField<
    "min-func-count-for-cg-matching", "unsigned", "5",
    "The minimum number of basic blocks required for a function to run stale profile call graph matching.", /*hidden=*/1>;

defm IPO_MinCallCountForCGMatching : ValueField<
    "min-call-count-for-cg-matching", "unsigned", "3",
    "The minimum number of call anchors required for a function to run stale profile call graph matching.", /*hidden=*/1>;

defm IPO_LoadFuncProfileforCGMatching : BoolField<
    "load-func-profile-for-cg-matching", "true",
    "Load top-level profiles that the sample reader initially skipped for the call-graph matching (only meaningful for extended binary format)", /*hidden=*/1>;

defm IPO_SalvageUnusedProfileMaxFunctions : ValueField<
    "salvage-unused-profile-max-functions", "unsigned", "UINT_MAX",
    "The maximum number of functions in a module, above which salvage unused profile will be skipped.", /*hidden=*/1>;

defm IPO_SalvageStaleProfileMaxCallsites : ValueField<
    "salvage-stale-profile-max-callsites", "unsigned", "UINT_MAX",
    "The maximum number of callsites in a function, above which stale profile matching will be skipped.", /*hidden=*/1>;


// =========================================================================
// Options -- SampleProfileProbe.cpp
// =========================================================================

defm IPO_VerifyPseudoProbe : BoolField<
    "verify-pseudo-probe", "false",
    "Do pseudo probe verification", /*hidden=*/1>;

defm IPO_VerifyPseudoProbeFuncList : ListField<
    "verify-pseudo-probe-funcs", "std::string",
    "The option to specify the name of the functions to verify.", /*commaJoined=*/0, /*appendEquals=*/1, /*hidden=*/1>;

defm IPO_UpdatePseudoProbe : BoolField<
    "update-pseudo-probe", "true",
    "Update pseudo probe distribution factor", /*hidden=*/1>;


// =========================================================================
// Options -- StripSymbols.cpp
// =========================================================================

defm IPO_StripGlobalConstants : BoolField<
    "strip-global-constants", "false",
    "Removes debug compile units which reference to non-existing global constants", /*hidden=*/1>;


// =========================================================================
// Options -- WholeProgramDevirt.cpp
// =========================================================================

defm IPO_WholeProgramDevirtSummaryAction : EnumField<
    "wholeprogramdevirt-summary-action", PassSummaryActionEnum, "llvm::PassSummaryAction::None",
    "What to do with the summary when running this pass", /*hidden=*/1>;

defm IPO_WholeProgramDevirtReadSummary : ValueField<
    "wholeprogramdevirt-read-summary", "std::string", "\"\"",
    "Read summary from given bitcode or YAML file before running pass", /*hidden=*/1>;

defm IPO_WholeProgramDevirtWriteSummary : ValueField<
    "wholeprogramdevirt-write-summary", "std::string", "\"\"",
    "Write summary to given bitcode or YAML file after running pass. Output file format is deduced from extension: *.bc means writing bitcode, otherwise YAML", /*hidden=*/1>;

defm IPO_ClDevirtualizeSpeculatively : BoolField<
    "devirtualize-speculatively", "false",
    "Enable speculative devirtualization optimization">;

defm IPO_ClThreshold : ValueField<
    "wholeprogramdevirt-branch-funnel-threshold", "unsigned", "10",
    "Maximum number of call targets per call site to enable branch funnels", /*hidden=*/1>;

defm IPO_PrintSummaryDevirt : BoolField<
    "wholeprogramdevirt-print-index-based", "false",
    "Print index-based devirtualization messages", /*hidden=*/1>;

defm IPO_WholeProgramVisibility : BoolField<
    "whole-program-visibility", "false",
    "Enable whole program visibility", /*hidden=*/1>;

defm IPO_DisableWholeProgramVisibility : BoolField<
    "disable-whole-program-visibility", "false",
    "Disable whole program visibility (overrides enabling options)", /*hidden=*/1>;

defm IPO_SkipFunctionNames : ListField<
    "wholeprogramdevirt-skip", "std::string",
    "Prevent function(s) from being devirtualized", /*commaJoined=*/1, /*appendEquals=*/1, /*hidden=*/1>;

defm IPO_WholeProgramDevirtKeepUnreachableFunction : BoolField<
    "wholeprogramdevirt-keep-unreachable-function", "true",
    "Regard unreachable functions as possible devirtualize targets.", /*hidden=*/1>;

defm IPO_DevirtCheckMode : EnumField<
    "wholeprogramdevirt-check", WPDCheckModeEnum, "WPDCheckMode::None",
    "Type of checking for incorrect devirtualizations", /*hidden=*/1>;

