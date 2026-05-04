//===- IPOOptions.td - CLV2 option defs for IPO -----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def IPOOptsReg : Registry<"llvm::ipo_opts"> { let OutOfLineGetters = 1; }

// =========================================================================
// External enum: ExpandVariadicsMode (from ExpandVariadics.h)
// =========================================================================

def ExpandVariadicsModeUnspecified : EnumValue<"unspecified",
    "Use the implementation defaults"> {
  let CppName = "Unspecified";
}

def ExpandVariadicsModeDisable : EnumValue<"disable",
    "Disable the pass entirely"> {
  let CppName = "Disable";
}

def ExpandVariadicsModeOptimize : EnumValue<"optimize",
    "Optimise without changing ABI"> {
  let CppName = "Optimize";
}

def ExpandVariadicsModeLowering : EnumValue<"lowering",
    "Change variadic calling convention"> {
  let CppName = "Lowering";
}

def ExpandVariadicsModeEnum : EnumType<"ExpandVariadicsMode",
    [ExpandVariadicsModeUnspecified, ExpandVariadicsModeDisable,
     ExpandVariadicsModeOptimize, ExpandVariadicsModeLowering]> {
  let External = 1;
}

// =========================================================================
// External enum: ReplayInlinerSettings::Scope (from ReplayInlineAdvisor.h)
// =========================================================================

def ReplayInlinerScopeFunction : EnumValue<"Function",
    "Replay on functions that have remarks associated "
    "with them (default)"> {
  let CppName = "Function";
}

def ReplayInlinerScopeModule : EnumValue<"Module",
    "Replay on the entire module"> {
  let CppName = "Module";
}

def ReplayInlinerScopeEnum : EnumType<"ReplayInlinerScope",
    [ReplayInlinerScopeFunction, ReplayInlinerScopeModule]> {
  let External = 1;
}

// =========================================================================
// External enum: ReplayInlinerSettings::Fallback (from ReplayInlineAdvisor.h)
// =========================================================================

def ReplayInlinerFallbackOriginal : EnumValue<"Original",
    "All decisions not in replay send to original advisor (default)"> {
  let CppName = "Original";
}

def ReplayInlinerFallbackAlwaysInline : EnumValue<"AlwaysInline",
    "All decisions not in replay are inlined"> {
  let CppName = "AlwaysInline";
}

def ReplayInlinerFallbackNeverInline : EnumValue<"NeverInline",
    "All decisions not in replay are not inlined"> {
  let CppName = "NeverInline";
}

def ReplayInlinerFallbackEnum : EnumType<"ReplayInlinerFallback",
    [ReplayInlinerFallbackOriginal, ReplayInlinerFallbackAlwaysInline,
     ReplayInlinerFallbackNeverInline]> {
  let External = 1;
}

// =========================================================================
// External enum: CallSiteFormat::Format (from ReplayInlineAdvisor.h)
// =========================================================================

def CallSiteFormatLine : EnumValue<"Line",
    "<Line Number>"> {
  let CppName = "Line";
}

def CallSiteFormatLineColumn : EnumValue<"LineColumn",
    "<Line Number>:<Column Number>"> {
  let CppName = "LineColumn";
}

def CallSiteFormatLineDiscriminator : EnumValue<"LineDiscriminator",
    "<Line Number>.<Discriminator>"> {
  let CppName = "LineDiscriminator";
}

def CallSiteFormatLineColumnDiscriminator : EnumValue<"LineColumnDiscriminator",
    "<Line Number>:<Column Number>.<Discriminator> (default)"> {
  let CppName = "LineColumnDiscriminator";
}

def CallSiteFormatEnum : EnumType<"CallSiteFormatKind",
    [CallSiteFormatLine, CallSiteFormatLineColumn,
     CallSiteFormatLineDiscriminator,
     CallSiteFormatLineColumnDiscriminator]> {
  let External = 1;
}

// =========================================================================
// External enum: PassSummaryAction (from IPO.h)
// =========================================================================

def PassSummaryActionNone : EnumValue<"none",
    "Do nothing"> {
  let CppName = "None";
}

def PassSummaryActionImport : EnumValue<"import",
    "Import typeid resolutions from summary and globals"> {
  let CppName = "Import";
}

def PassSummaryActionExport : EnumValue<"export",
    "Export typeid resolutions to summary and globals"> {
  let CppName = "Export";
}

def PassSummaryActionEnum : EnumType<"PassSummaryAction",
    [PassSummaryActionNone, PassSummaryActionImport,
     PassSummaryActionExport]> {
  let External = 1;
}

// =========================================================================
// Inline enum: DotScope (defined in MemProfContextDisambiguation.cpp)
// =========================================================================

def DotScopeAll : EnumValue<"all",
    "Export full callsite graph"> {
  let CppName = "DotScope_All";
}

def DotScopeAlloc : EnumValue<"alloc",
    "Export only nodes with contexts feeding given "
    "-memprof-dot-alloc-id"> {
  let CppName = "DotScope_Alloc";
}

def DotScopeContext : EnumValue<"context",
    "Export only nodes with given -memprof-dot-context-id"> {
  let CppName = "DotScope_Context";
}

def DotScopeEnum : EnumType<"DotScope",
    [DotScopeAll, DotScopeAlloc, DotScopeContext]>;

// =========================================================================
// Inline enum: WPDCheckMode (defined in WholeProgramDevirt.cpp)
// =========================================================================

def WPDCheckModeNone : EnumValue<"none",
    "No checking"> {
  let CppName = "WPDCheckMode_None";
}

def WPDCheckModeTrap : EnumValue<"trap",
    "Trap when incorrect"> {
  let CppName = "WPDCheckMode_Trap";
}

def WPDCheckModeFallback : EnumValue<"fallback",
    "Fallback to indirect when incorrect"> {
  let CppName = "WPDCheckMode_Fallback";
}

def WPDCheckModeEnum : EnumType<"WPDCheckMode",
    [WPDCheckModeNone, WPDCheckModeTrap, WPDCheckModeFallback]>;

// =========================================================================
// Options -- Attributor.cpp
// =========================================================================

def IPO_SetFixpointIterations : Option<
    "attributor-max-iterations",
    "Maximal number of fixpoint iterations.",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "32";
  let Hidden = 1;
}

def IPO_MaxSpecializationPerCB : Option<
    "attributor-max-specializations-per-call-base",
    "Maximal number of callees specialized for a call base",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "UINT32_MAX";
  let Hidden = 1;
}

def IPO_MaxInitializationChainLengthX : Option<
    "attributor-max-initialization-chain-length",
    "Maximal number of chained initializations (to avoid stack overflows)",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "1024";
  let Hidden = 1;
}

def IPO_AnnotateDeclarationCallSites : Option<
    "attributor-annotate-decl-cs",
    "Annotate call sites of function declarations.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_EnableHeapToStack : Option<
    "enable-heap-to-stack-conversion", "",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def IPO_AllowShallowWrappers : Option<
    "attributor-allow-shallow-wrappers",
    "Allow the Attributor to create shallow "
    "wrappers for non-exact definitions.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_AllowDeepWrapper : Option<
    "attributor-allow-deep-wrappers",
    "Allow the Attributor to use IP information "
    "derived from non-exact functions via cloning",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_SeedAllowList : ListOption<
    "attributor-seed-allow-list",
    "Comma separated list of attribute names that are "
    "allowed to be seeded.",
    "std::string"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
  let CommaSeparated = 1;
}

def IPO_FunctionSeedAllowList : ListOption<
    "attributor-function-seed-allow-list",
    "Comma separated list of function names that are "
    "allowed to be seeded.",
    "std::string"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
  let CommaSeparated = 1;
}

def IPO_DumpDepGraph : Option<
    "attributor-dump-dep-graph",
    "Dump the dependency graph to dot files.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_DepGraphDotFileNamePrefix : Option<
    "attributor-depgraph-dot-filename-prefix",
    "The prefix used for the CallGraph dot file names.",
    "std::string"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
}

def IPO_ViewDepGraph : Option<
    "attributor-view-dep-graph",
    "View the dependency graph.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_PrintDependencies : Option<
    "attributor-print-dep",
    "Print attribute dependencies",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_EnableCallSiteSpecific : Option<
    "attributor-enable-call-site-specific-deduction",
    "Allow the Attributor to do call site specific analysis",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_PrintCallGraph : Option<
    "attributor-print-call-graph",
    "Print Attributor's internal call graph",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_SimplifyAllLoads : Option<
    "attributor-simplify-all-loads",
    "Try to simplify all loads.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def IPO_CloseWorldAssumption : Option<
    "attributor-assume-closed-world",
    "Should a closed world be assumed, or not. Default if not set.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
}

// =========================================================================
// Options -- AttributorAttributes.cpp
// =========================================================================

def IPO_ManifestInternal : Option<
    "attributor-manifest-internal",
    "Manifest Attributor internal string attributes.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_MaxHeapToStackSize : Option<
    "max-heap-to-stack-size", "",
    "int"> {
  let Registry = IPOOptsReg;
  let Default = "128";
  let Hidden = 1;
}

def IPO_MaxPotentialValues : Option<
    "attributor-max-potential-values",
    "Maximum number of potential values to be "
    "tracked for each position.",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "7";
  let Hidden = 1;
}

def IPO_MaxPotentialValuesIterations : Option<
    "attributor-max-potential-values-iterations",
    "Maximum number of iterations we keep dismantling potential values.",
    "int"> {
  let Registry = IPOOptsReg;
  let Default = "64";
  let Hidden = 1;
}

// =========================================================================
// Options -- BlockExtractor.cpp
// =========================================================================

def IPO_BlockExtractorFile : Option<
    "extract-blocks-file",
    "A file containing list of basic blocks to extract",
    "std::string"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
  let ValueDesc = "filename";
}

def IPO_BlockExtractorEraseFuncs : Option<
    "extract-blocks-erase-funcs",
    "Erase the existing functions",
    "bool"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
}

// =========================================================================
// Options -- CalledValuePropagation.cpp
// =========================================================================

def IPO_MaxFunctionsPerValue : Option<
    "cvp-max-functions-per-value",
    "The maximum number of functions to track per lattice value",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "4";
  let Hidden = 1;
}

// =========================================================================
// Options -- ElimAvailExtern.cpp
// =========================================================================

def IPO_ConvertToLocal : Option<
    "avail-extern-to-local",
    "Convert available_externally into locals, renaming them "
    "to avoid link-time clashes.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
}

def IPO_ConvertGlobalVariableInAddrSpace : Option<
    "avail-extern-gv-in-addrspace-to-local",
    "Convert available_externally global variables into locals if they are "
    "in specificed addrspace, renaming them to avoid link-time clashes.",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
}

// =========================================================================
// Options -- ExpandVariadics.cpp
// =========================================================================

def IPO_ExpandVariadicsModeOption : Option<
    "expand-variadics-override",
    "Override the behaviour of expand-variadics",
    "ExpandVariadicsMode"> {
  let Registry = IPOOptsReg;
  let Default = "unspecified";
}

// =========================================================================
// Options -- ForceFunctionAttrs.cpp
// =========================================================================

def IPO_ForceAttributes : ListOption<
    "force-attribute",
    "Add an attribute to a function. This can be a "
    "pair of 'function-name:attribute-name', to apply an attribute to a "
    "specific function. For "
    "example -force-attribute=foo:noinline. Specifying only an attribute "
    "will apply the attribute to every function in the module. This "
    "option can be specified multiple times.",
    "std::string"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
}

def IPO_ForceRemoveAttributes : ListOption<
    "force-remove-attribute",
    "Remove an attribute from a function. This can be a "
    "pair of 'function-name:attribute-name' to remove an attribute "
    "from a specific function. For "
    "example -force-remove-attribute=foo:noinline. Specifying only an "
    "attribute will remove the attribute from all functions in the "
    "module. This "
    "option can be specified multiple times.",
    "std::string"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
}

def IPO_CSVFilePath : Option<
    "forceattrs-csv-path",
    "Path to CSV file containing lines of function names and attributes to "
    "add to them in the form of `f1,attr1` or `f2,attr2=str`.",
    "std::string"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
}

// =========================================================================
// Options -- FunctionAttrs.cpp
// =========================================================================

def IPO_EnablePoisonArgAttrPropagation : Option<
    "enable-poison-arg-attr-prop",
    "Try to propagate nonnull and nofpclass argument attributes from "
    "callsites to caller functions.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def IPO_DisableNoUnwindInference : Option<
    "disable-nounwind-inference",
    "Stop inferring nounwind attribute during function-attrs pass",
    "bool"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
}

def IPO_DisableNoFreeInference : Option<
    "disable-nofree-inference",
    "Stop inferring nofree attribute during function-attrs pass",
    "bool"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
}

def IPO_DisableThinLTOPropagation : Option<
    "disable-thinlto-funcattrs",
    "Don't propagate function-attrs in thinLTO",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// =========================================================================
// Options -- FunctionImport.cpp
// =========================================================================

def IPO_ForceImportAll : Option<
    "force-import-all",
    "Import functions with noinline attribute",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_ImportInstrLimit : Option<
    "import-instr-limit",
    "Only import functions with less than N instructions",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "100";
  let Hidden = 1;
  let ValueDesc = "N";
}

def IPO_ImportCutoff : Option<
    "import-cutoff",
    "Only import first N functions if N>=0 (default -1)",
    "int"> {
  let Registry = IPOOptsReg;
  let Default = "-1";
  let Hidden = 1;
  let ValueDesc = "N";
}

def IPO_ImportInstrFactor : Option<
    "import-instr-evolution-factor",
    "As we import functions, multiply the "
    "`import-instr-limit` threshold by this factor "
    "before processing newly imported functions",
    "float"> {
  let Registry = IPOOptsReg;
  let Default = "0.7";
  let Hidden = 1;
  let ValueDesc = "x";
}

def IPO_ImportHotInstrFactor : Option<
    "import-hot-evolution-factor",
    "As we import functions called from hot callsite, multiply the "
    "`import-instr-limit` threshold by this factor "
    "before processing newly imported functions",
    "float"> {
  let Registry = IPOOptsReg;
  let Default = "1.0";
  let Hidden = 1;
  let ValueDesc = "x";
}

def IPO_ImportHotMultiplier : Option<
    "import-hot-multiplier",
    "Multiply the `import-instr-limit` threshold for hot callsites",
    "float"> {
  let Registry = IPOOptsReg;
  let Default = "10.0";
  let Hidden = 1;
  let ValueDesc = "x";
}

def IPO_ImportCriticalMultiplier : Option<
    "import-critical-multiplier",
    "Multiply the `import-instr-limit` threshold for critical callsites",
    "float"> {
  let Registry = IPOOptsReg;
  let Default = "100.0";
  let Hidden = 1;
  let ValueDesc = "x";
}

def IPO_ImportColdMultiplier : Option<
    "import-cold-multiplier",
    "Multiply the `import-instr-limit` threshold for cold callsites",
    "float"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
  let ValueDesc = "N";
}

def IPO_PrintImports : Option<
    "print-imports",
    "Print imported functions",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_PrintImportFailures : Option<
    "print-import-failures",
    "Print information for functions rejected for importing",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_ComputeDead : Option<
    "compute-dead",
    "Compute dead symbols",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def IPO_EnableImportMetadata : Option<
    "enable-import-metadata",
    "Enable import metadata like 'thinlto_src_module' and "
    "'thinlto_src_file'",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_SummaryFile : Option<
    "summary-file",
    "The summary file to use for function importing.",
    "std::string"> {
  let Registry = IPOOptsReg;
}

def IPO_ImportAllIndex : Option<
    "import-all-index",
    "Import all external functions in index.",
    "bool"> {
  let Registry = IPOOptsReg;
}

def IPO_ImportDeclaration : Option<
    "import-declaration",
    "If true, import function declaration as fallback if the function "
    "definition is not imported.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_WorkloadDefinitions : Option<
    "thinlto-workload-def",
    "Pass a workload definition. This is a file containing a JSON "
    "dictionary. The keys are root functions, the values are lists of "
    "functions to import in the module defining the root. It is "
    "assumed -funique-internal-linkage-names was used, to ensure "
    "local linkage functions have unique names.",
    "std::string"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
}

def IPO_CtxprofMoveRootsToOwnModule : Option<
    "thinlto-move-ctxprof-trees",
    "Move contextual profiling roots and the graphs under them in "
    "their own module.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// =========================================================================
// Options -- FunctionSpecialization.cpp
// =========================================================================

def IPO_ForceSpecialization : Option<
    "force-specialization",
    "Force function specialization for every call site with a constant "
    "argument",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_MaxClones : Option<
    "funcspec-max-clones",
    "The maximum number of clones allowed for a single function "
    "specialization",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "3";
  let Hidden = 1;
}

def IPO_MaxDiscoveryIterations : Option<
    "funcspec-max-discovery-iterations",
    "The maximum number of iterations allowed "
    "when searching for transitive phis",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "100";
  let Hidden = 1;
}

def IPO_MaxIncomingPhiValues : Option<
    "funcspec-max-incoming-phi-values",
    "The maximum number of incoming values a PHI node can have to be "
    "considered during the specialization bonus estimation",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "8";
  let Hidden = 1;
}

def IPO_MaxBlockPredecessors : Option<
    "funcspec-max-block-predecessors",
    "The maximum number of predecessors a basic block can have to be "
    "considered during the estimation of dead code",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def IPO_MinFunctionSize : Option<
    "funcspec-min-function-size",
    "Don't specialize functions that have less than this number of "
    "instructions",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "500";
  let Hidden = 1;
}

def IPO_MaxCodeSizeGrowth : Option<
    "funcspec-max-codesize-growth",
    "Maximum codesize growth allowed per function",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "3";
  let Hidden = 1;
}

def IPO_MinCodeSizeSavings : Option<
    "funcspec-min-codesize-savings",
    "Reject specializations whose codesize savings are less than this "
    "much percent of the original function size",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "20";
  let Hidden = 1;
}

def IPO_MinLatencySavings : Option<
    "funcspec-min-latency-savings",
    "Reject specializations whose latency savings are less than this "
    "much percent of the original function size",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "20";
  let Hidden = 1;
}

def IPO_MinInliningBonus : Option<
    "funcspec-min-inlining-bonus",
    "Reject specializations whose inlining bonus is less than this "
    "much percent of the original function size",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "300";
  let Hidden = 1;
}

def IPO_SpecializeOnAddress : Option<
    "funcspec-on-address",
    "Enable function specialization on the address of global values",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_SpecializeLiteralConstant : Option<
    "funcspec-for-literal-constant",
    "Enable specialization of functions that take a literal constant as an "
    "argument",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// =========================================================================
// Options -- GlobalDCE.cpp
// =========================================================================

def IPO_ClEnableVFE : Option<
    "enable-vfe",
    "Enable virtual function elimination",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// =========================================================================
// Options -- GlobalOpt.cpp
// =========================================================================

def IPO_OptimizeNonFMVCallers : Option<
    "optimize-non-fmv-callers",
    "Statically resolve calls to versioned "
    "functions from non-versioned callers.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def IPO_MaxIFuncVersions : Option<
    "max-ifunc-versions",
    "Maximum number of caller/callee versions that is allowed for "
    "using the expensive (cubic) static resolution algorithm.",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "5";
  let Hidden = 1;
}

def IPO_EnableColdCCStressTest : Option<
    "enable-coldcc-stress-test",
    "Enable stress test of coldcc by adding "
    "calling conv to all internal functions.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_ColdCCRelFreq : Option<
    "coldcc-rel-freq",
    "Maximum block frequency, expressed as a percentage of caller's "
    "entry frequency, for a call site to be considered cold for enabling "
    "coldcc",
    "int"> {
  let Registry = IPOOptsReg;
  let Default = "2";
  let Hidden = 1;
}

// =========================================================================
// Options -- HotColdSplitting.cpp
// =========================================================================

def IPO_EnableStaticAnalysis : Option<
    "hot-cold-static-analysis", "",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def IPO_SplittingThreshold : Option<
    "hotcoldsplit-threshold",
    "Base penalty for splitting cold code (as a "
    "multiple of TCC_Basic)",
    "int"> {
  let Registry = IPOOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def IPO_EnableColdSection : Option<
    "enable-cold-section",
    "Enable placement of extracted cold functions"
    " into a separate section after hot-cold splitting.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_ColdSectionName : Option<
    "hotcoldsplit-cold-section-name",
    "Name for the section containing cold functions "
    "extracted by hot-cold splitting.",
    "std::string"> {
  let Registry = IPOOptsReg;
  let Default = "\"__llvm_cold\"";
  let Hidden = 1;
}

def IPO_MaxParametersForSplit : Option<
    "hotcoldsplit-max-params",
    "Maximum number of parameters for a split function",
    "int"> {
  let Registry = IPOOptsReg;
  let Default = "4";
  let Hidden = 1;
}

def IPO_ColdBranchProbDenom : Option<
    "hotcoldsplit-cold-probability-denom",
    "Divisor of cold branch probability."
    "BranchProbability = 1/ColdBranchProbDenom",
    "int"> {
  let Registry = IPOOptsReg;
  let Default = "100";
  let Hidden = 1;
}

// =========================================================================
// Options -- Inliner.cpp
// =========================================================================

def IPO_IntraSCCCostMultiplier : Option<
    "intra-scc-cost-multiplier",
    "Cost multiplier to multiply onto inlined call sites where the "
    "new call was previously an intra-SCC call (not relevant when the "
    "original call was already intra-SCC). This can accumulate over "
    "multiple inlinings (e.g. if a call site already had a cost "
    "multiplier and one of its inlined calls was also subject to "
    "this, the inlined call would have the original multiplier "
    "multiplied by intra-scc-cost-multiplier). This is to prevent tons of "
    "inlining through a child SCC which can cause terrible compile times",
    "int"> {
  let Registry = IPOOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def IPO_InlinerForwardingScanLimit : Option<
    "inliner-forwarding-scan-limit",
    "Maximum number of instructions to scan backward for "
    "store-to-load forwarding in subsequent inlining decisions. "
    "DefMaxInstsToScan=6 is not enough and misses inlining "
    "opportunities (e.g. when class stores into mutiple members in "
    "ctor and afterwards calls a function reading those members)",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "16";
  let Hidden = 1;
}

def IPO_KeepAdvisorForPrinting : Option<
    "keep-inline-advisor-for-printing", "",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_EnablePostSCCAdvisorPrinting : Option<
    "enable-scc-inline-advisor-printing", "",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_CGSCCInlineReplayFile : Option<
    "cgscc-inline-replay",
    "Optimization remarks file containing inline remarks to be replayed "
    "by cgscc inlining.",
    "std::string"> {
  let Registry = IPOOptsReg;
  let Default = "\"\"";
  let Hidden = 1;
  let ValueDesc = "filename";
}

def IPO_CGSCCInlineReplayScope : Option<
    "cgscc-inline-replay-scope",
    "Whether inline replay should be applied to the entire "
    "Module or just the Functions (default) that are present as "
    "callers in remarks during cgscc inlining.",
    "ReplayInlinerScope"> {
  let Registry = IPOOptsReg;
  let Default = "Function";
  let Hidden = 1;
}

def IPO_CGSCCInlineReplayFallback : Option<
    "cgscc-inline-replay-fallback",
    "How cgscc inline replay treats sites that don't come from the replay. "
    "Original: defers to original advisor, AlwaysInline: inline all sites "
    "not in replay, NeverInline: inline no sites not in replay",
    "ReplayInlinerFallback"> {
  let Registry = IPOOptsReg;
  let Default = "Original";
  let Hidden = 1;
}

def IPO_CGSCCInlineReplayFormat : Option<
    "cgscc-inline-replay-format",
    "How cgscc inline replay file is formatted",
    "CallSiteFormatKind"> {
  let Registry = IPOOptsReg;
  let Default = "LineColumnDiscriminator";
  let Hidden = 1;
}

// =========================================================================
// Options -- Instrumentor.cpp
// =========================================================================

def IPO_InstrumentorOutputConfigFile : Option<
    "instrumentor-write-config-file",
    "Write the instrumentor configuration into the specified JSON file",
    "std::string"> {
  let Registry = IPOOptsReg;
  let Default = "\"\"";
}

def IPO_InstrumentorConfigFiles : ListOption<
    "instrumentor-read-config-files",
    "Read the instrumentor configuration from the "
    "specified JSON files (comma separated)",
    "std::string"> {
  let Registry = IPOOptsReg;
  let CommaSeparated = 1;
}

def IPO_InstrumentorConfigPathsFile : Option<
    "instrumentor-read-config-paths-file",
    "Read the instrumentor configuration file "
    "paths from the specified file (newline separated)",
    "std::string"> {
  let Registry = IPOOptsReg;
  let Default = "\"\"";
}

// =========================================================================
// Options -- Internalize.cpp
// =========================================================================

def IPO_APIFile : Option<
    "internalize-public-api-file",
    "A file containing list of symbol names to preserve",
    "std::string"> {
  let Registry = IPOOptsReg;
  let ValueDesc = "filename";
}

def IPO_APIList : ListOption<
    "internalize-public-api-list",
    "A list of symbol names to preserve",
    "std::string"> {
  let Registry = IPOOptsReg;
  let CommaSeparated = 1;
  let ValueDesc = "list";
}

// =========================================================================
// Options -- LowerTypeTests.cpp
// =========================================================================

def IPO_AvoidReuse : Option<
    "lowertypetests-avoid-reuse",
    "Try to avoid reuse of byte array addresses using aliases",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def IPO_LowerTypeTestsSummaryAction : Option<
    "lowertypetests-summary-action",
    "What to do with the summary when running this pass",
    "PassSummaryAction"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
}

def IPO_LowerTypeTestsReadSummary : Option<
    "lowertypetests-read-summary",
    "Read summary from given YAML file before running pass",
    "std::string"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
}

def IPO_LowerTypeTestsWriteSummary : Option<
    "lowertypetests-write-summary",
    "Write summary to given YAML file after running pass",
    "std::string"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
}

def IPO_EnableJumpTableDebugInfo : Option<
    "lowertypetests-jump-table-debug-info",
    "Enable debug info generation for jump tables",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// =========================================================================
// Options -- MemProfContextDisambiguation.cpp
// =========================================================================

def IPO_DotFilePathPrefix : Option<
    "memprof-dot-file-path-prefix",
    "Specify the path prefix of the MemProf dot files.",
    "std::string"> {
  let Registry = IPOOptsReg;
  let Default = "\"\"";
  let Hidden = 1;
  let ValueDesc = "filename";
}

def IPO_ExportToDot : Option<
    "memprof-export-to-dot",
    "Export graph to dot files.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_DoMergeIteration : Option<
    "memprof-merge-iteration",
    "Iteratively apply merging on a node to catch new callers",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def IPO_DotGraphScope : Option<
    "memprof-dot-scope",
    "Scope of graph to export to dot",
    "DotScope"> {
  let Registry = IPOOptsReg;
  let Default = "all";
  let Hidden = 1;
}

def IPO_AllocIdForDot : Option<
    "memprof-dot-alloc-id",
    "Id of alloc to export if -memprof-dot-scope=alloc "
    "or to highlight if -memprof-dot-scope=all",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_ContextIdForDot : Option<
    "memprof-dot-context-id",
    "Id of context to export if -memprof-dot-scope=context or to "
    "highlight otherwise",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_DumpCCG : Option<
    "memprof-dump-ccg",
    "Dump CallingContextGraph to stdout after each stage.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_VerifyCCG : Option<
    "memprof-verify-ccg",
    "Perform verification checks on CallingContextGraph.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_VerifyNodes : Option<
    "memprof-verify-nodes",
    "Perform frequent verification checks on nodes.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_MemProfImportSummary : Option<
    "memprof-import-summary",
    "Import summary to use for testing the ThinLTO backend via opt",
    "std::string"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
}

def IPO_TailCallSearchDepth : Option<
    "memprof-tail-call-search-depth",
    "Max depth to recursively search for missing "
    "frames through tail calls.",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "5";
  let Hidden = 1;
}

def IPO_AllowRecursiveCallsites : Option<
    "memprof-allow-recursive-callsites",
    "Allow cloning of callsites involved in recursive cycles",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def IPO_CloneRecursiveContexts : Option<
    "memprof-clone-recursive-contexts",
    "Allow cloning of contexts through recursive cycles",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def IPO_MergeClones : Option<
    "memprof-merge-clones",
    "Merge clones before assigning functions",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def IPO_AllowRecursiveContexts : Option<
    "memprof-allow-recursive-contexts",
    "Allow cloning of contexts having recursive cycles",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def IPO_MemProfICPNoInlineThreshold : Option<
    "memprof-icp-noinline-threshold",
    "Minimum absolute count for promoted target to be inlinable",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_EnableMemProfContextDisambiguation : Option<
    "enable-memprof-context-disambiguation",
    "Enable MemProf context disambiguation",
    "bool"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
}

def IPO_SupportsHotColdNew : Option<
    "supports-hot-cold-new",
    "Linking with hot/cold operator new interfaces",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_MemProfRequireDefinitionForPromotion : Option<
    "memprof-require-definition-for-promotion",
    "Require target function definition when promoting indirect calls",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_MemProfTopNImportant : Option<
    "memprof-top-n-important",
    "Number of largest cold contexts to consider important",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "10";
  let Hidden = 1;
}

def IPO_MemProfFixupImportant : Option<
    "memprof-fixup-important",
    "Enables edge fixup for important contexts",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// =========================================================================
// Options -- MergeFunctions.cpp
// =========================================================================

def IPO_NumFunctionsForVerificationCheck : Option<
    "mergefunc-verify",
    "How many functions in a module could be used for "
    "MergeFunctions to pass a basic correctness check. "
    "'0' disables this check. Works only with '-debug' key.",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_MergeFunctionsPDI : Option<
    "mergefunc-preserve-debug-info",
    "Preserve debug info in thunk when mergefunc "
    "transformations are made.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_MergeFunctionsAliases : Option<
    "mergefunc-use-aliases",
    "Allow mergefunc to create aliases",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// =========================================================================
// Options -- ModuleInliner.cpp
// =========================================================================

def IPO_CtxProfPromoteAlwaysInline : Option<
    "ctx-prof-promote-alwaysinline",
    "If using a contextual profile in this module, and an indirect "
    "call target is marked as alwaysinline, perform indirect call "
    "promotion for that target. If multiple targets for an indirect "
    "call site fit this description, they are all promoted.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// =========================================================================
// Options -- OpenMPOpt.cpp
// =========================================================================

def IPO_DisableOpenMPOptimizations : Option<
    "openmp-opt-disable",
    "Disable OpenMP specific optimizations.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_EnableParallelRegionMerging : Option<
    "openmp-opt-enable-merging",
    "Enable the OpenMP region merging optimization.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_DisableInternalization : Option<
    "openmp-opt-disable-internalization",
    "Disable function internalization.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_DeduceICVValues : Option<
    "openmp-deduce-icv-values", "",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_PrintICVValues : Option<
    "openmp-print-icv-values", "",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_PrintOpenMPKernels : Option<
    "openmp-print-gpu-kernels", "",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_HideMemoryTransferLatency : Option<
    "openmp-hide-memory-transfer-latency",
    "[WIP] Tries to hide the latency of host to device memory"
    " transfers",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_DisableOpenMPOptDeglobalization : Option<
    "openmp-opt-disable-deglobalization",
    "Disable OpenMP optimizations involving deglobalization.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_DisableOpenMPOptSPMDization : Option<
    "openmp-opt-disable-spmdization",
    "Disable OpenMP optimizations involving SPMD-ization.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_DisableOpenMPOptFolding : Option<
    "openmp-opt-disable-folding",
    "Disable OpenMP optimizations involving folding.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_DisableOpenMPOptStateMachineRewrite : Option<
    "openmp-opt-disable-state-machine-rewrite",
    "Disable OpenMP optimizations that replace the state machine.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_DisableOpenMPOptBarrierElimination : Option<
    "openmp-opt-disable-barrier-elimination",
    "Disable OpenMP optimizations that eliminate barriers.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_PrintModuleAfterOptimizations : Option<
    "openmp-opt-print-module-after",
    "Print the current module after OpenMP optimizations.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_PrintModuleBeforeOptimizations : Option<
    "openmp-opt-print-module-before",
    "Print the current module before OpenMP optimizations.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_AlwaysInlineDeviceFunctions : Option<
    "openmp-opt-inline-device",
    "Inline all applicable functions on the device.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_EnableVerboseRemarks : Option<
    "openmp-opt-verbose-remarks",
    "Enables more verbose remarks.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_OpenMPSetFixpointIterations : Option<
    "openmp-opt-max-iterations",
    "Maximal number of attributor iterations.",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "256";
  let Hidden = 1;
}

def IPO_SharedMemoryLimit : Option<
    "openmp-opt-shared-limit",
    "Maximum amount of shared memory to use.",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "std::numeric_limits<unsigned>::max()";
  let Hidden = 1;
}

// =========================================================================
// Options -- PartialInlining.cpp
// =========================================================================

def IPO_DisablePartialInlining : Option<
    "disable-partial-inlining",
    "Disable partial inlining",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_DisableMultiRegionPartialInline : Option<
    "disable-mr-partial-inlining",
    "Disable multi-region partial inlining",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_ForceLiveExit : Option<
    "pi-force-live-exit-outline",
    "Force outline regions with live exits",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_MarkOutlinedColdCC : Option<
    "pi-mark-coldcc",
    "Mark outline function calls with ColdCC",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_SkipCostAnalysis : Option<
    "skip-partial-inlining-cost-analysis",
    "Skip Cost Analysis",
    "bool"> {
  let Registry = IPOOptsReg;
  let ReallyHidden = 1;
}

def IPO_MinRegionSizeRatio : Option<
    "min-region-size-ratio",
    "Minimum ratio comparing relative sizes of each "
    "outline candidate and original function",
    "float"> {
  let Registry = IPOOptsReg;
  let Default = "0.1";
  let Hidden = 1;
}

def IPO_MinBlockCounterExecution : Option<
    "min-block-execution",
    "Minimum block executions to consider "
    "its BranchProbabilityInfo valid",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "100";
  let Hidden = 1;
}

def IPO_ColdBranchRatio : Option<
    "cold-branch-ratio",
    "Minimum BranchProbability to consider a region cold.",
    "float"> {
  let Registry = IPOOptsReg;
  let Default = "0.1";
  let Hidden = 1;
}

def IPO_MaxNumInlineBlocks : Option<
    "max-num-inline-blocks",
    "Max number of blocks to be partially inlined",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "5";
  let Hidden = 1;
}

def IPO_MaxNumPartialInlining : Option<
    "max-partial-inlining",
    "Max number of partial inlining. The default is unlimited",
    "int"> {
  let Registry = IPOOptsReg;
  let Default = "-1";
  let Hidden = 1;
}

def IPO_OutlineRegionFreqPercent : Option<
    "outline-region-freq-percent",
    "Relative frequency of outline region to "
    "the entry block",
    "int"> {
  let Registry = IPOOptsReg;
  let Default = "75";
  let Hidden = 1;
}

def IPO_ExtraOutliningPenalty : Option<
    "partial-inlining-extra-penalty",
    "A debug option to add additional penalty to the computed one.",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// =========================================================================
// Options -- SCCP.cpp
// =========================================================================

def IPO_FuncSpecMaxIters : Option<
    "funcspec-max-iters",
    "The maximum number of iterations function specialization is run",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "10";
  let Hidden = 1;
}

// =========================================================================
// Options -- SampleProfile.cpp
// =========================================================================

def IPO_SampleProfileFile : Option<
    "sample-profile-file",
    "Profile file loaded by -sample-profile",
    "std::string"> {
  let Registry = IPOOptsReg;
  let Default = "\"\"";
  let Hidden = 1;
  let ValueDesc = "filename";
}

def IPO_SampleProfileRemappingFile : Option<
    "sample-profile-remapping-file",
    "Profile remapping file loaded by -sample-profile",
    "std::string"> {
  let Registry = IPOOptsReg;
  let Default = "\"\"";
  let Hidden = 1;
  let ValueDesc = "filename";
}

def IPO_SalvageStaleProfile : Option<
    "salvage-stale-profile",
    "Salvage stale profile by fuzzy matching and use the remapped "
    "location for sample profile query.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_SalvageUnusedProfile : Option<
    "salvage-unused-profile",
    "Salvage unused profile by matching with new "
    "functions on call graph.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_ReportProfileStaleness : Option<
    "report-profile-staleness",
    "Compute and report stale profile statistical metrics.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_PersistProfileStaleness : Option<
    "persist-profile-staleness",
    "Compute stale profile statistical metrics and write it into the "
    "native object file(.llvm_stats section).",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_ProfileSampleAccurate : Option<
    "profile-sample-accurate",
    "If the sample profile is accurate, we will mark all un-sampled "
    "callsite and function as having 0 samples. Otherwise, treat "
    "un-sampled callsites and functions conservatively as unknown.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_ProfileSampleBlockAccurate : Option<
    "profile-sample-block-accurate",
    "If the sample profile is accurate, we will mark all un-sampled "
    "branches and calls as having 0 samples. Otherwise, treat "
    "them conservatively as unknown.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_ProfileAccurateForSymsInList : Option<
    "profile-accurate-for-symsinlist",
    "For symbols in profile symbol list, regard their profiles to "
    "be accurate. It may be overridden by profile-sample-accurate.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def IPO_ProfileMergeInlinee : Option<
    "sample-profile-merge-inlinee",
    "Merge past inlinee's profile to outline version if sample "
    "profile loader decided not to inline a call site. It will "
    "only be enabled when top-down order of profile loading is "
    "enabled.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def IPO_ProfileTopDownLoad : Option<
    "sample-profile-top-down-load",
    "Do profile annotation and inlining for functions in top-down "
    "order of call graph during sample profile loading. It only "
    "works for new pass manager.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def IPO_UseProfiledCallGraph : Option<
    "use-profiled-call-graph",
    "Process functions in a top-down order "
    "defined by the profiled call graph when "
    "-sample-profile-top-down-load is on.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def IPO_ProfileSizeInline : Option<
    "sample-profile-inline-size",
    "Inline cold call sites in profile loader if it's beneficial "
    "for code size.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_DisableSampleLoaderInlining : Option<
    "disable-sample-loader-inlining",
    "If true, artificially skip inline transformation in sample-loader "
    "pass, and merge (or scale) profiles (as configured by "
    "--sample-profile-merge-inlinee).",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_SortProfiledSCC : Option<
    "sort-profiled-scc-member",
    "Sort profiled recursion by edge weights.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def IPO_ProfileInlineGrowthLimit : Option<
    "sample-profile-inline-growth-limit",
    "The size growth ratio limit for proirity-based sample profile "
    "loader inlining.",
    "int"> {
  let Registry = IPOOptsReg;
  let Default = "12";
  let Hidden = 1;
}

def IPO_ProfileInlineLimitMin : Option<
    "sample-profile-inline-limit-min",
    "The lower bound of size growth limit for "
    "proirity-based sample profile loader inlining.",
    "int"> {
  let Registry = IPOOptsReg;
  let Default = "100";
  let Hidden = 1;
}

def IPO_ProfileInlineLimitMax : Option<
    "sample-profile-inline-limit-max",
    "The upper bound of size growth limit for "
    "proirity-based sample profile loader inlining.",
    "int"> {
  let Registry = IPOOptsReg;
  let Default = "10000";
  let Hidden = 1;
}

def IPO_SampleHotCallSiteThreshold : Option<
    "sample-profile-hot-inline-threshold",
    "Hot callsite threshold for proirity-based sample profile loader "
    "inlining.",
    "int"> {
  let Registry = IPOOptsReg;
  let Default = "3000";
  let Hidden = 1;
}

def IPO_SampleColdCallSiteThreshold : Option<
    "sample-profile-cold-inline-threshold",
    "Threshold for inlining cold callsites",
    "int"> {
  let Registry = IPOOptsReg;
  let Default = "45";
  let Hidden = 1;
}

def IPO_ProfileICPRelativeHotness : Option<
    "sample-profile-icp-relative-hotness",
    "Relative hotness percentage threshold for indirect "
    "call promotion in proirity-based sample profile loader inlining.",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "25";
  let Hidden = 1;
}

def IPO_ProfileICPRelativeHotnessSkip : Option<
    "sample-profile-icp-relative-hotness-skip",
    "Skip relative hotness check for ICP up to given number of targets.",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def IPO_HotFuncCutoffForStalenessError : Option<
    "hot-func-cutoff-for-staleness-error",
    "A function is considered hot for staleness error check if its "
    "total sample count is above the specified percentile",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "800000";
  let Hidden = 1;
}

def IPO_MinfuncsForStalenessError : Option<
    "min-functions-for-staleness-error",
    "Skip the check if the number of hot functions is smaller than "
    "the specified number.",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "50";
  let Hidden = 1;
}

def IPO_PrecentMismatchForStalenessError : Option<
    "precent-mismatch-for-staleness-error",
    "Reject the profile if the mismatch percent is higher than the "
    "given number.",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "80";
  let Hidden = 1;
}

def IPO_CallsitePrioritizedInline : Option<
    "sample-profile-prioritized-inline",
    "Use call site prioritized inlining for sample profile loader. "
    "Currently only CSSPGO is supported.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
}

def IPO_UsePreInlinerDecision : Option<
    "sample-profile-use-preinliner",
    "Use the preinliner decisions stored in profile context.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
}

def IPO_AllowRecursiveInline : Option<
    "sample-profile-recursive-inline",
    "Allow sample loader inliner to inline recursive calls.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
}

def IPO_RemoveProbeAfterProfileAnnotation : Option<
    "sample-profile-remove-probe",
    "Remove pseudo-probe after sample profile annotation.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_ProfileInlineReplayFile : Option<
    "sample-profile-inline-replay",
    "Optimization remarks file containing inline remarks to be replayed "
    "by inlining from sample profile loader.",
    "std::string"> {
  let Registry = IPOOptsReg;
  let Default = "\"\"";
  let Hidden = 1;
  let ValueDesc = "filename";
}

def IPO_ProfileInlineReplayScope : Option<
    "sample-profile-inline-replay-scope",
    "Whether inline replay should be applied to the entire "
    "Module or just the Functions (default) that are present as "
    "callers in remarks during sample profile inlining.",
    "ReplayInlinerScope"> {
  let Registry = IPOOptsReg;
  let Default = "Function";
  let Hidden = 1;
}

def IPO_ProfileInlineReplayFallback : Option<
    "sample-profile-inline-replay-fallback",
    "How sample profile inline replay treats sites that don't come "
    "from the replay. Original: defers to original advisor, "
    "AlwaysInline: inline all sites not in replay, NeverInline: "
    "inline no sites not in replay",
    "ReplayInlinerFallback"> {
  let Registry = IPOOptsReg;
  let Default = "Original";
  let Hidden = 1;
}

def IPO_ProfileInlineReplayFormat : Option<
    "sample-profile-inline-replay-format",
    "How sample profile inline replay file is formatted",
    "CallSiteFormatKind"> {
  let Registry = IPOOptsReg;
  let Default = "LineColumnDiscriminator";
  let Hidden = 1;
}

def IPO_MaxNumPromotions : Option<
    "sample-profile-icp-max-prom",
    "Max number of promotions for a single indirect "
    "call callsite in sample profile loader",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "3";
  let Hidden = 1;
}

def IPO_OverwriteExistingWeights : Option<
    "overwrite-existing-weights",
    "Ignore existing branch weights on IR and always overwrite.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_AnnotateSampleProfileInlinePhase : Option<
    "annotate-sample-profile-inline-phase",
    "Annotate LTO phase (prelink / postlink), or main (no LTO) for "
    "sample-profile inline pass name.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// =========================================================================
// Options -- SampleProfileMatcher.cpp
// =========================================================================

def IPO_FuncProfileSimilarityThreshold : Option<
    "func-profile-similarity-threshold",
    "Consider a profile matches a function if the similarity of their "
    "callee sequences is above the specified percentile.",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "80";
  let Hidden = 1;
}

def IPO_MinFuncCountForCGMatching : Option<
    "min-func-count-for-cg-matching",
    "The minimum number of basic blocks required for a function to "
    "run stale profile call graph matching.",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "5";
  let Hidden = 1;
}

def IPO_MinCallCountForCGMatching : Option<
    "min-call-count-for-cg-matching",
    "The minimum number of call anchors required for a function to "
    "run stale profile call graph matching.",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "3";
  let Hidden = 1;
}

def IPO_LoadFuncProfileforCGMatching : Option<
    "load-func-profile-for-cg-matching",
    "Load top-level profiles that the sample reader initially skipped for "
    "the call-graph matching (only meaningful for extended binary "
    "format)",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def IPO_SalvageUnusedProfileMaxFunctions : Option<
    "salvage-unused-profile-max-functions",
    "The maximum number of functions in a module, above which salvage "
    "unused profile will be skipped.",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "UINT_MAX";
  let Hidden = 1;
}

def IPO_SalvageStaleProfileMaxCallsites : Option<
    "salvage-stale-profile-max-callsites",
    "The maximum number of callsites in a function, above which stale "
    "profile matching will be skipped.",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "UINT_MAX";
  let Hidden = 1;
}

// =========================================================================
// Options -- SampleProfileProbe.cpp
// =========================================================================

def IPO_VerifyPseudoProbe : Option<
    "verify-pseudo-probe",
    "Do pseudo probe verification",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def IPO_VerifyPseudoProbeFuncList : ListOption<
    "verify-pseudo-probe-funcs",
    "The option to specify the name of the functions to verify.",
    "std::string"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
}

def IPO_UpdatePseudoProbe : Option<
    "update-pseudo-probe",
    "Update pseudo probe distribution factor",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// =========================================================================
// Options -- StripSymbols.cpp
// =========================================================================

def IPO_StripGlobalConstants : Option<
    "strip-global-constants",
    "Removes debug compile units which reference "
    "to non-existing global constants",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// =========================================================================
// Options -- WholeProgramDevirt.cpp
// =========================================================================

def IPO_WholeProgramDevirtSummaryAction : Option<
    "wholeprogramdevirt-summary-action",
    "What to do with the summary when running this pass",
    "PassSummaryAction"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
}

def IPO_WholeProgramDevirtReadSummary : Option<
    "wholeprogramdevirt-read-summary",
    "Read summary from given bitcode or YAML file before running pass",
    "std::string"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
}

def IPO_WholeProgramDevirtWriteSummary : Option<
    "wholeprogramdevirt-write-summary",
    "Write summary to given bitcode or YAML file after running pass. "
    "Output file format is deduced from extension: *.bc means writing "
    "bitcode, otherwise YAML",
    "std::string"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
}

def IPO_ClDevirtualizeSpeculatively : Option<
    "devirtualize-speculatively",
    "Enable speculative devirtualization optimization",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "0";
}

def IPO_ClThreshold : Option<
    "wholeprogramdevirt-branch-funnel-threshold",
    "Maximum number of call targets per "
    "call site to enable branch funnels",
    "unsigned"> {
  let Registry = IPOOptsReg;
  let Default = "10";
  let Hidden = 1;
}

def IPO_PrintSummaryDevirt : Option<
    "wholeprogramdevirt-print-index-based",
    "Print index-based devirtualization messages",
    "bool"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
}

def IPO_WholeProgramVisibility : Option<
    "whole-program-visibility",
    "Enable whole program visibility",
    "bool"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
}

def IPO_DisableWholeProgramVisibility : Option<
    "disable-whole-program-visibility",
    "Disable whole program visibility (overrides enabling options)",
    "bool"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
}

def IPO_SkipFunctionNames : ListOption<
    "wholeprogramdevirt-skip",
    "Prevent function(s) from being devirtualized",
    "std::string"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
  let CommaSeparated = 1;
}

def IPO_WholeProgramDevirtKeepUnreachableFunction : Option<
    "wholeprogramdevirt-keep-unreachable-function",
    "Regard unreachable functions as possible devirtualize targets.",
    "bool"> {
  let Registry = IPOOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def IPO_DevirtCheckMode : Option<
    "wholeprogramdevirt-check",
    "Type of checking for incorrect devirtualizations",
    "WPDCheckMode"> {
  let Registry = IPOOptsReg;
  let Hidden = 1;
}
