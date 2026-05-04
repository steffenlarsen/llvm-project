//===- PollyOptions.td - CLV2 option defs for Polly -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def PollyOptsReg : Registry<"polly_opts">;

def PollyCat : OptionCategory<"Polly Options",
    "Configure the polly loop optimizer">;

// =========================================================================
// Inline enum types
// =========================================================================

// --- POLLY_AnalysisType (DependenceInfo.cpp) ---

def POLLY_AnalysisTypeValueBased : EnumValue<"value-based",
    "Exact dependences without transitive dependences"> {
  let CppName = "ValueBased";
}

def POLLY_AnalysisTypeMemoryBased : EnumValue<"memory-based",
    "Overapproximation of dependences"> {
  let CppName = "MemoryBased";
}

def POLLY_AnalysisTypeEnum : EnumType<"POLLY_AnalysisType",
    [POLLY_AnalysisTypeValueBased, POLLY_AnalysisTypeMemoryBased]>;

// --- POLLY_DependenceAnalysisLevel (DependenceInfo.cpp) ---

def POLLY_DependenceAnalysisLevelStatementWise : EnumValue<"statement-wise",
    "Statement-level analysis"> {
  let CppName = "StatementWise";
}

def POLLY_DependenceAnalysisLevelReferenceWise : EnumValue<"reference-wise",
    "Memory reference level analysis that distinguish"
    " accessed references in the same statement"> {
  let CppName = "ReferenceWise";
}

def POLLY_DependenceAnalysisLevelAccessWise : EnumValue<"access-wise",
    "Memory reference level analysis that distinguish"
    " access instructions in the same statement"> {
  let CppName = "AccessWise";
}

def POLLY_DependenceAnalysisLevelEnum : EnumType<"POLLY_DependenceAnalysisLevel",
    [POLLY_DependenceAnalysisLevelStatementWise,
     POLLY_DependenceAnalysisLevelReferenceWise,
     POLLY_DependenceAnalysisLevelAccessWise]>;

// --- POLLY_GranularityChoice (ScopBuilder.cpp) ---

def POLLY_GranularityChoiceBasicBlocks : EnumValue<"bb",
    "One statement per basic block"> {
  let CppName = "BasicBlocks";
}

def POLLY_GranularityChoiceScalarIndependence : EnumValue<"scalar-indep",
    "Scalar independence heuristic"> {
  let CppName = "ScalarIndependence";
}

def POLLY_GranularityChoiceStores : EnumValue<"store",
    "Store-level granularity"> {
  let CppName = "Stores";
}

def POLLY_GranularityChoiceEnum : EnumType<"POLLY_GranularityChoice",
    [POLLY_GranularityChoiceBasicBlocks,
     POLLY_GranularityChoiceScalarIndependence,
     POLLY_GranularityChoiceStores]>;

// --- POLLY_OverflowTrackingChoice (IslExprBuilder.cpp) ---

def POLLY_OverflowTrackingChoiceNever : EnumValue<"never",
    "Never track the overflow bit."> {
  let CppName = "Never";
}

def POLLY_OverflowTrackingChoiceRequest : EnumValue<"request",
    "Track the overflow bit if requested."> {
  let CppName = "Request";
}

def POLLY_OverflowTrackingChoiceAlways : EnumValue<"always",
    "Always track the overflow bit."> {
  let CppName = "Always";
}

def POLLY_OverflowTrackingChoiceEnum : EnumType<"POLLY_OverflowTrackingChoice",
    [POLLY_OverflowTrackingChoiceNever,
     POLLY_OverflowTrackingChoiceRequest,
     POLLY_OverflowTrackingChoiceAlways]>;

// --- POLLY_OpenMPBackend (IslNodeBuilder.cpp) ---

def POLLY_OpenMPBackendGNU : EnumValue<"GNU",
    "GNU OpenMP"> {
  let CppName = "GNU";
}

def POLLY_OpenMPBackendLLVM : EnumValue<"LLVM",
    "LLVM OpenMP"> {
  let CppName = "LLVM";
}

def POLLY_OpenMPBackendEnum : EnumType<"POLLY_OpenMPBackend",
    [POLLY_OpenMPBackendGNU, POLLY_OpenMPBackendLLVM]>;

// --- POLLY_SchedulingType (LoopGenerators.cpp) ---

def POLLY_SchedulingTypeStatic : EnumValue<"static",
    "Static scheduling"> {
  let CppName = "Static";
}

def POLLY_SchedulingTypeDynamic : EnumValue<"dynamic",
    "Dynamic scheduling"> {
  let CppName = "Dynamic";
}

def POLLY_SchedulingTypeGuided : EnumValue<"guided",
    "Guided scheduling"> {
  let CppName = "Guided";
}

def POLLY_SchedulingTypeRuntime : EnumValue<"runtime",
    "Runtime determined (OMP_SCHEDULE)"> {
  let CppName = "Runtime";
}

def POLLY_SchedulingTypeEnum : EnumType<"POLLY_SchedulingType",
    [POLLY_SchedulingTypeStatic, POLLY_SchedulingTypeDynamic,
     POLLY_SchedulingTypeGuided, POLLY_SchedulingTypeRuntime]>;

// --- POLLY_PassPositionChoice (RegisterPasses.cpp) ---

def POLLY_PassPositionChoiceEarly : EnumValue<"early",
    "Before everything"> {
  let CppName = "Early";
}

def POLLY_PassPositionChoiceBeforeVectorizer : EnumValue<"before-vectorizer",
    "Right before the vectorizer"> {
  let CppName = "BeforeVectorizer";
}

def POLLY_PassPositionChoiceEnum : EnumType<"POLLY_PassPositionChoice",
    [POLLY_PassPositionChoiceEarly,
     POLLY_PassPositionChoiceBeforeVectorizer]>;

// --- POLLY_OptimizerChoice (RegisterPasses.cpp) ---

def POLLY_OptimizerChoiceNone : EnumValue<"none",
    "No optimizer"> {
  let CppName = "None";
}

def POLLY_OptimizerChoiceISL : EnumValue<"isl",
    "The isl scheduling optimizer"> {
  let CppName = "ISL";
}

def POLLY_OptimizerChoiceEnum : EnumType<"POLLY_OptimizerChoice",
    [POLLY_OptimizerChoiceNone, POLLY_OptimizerChoiceISL]>;

// --- POLLY_CodeGenChoice (RegisterPasses.cpp) ---

def POLLY_CodeGenChoiceFull : EnumValue<"full",
    "AST and IR generation"> {
  let CppName = "Full";
}

def POLLY_CodeGenChoiceAST : EnumValue<"ast",
    "Only AST generation"> {
  let CppName = "AST";
}

def POLLY_CodeGenChoiceNone : EnumValue<"none",
    "No code generation"> {
  let CppName = "None";
}

def POLLY_CodeGenChoiceEnum : EnumType<"POLLY_CodeGenChoice",
    [POLLY_CodeGenChoiceFull, POLLY_CodeGenChoiceAST,
     POLLY_CodeGenChoiceNone]>;

// --- POLLY_VectorizerChoice (RegisterPasses.cpp) ---

def POLLY_VectorizerChoiceNone : EnumValue<"none",
    "No Vectorization"> {
  let CppName = "None";
}

def POLLY_VectorizerChoiceStripmine : EnumValue<"stripmine",
    "Strip-mine outer loops for the loop-vectorizer to trigger"> {
  let CppName = "Stripmine";
}

def POLLY_VectorizerChoiceEnum : EnumType<"POLLY_VectorizerChoice",
    [POLLY_VectorizerChoiceNone, POLLY_VectorizerChoiceStripmine]>;

// =========================================================================
// Analysis/DependenceInfo.cpp
// =========================================================================

def POLLY_DependencesComputeout : Option<
    "polly-dependences-computeout",
    "Bound the dependence analysis by a maximal amount of "
    "computational steps (0 means no bound)",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "500000";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_DisableLegality : Option<
    "disable-polly-legality",
    "Disable polly legality check",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_DependencesUseReductions : Option<
    "polly-dependences-use-reductions",
    "Exploit reductions in dependence analysis",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "1";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_DependencesAnalysisType : Option<
    "polly-dependences-analysis-type",
    "The kind of dependence analysis to use",
    "POLLY_AnalysisType"> {
  let Registry = PollyOptsReg;
  let Default = "value-based";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_DependencesAnalysisLevel : Option<
    "polly-dependences-analysis-level",
    "The level of dependence analysis",
    "POLLY_DependenceAnalysisLevel"> {
  let Registry = PollyOptsReg;
  let Default = "statement-wise";
  let Hidden = 1;
  let Category = PollyCat;
}

// =========================================================================
// Analysis/ScopBuilder.cpp
// =========================================================================

def POLLY_AnalyzeReadOnlyScalars : Option<
    "polly-analyze-read-only-scalars",
    "Model read-only scalar values in the scop description",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "1";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_AstGenComputeout : Option<
    "polly-astgen-computeout",
    "Bound the AST generation by a maximal number of ISL operations "
    "[0 means un-bounded]",
    "unsigned long"> {
  let Registry = PollyOptsReg;
  let Default = "3000000";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_AnalysisComputeout : Option<
    "polly-analysis-computeout",
    "Bound the scop analysis by a maximal amount of "
    "computational steps (0 means no bound)",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "800000";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_AllowDereferenceOfAllFunctionParams : Option<
    "polly-allow-dereference-of-all-function-parameters",
    "Treat all parameters to functions that are pointers as dereferencible."
    " This is useful for invariant load hoisting, since we can generate"
    " less runtime checks. This is only valid if all pointers to functions"
    " are always initialized, so that Polly can choose to hoist"
    " their loads. ",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_IgnoreInbounds : Option<
    "polly-ignore-inbounds",
    "Do not take inbounds assumptions at all",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_RtcMaxArraysPerGroup : Option<
    "polly-rtc-max-arrays-per-group",
    "The maximal number of arrays to compare in each alias group.",
    "unsigned"> {
  let Registry = PollyOptsReg;
  let Default = "20u";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_RtcMaxArrayDisjuncts : Option<
    "polly-rtc-max-array-disjuncts",
    "The maximal number of disjunts allowed in memory accesses to "
    "to build RTCs.",
    "unsigned"> {
  let Registry = PollyOptsReg;
  let Default = "8u";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_RtcMaxParameters : Option<
    "polly-rtc-max-parameters",
    "The maximal number of parameters allowed in RTCs.",
    "unsigned"> {
  let Registry = PollyOptsReg;
  let Default = "8u";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_UnprofitableScalarAccs : Option<
    "polly-unprofitable-scalar-accs",
    "Count statements with scalar accesses as not optimizable",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_Context : Option<
    "polly-context",
    "Provide additional constraints on the context parameters",
    "std::string"> {
  let Registry = PollyOptsReg;
  let ValueDesc = "isl parameter set";
  let Category = PollyCat;
}

def POLLY_DetectReductions : Option<
    "polly-detect-reductions",
    "Detect and exploit reductions",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "1";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_DisableMultiplicativeReductions : Option<
    "polly-disable-multiplicative-reductions",
    "Disable multiplicative reductions",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_StmtGranularity : Option<
    "polly-stmt-granularity",
    "Algorithm to use for splitting basic blocks into multiple statements",
    "POLLY_GranularityChoice"> {
  let Registry = PollyOptsReg;
  let Default = "scalar-indep";
  let Category = PollyCat;
}

// =========================================================================
// Analysis/ScopDetection.cpp
// =========================================================================

def POLLY_DetectProfitabilityMinPerLoopInsts : Option<
    "polly-detect-profitability-min-per-loop-insts",
    "The minimal number of per-loop instructions before a single loop "
    "region is considered profitable",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "100000000";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_ProcessUnprofitable : Option<
    "polly-process-unprofitable",
    "Process scops that are unlikely to benefit from Polly optimizations.",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

def POLLY_OnlyFunc : ListOption<
    "polly-only-func",
    "Only run on functions that match a regex. "
    "Multiple regexes can be comma separated. "
    "Scop detection will run on all functions that match "
    "ANY of the regexes provided.",
    "std::string"> {
  let Registry = PollyOptsReg;
  let CommaSeparated = 1;
  let Category = PollyCat;
}

def POLLY_IgnoreFunc : ListOption<
    "polly-ignore-func",
    "Ignore functions that match a regex. "
    "Multiple regexes can be comma separated. "
    "Scop detection will ignore all functions that match "
    "ANY of the regexes provided.",
    "std::string"> {
  let Registry = PollyOptsReg;
  let CommaSeparated = 1;
  let Category = PollyCat;
}

def POLLY_DetectFullFunctions : Option<
    "polly-detect-full-functions",
    "Allow the detection of full functions",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

def POLLY_OnlyRegion : Option<
    "polly-only-region",
    "Only run on certain regions (The provided identifier must "
    "appear in the name of the region's entry block",
    "std::string"> {
  let Registry = PollyOptsReg;
  let ValueDesc = "identifier";
  let Category = PollyCat;
}

def POLLY_IgnoreAliasing : Option<
    "polly-ignore-aliasing",
    "Ignore possible aliasing of the array bases",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_AllowUnsignedOperations : Option<
    "polly-allow-unsigned-operations",
    "Allow unsigned operations such as comparisons or zero-extends.",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "1";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_UseRuntimeAliasChecks : Option<
    "polly-use-runtime-alias-checks",
    "Use runtime alias checks to resolve possible aliasing.",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "1";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_Report : Option<
    "polly-report",
    "Print information about the activities of Polly",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

def POLLY_AllowDifferingElementTypes : Option<
    "polly-allow-differing-element-types",
    "Allow different element types for array accesses",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "1";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_AllowNonaffine : Option<
    "polly-allow-nonaffine",
    "Allow non affine access functions in arrays",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_AllowModrefCalls : Option<
    "polly-allow-modref-calls",
    "Allow functions with known modref behavior",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_AllowNonaffineBranches : Option<
    "polly-allow-nonaffine-branches",
    "Allow non affine conditions for branches",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "1";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_AllowNonaffineLoops : Option<
    "polly-allow-nonaffine-loops",
    "Allow non affine conditions for loops",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_DetectTrackFailures : Option<
    "polly-detect-track-failures",
    "Track failure strings in detecting scop regions",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "1";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_DetectKeepGoing : Option<
    "polly-detect-keep-going",
    "Do not fail on the first error.",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_Delinearize : Option<
    "polly-delinearize",
    "Delinearize array access functions",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "1";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_DetectVerify : Option<
    "polly-detect-verify",
    "Verify the detected SCoPs after each transformation",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_InvariantLoadHoisting : Option<
    "polly-invariant-load-hoisting",
    "Hoist invariant loads.",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_AllowErrorBlocks : Option<
    "polly-allow-error-blocks",
    "Allow to speculate on the execution of 'error blocks'.",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "1";
  let Hidden = 1;
  let Category = PollyCat;
}

// =========================================================================
// Analysis/ScopGraphPrinter.cpp
// =========================================================================

def POLLY_ViewOnly : Option<
    "polly-view-only",
    "Only view functions that match this pattern",
    "std::string"> {
  let Registry = PollyOptsReg;
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_ViewAll : Option<
    "polly-view-all",
    "Also show functions without any scops",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

// =========================================================================
// Analysis/ScopInfo.cpp
// =========================================================================

def POLLY_RemarksMinimal : Option<
    "polly-remarks-minimal",
    "Do not emit remarks about assumptions that are known",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_OnIslErrorAbort : Option<
    "polly-on-isl-error-abort",
    "Abort if an isl error is encountered",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "1";
  let Category = PollyCat;
}

def POLLY_PreciseInbounds : Option<
    "polly-precise-inbounds",
    "Take more precise inbounds assumptions (do not scale well)",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_IgnoreParameterBounds : Option<
    "polly-ignore-parameter-bounds",
    "Do not add parameter bounds and do no gist simplify sets accordingly",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_PreciseFoldAccesses : Option<
    "polly-precise-fold-accesses",
    "Fold memory accesses to model more possible delinearizations "
    "(does not scale well)",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_UseLlvmNames : Option<
    "polly-use-llvm-names",
    "Use LLVM-IR names when deriving statement names",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_PrintInstructions : Option<
    "polly-print-instructions",
    "Output instructions per ScopStmt",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_IslArg : ListOption<
    "polly-isl-arg",
    "Option passed to ISL",
    "std::string"> {
  let Registry = PollyOptsReg;
  let ValueDesc = "argument";
  let Category = PollyCat;
}

// =========================================================================
// CodeGen/BlockGenerators.cpp
// =========================================================================

def POLLY_EnableAligned : Option<
    "enable-polly-aligned",
    "Assumed aligned memory accesses.",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_CodegenAddDebugPrinting : Option<
    "polly-codegen-add-debug-printing",
    "Add printf calls that show the values loaded/stored.",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_CodegenTraceStmts : Option<
    "polly-codegen-trace-stmts",
    "Add printf calls that print the statement being executed",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_CodegenTraceScalars : Option<
    "polly-codegen-trace-scalars",
    "Add printf calls that print the values of all scalar values "
    "used in a statement. Requires -polly-codegen-trace-stmts.",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

// =========================================================================
// CodeGen/CodeGeneration.cpp
// =========================================================================

def POLLY_CodegenVerify : Option<
    "polly-codegen-verify",
    "Verify the function generated by Polly",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_CodegenPerfMonitoring : Option<
    "polly-codegen-perf-monitoring",
    "Add run-time performance monitoring",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

// =========================================================================
// CodeGen/IslAst.cpp
// =========================================================================

def POLLY_Parallel : Option<
    "polly-parallel",
    "Generate thread parallel code (isl codegen only)",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

def POLLY_AstPrintAccesses : Option<
    "polly-ast-print-accesses",
    "Print memory access functions",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

def POLLY_ParallelForce : Option<
    "polly-parallel-force",
    "Force generation of thread parallel code ignoring any cost model",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

def POLLY_AstUseContext : Option<
    "polly-ast-use-context",
    "Use context",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "1";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_AstDetectParallel : Option<
    "polly-ast-detect-parallel",
    "Detect parallelism",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_PrintAst : Option<
    "polly-print-ast",
    "Print the ISL abstract syntax tree",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

// =========================================================================
// CodeGen/IslExprBuilder.cpp
// =========================================================================

def POLLY_OverflowTracking : Option<
    "polly-overflow-tracking",
    "Define where potential integer overflows in generated "
    "expressions should be tracked.",
    "POLLY_OverflowTrackingChoice"> {
  let Registry = PollyOptsReg;
  let Default = "request";
  let Hidden = 1;
  let Category = PollyCat;
}

// =========================================================================
// CodeGen/IslNodeBuilder.cpp
// =========================================================================

def POLLY_CodegenEmitRtcPrint : Option<
    "polly-codegen-emit-rtc-print",
    "Emit code that prints the runtime check result dynamically.",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_CodegenGenerateExpressions : Option<
    "polly-codegen-generate-expressions",
    "Generate AST expressions for unmodified and modified accesses",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_TargetFirstLevelCacheLineSize : Option<
    "polly-target-first-level-cache-line-size",
    "The size of the first level cache line size specified in bytes.",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "64";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_OmpBackend : Option<
    "polly-omp-backend",
    "Choose the OpenMP library to use:",
    "POLLY_OpenMPBackend"> {
  let Registry = PollyOptsReg;
  let Default = "GNU";
  let Hidden = 1;
  let Category = PollyCat;
}

// =========================================================================
// CodeGen/LoopGenerators.cpp
// =========================================================================

def POLLY_NumThreads : Option<
    "polly-num-threads",
    "Number of threads to use (0 = auto)",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_AnnotateMetadataVectorize : Option<
    "polly-annotate-metadata-vectorize",
    "Append vectorize enable/disable metadata from polly",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

def POLLY_Scheduling : Option<
    "polly-scheduling",
    "Scheduling type of parallel OpenMP for loops",
    "POLLY_SchedulingType"> {
  let Registry = PollyOptsReg;
  let Default = "runtime";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_SchedulingChunksize : Option<
    "polly-scheduling-chunksize",
    "Chunksize to use by the OpenMP runtime calls",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

// =========================================================================
// Exchange/JSONExporter.cpp
// =========================================================================

def POLLY_PrintImportJscop : Option<
    "polly-print-import-jscop",
    "Polly - Print Scop import result",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

def POLLY_ImportJscopDir : Option<
    "polly-import-jscop-dir",
    "The directory to import the .jscop files from.",
    "std::string"> {
  let Registry = PollyOptsReg;
  let Default = "\".\"";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_ImportJscopPostfix : Option<
    "polly-import-jscop-postfix",
    "Postfix to append to the import .jsop files.",
    "std::string"> {
  let Registry = PollyOptsReg;
  let Hidden = 1;
  let Category = PollyCat;
}

// =========================================================================
// Support/PollyDebug.cpp
// =========================================================================

def POLLY_Debug : Option<
    "polly-debug",
    "Enable debug output for only polly passes.",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

// =========================================================================
// Support/RegisterPasses.cpp
// =========================================================================

def POLLY_Enabled : Option<
    "polly",
    "Enable the polly optimizer (with -O1, -O2 or -O3)",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

def POLLY_OnlyScopDetection : Option<
    "polly-only-scop-detection",
    "Only run scop detection, but no other optimizations",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

def POLLY_Position : Option<
    "polly-position",
    "Where to run polly in the pass pipeline",
    "POLLY_PassPositionChoice"> {
  let Registry = PollyOptsReg;
  let Default = "before-vectorizer";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_Optimizer : Option<
    "polly-optimizer",
    "Select the scheduling optimizer",
    "POLLY_OptimizerChoice"> {
  let Registry = PollyOptsReg;
  let Default = "isl";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_CodeGeneration : Option<
    "polly-code-generation",
    "How much code-generation to perform",
    "POLLY_CodeGenChoice"> {
  let Registry = PollyOptsReg;
  let Default = "full";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_Vectorizer : Option<
    "polly-vectorizer",
    "Select the vectorization strategy",
    "POLLY_VectorizerChoice"> {
  let Registry = PollyOptsReg;
  let Default = "none";
  let Category = PollyCat;
}

def POLLY_Import : Option<
    "polly-import",
    "Import the polyhedral description of the detected Scops",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_EnableMse : Option<
    "polly-enable-mse",
    "Fully expand the memory accesses of the detected Scops",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_Export : Option<
    "polly-export",
    "Export the polyhedral description of the detected Scops",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_RunDce : Option<
    "polly-run-dce",
    "Run the dead code elimination",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_Show : Option<
    "polly-show",
    "Highlight the code regions that will be optimized in a "
    "(CFG BBs and LLVM-IR instructions)",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

def POLLY_ShowOnly : Option<
    "polly-show-only",
    "Highlight the code regions that will be optimized in a "
    "(CFG only BBs)",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

def POLLY_Dot : Option<
    "polly-dot",
    "Enable the Polly DOT printer in -O3",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_DotOnly : Option<
    "polly-dot-only",
    "Enable the Polly DOT printer in -O3 (no BB content)",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_ViewCfg : Option<
    "polly-view-cfg",
    "Show the Polly CFG right after code generation",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_EnableOptree : Option<
    "polly-enable-optree",
    "Enable operand tree forwarding",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "1";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_DumpBefore : Option<
    "polly-dump-before",
    "Dump module before Polly transformations into a file "
    "suffixed with \"-before\"",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

def POLLY_DumpBeforeFile : ListOption<
    "polly-dump-before-file",
    "Dump module before Polly transformations to the given file",
    "std::string"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

def POLLY_DumpAfter : Option<
    "polly-dump-after",
    "Dump module after Polly transformations into a file "
    "suffixed with \"-after\"",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

def POLLY_DumpAfterFile : ListOption<
    "polly-dump-after-file",
    "Dump module after Polly transformations to the given file",
    "std::string"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

def POLLY_EnableDelicm : Option<
    "polly-enable-delicm",
    "Eliminate scalar loop carried dependences",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "1";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_EnableSimplify : Option<
    "polly-enable-simplify",
    "Simplify SCoP after optimizations",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "1";
  let Category = PollyCat;
}

def POLLY_EnablePruneUnprofitable : Option<
    "polly-enable-prune-unprofitable",
    "Bail out on unprofitable SCoPs before rescheduling",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "1";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_PrintDetect : Option<
    "polly-print-detect",
    "Polly - Print static control parts (SCoPs)",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

def POLLY_PrintScops : Option<
    "polly-print-scops",
    "Print polyhedral description of all regions",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

def POLLY_PrintDeps : Option<
    "polly-print-deps",
    "Polly - Print dependences",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

// =========================================================================
// Support/SCEVAffinator.cpp
// =========================================================================

def POLLY_IgnoreIntegerWrapping : Option<
    "polly-ignore-integer-wrapping",
    "Do not build run-time checks to proof absence of integer wrapping",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

// =========================================================================
// Support/ScopHelper.cpp
// =========================================================================

def POLLY_DebugFunc : ListOption<
    "polly-debug-func",
    "Allow calls to the specified functions in SCoPs even if their "
    "side-effects are unknown. This can be used to do debug output in "
    "Polly-transformed code.",
    "std::string"> {
  let Registry = PollyOptsReg;
  let Hidden = 1;
  let CommaSeparated = 1;
  let Category = PollyCat;
}

// =========================================================================
// Transform/Canonicalization.cpp
// =========================================================================

def POLLY_RunInliner : Option<
    "polly-run-inliner",
    "Run an early inliner pass before Polly",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

// =========================================================================
// Transform/DeadCodeElimination.cpp
// =========================================================================

def POLLY_DcePreciseSteps : Option<
    "polly-dce-precise-steps",
    "The number of precise steps between two approximating "
    "iterations. (A value of -1 schedules another approximation stage "
    "before the actual dead code elimination.",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "-1";
  let Category = PollyCat;
}

// =========================================================================
// Transform/DeLICM.cpp
// =========================================================================

def POLLY_PrintDelicm : Option<
    "polly-print-delicm",
    "Polly - Print DeLICM/DePRE",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

def POLLY_DelicmMaxOps : Option<
    "polly-delicm-max-ops",
    "Maximum number of isl operations to invest for lifetime analysis; "
    "0=no limit",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "1000000";
  let Category = PollyCat;
}

def POLLY_DelicmOverapproximateWrites : Option<
    "polly-delicm-overapproximate-writes",
    "Do more PHI writes than necessary in order to avoid partial accesses",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_DelicmPartialWrites : Option<
    "polly-delicm-partial-writes",
    "Allow partial writes",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "1";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_DelicmComputeKnown : Option<
    "polly-delicm-compute-known",
    "Compute known content of array elements",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "1";
  let Hidden = 1;
  let Category = PollyCat;
}

// =========================================================================
// Transform/FlattenSchedule.cpp
// =========================================================================

def POLLY_PrintFlattenSchedule : Option<
    "polly-print-flatten-schedule",
    "A polly pass",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

// =========================================================================
// Transform/ForwardOpTree.cpp
// =========================================================================

def POLLY_OptreeAnalyzeKnown : Option<
    "polly-optree-analyze-known",
    "Analyze array contents for load forwarding",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "1";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_OptreeNormalizePhi : Option<
    "polly-optree-normalize-phi",
    "Replace PHIs by their incoming values",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_OptreeMaxOps : Option<
    "polly-optree-max-ops",
    "Maximum number of ISL operations to invest for known analysis; "
    "0=no limit",
    "unsigned"> {
  let Registry = PollyOptsReg;
  let Default = "1000000u";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_PrintOptree : Option<
    "polly-print-optree",
    "Polly - Print forward operand tree result",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

// =========================================================================
// Transform/ManualOptimizer.cpp
// =========================================================================

def POLLY_PragmaIgnoreDepcheck : Option<
    "polly-pragma-ignore-depcheck",
    "Skip the dependency check for pragma-based transformations",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

// =========================================================================
// Transform/MatmulOptimizer.cpp
// =========================================================================

def POLLY_TargetLatencyVectorFma : Option<
    "polly-target-latency-vector-fma",
    "The minimal number of cycles between issuing two "
    "dependent consecutive vector fused multiply-add instructions.",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "8";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_TargetThroughputVectorFma : Option<
    "polly-target-throughput-vector-fma",
    "A throughput of the processor floating-point arithmetic units "
    "expressed in the number of vector fused multiply-add "
    "instructions per clock cycle.",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "1";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_Target1stCacheLevelSize : Option<
    "polly-target-1st-cache-level-size",
    "The size of the first cache level specified in bytes.",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "-1";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_Target1stCacheLevelDefaultSize : Option<
    "polly-target-1st-cache-level-default-size",
    "The default size of the first cache level specified in bytes "
    "(if not enough were provided by the TargetTransformInfo).",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "32768";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_Target2ndCacheLevelSize : Option<
    "polly-target-2nd-cache-level-size",
    "The size of the second level specified in bytes.",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "-1";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_Target2ndCacheLevelDefaultSize : Option<
    "polly-target-2nd-cache-level-default-size",
    "The default size of the second cache level specified in bytes "
    "(if not enough were provided by the TargetTransformInfo).",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "262144";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_Target1stCacheLevelAssociativity : Option<
    "polly-target-1st-cache-level-associativity",
    "The associativity of the first cache level.",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "-1";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_Target1stCacheLevelDefaultAssociativity : Option<
    "polly-target-1st-cache-level-default-associativity",
    "The default associativity of the first cache level "
    "(if not enough were provided by the TargetTransformInfo).",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "8";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_Target2ndCacheLevelAssociativity : Option<
    "polly-target-2nd-cache-level-associativity",
    "The associativity of the second cache level.",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "-1";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_Target2ndCacheLevelDefaultAssociativity : Option<
    "polly-target-2nd-cache-level-default-associativity",
    "The default associativity of the second cache level "
    "(if not enough were provided by the TargetTransformInfo).",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "8";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_TargetVectorRegisterBitwidth : Option<
    "polly-target-vector-register-bitwidth",
    "The size in bits of a vector register (if not set, this "
    "information is taken from LLVM's target information.",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "-1";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_PatternMatchingNcQuotient : Option<
    "polly-pattern-matching-nc-quotient",
    "Quotient that is obtained by dividing Nc, the parameter of the "
    "macro-kernel, by Nr, the parameter of the micro-kernel",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "256";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_TcOpt : Option<
    "polly-tc-opt",
    "Perform optimizations of tensor contractions based on pattern matching",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

def POLLY_MatmulOpt : Option<
    "polly-matmul-opt",
    "Perform optimizations of matrix multiplications based on "
    "pattern matching",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "1";
  let Category = PollyCat;
}

def POLLY_TcDependencesComputeout : Option<
    "polly-tc-dependences-computeout",
    "Bound the dependence analysis by a maximal amount of "
    "computational steps (0 means no bound)",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "500000";
  let Hidden = 1;
  let Category = PollyCat;
}

// =========================================================================
// Transform/MaximalStaticExpansion.cpp
// =========================================================================

def POLLY_PrintMse : Option<
    "polly-print-mse",
    "Polly - Print Maximal static expansion of SCoP",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

// =========================================================================
// Transform/ScheduleOptimizer.cpp
// =========================================================================

def POLLY_OptOptimizeOnly : Option<
    "polly-opt-optimize-only",
    "Only a certain kind of dependences (all/raw)",
    "std::string"> {
  let Registry = PollyOptsReg;
  let Default = "\"all\"";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_OptSimplifyDeps : Option<
    "polly-opt-simplify-deps",
    "Dependences should be simplified (yes/no)",
    "std::string"> {
  let Registry = PollyOptsReg;
  let Default = "\"yes\"";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_OptMaxConstantTerm : Option<
    "polly-opt-max-constant-term",
    "The maximal constant term allowed (-1 is unlimited)",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "20";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_OptMaxCoefficient : Option<
    "polly-opt-max-coefficient",
    "The maximal coefficient allowed (-1 is unlimited)",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "20";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_OptMaximizeBands : Option<
    "polly-opt-maximize-bands",
    "Maximize the band depth (yes/no)",
    "std::string"> {
  let Registry = PollyOptsReg;
  let Default = "\"yes\"";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_ScheduleComputeout : Option<
    "polly-schedule-computeout",
    "Bound the scheduler by maximal amount of computational steps.",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "300000";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_LoopfusionGreedy : Option<
    "polly-loopfusion-greedy",
    "Aggressively try to fuse everything",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_OptOuterCoincidence : Option<
    "polly-opt-outer-coincidence",
    "Try to construct schedules where the outer member of each band "
    "satisfies the coincidence constraints (yes/no)",
    "std::string"> {
  let Registry = PollyOptsReg;
  let Default = "\"no\"";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_PrevectWidth : Option<
    "polly-prevect-width",
    "The number of loop iterations to strip-mine for pre-vectorization",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "4";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_Tiling : Option<
    "polly-tiling",
    "Enable loop tiling",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "1";
  let Category = PollyCat;
}

def POLLY_DefaultTileSize : Option<
    "polly-default-tile-size",
    "The default tile size "
    "(if not enough were provided by --polly-tile-sizes)",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "32";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_TileSizes : ListOption<
    "polly-tile-sizes",
    "A tile size for each loop dimension, filled with "
    "--polly-default-tile-size",
    "int"> {
  let Registry = PollyOptsReg;
  let Hidden = 1;
  let CommaSeparated = 1;
  let Category = PollyCat;
}

def POLLY_2ndLevelTiling : Option<
    "polly-2nd-level-tiling",
    "Enable a 2nd level loop of loop tiling",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

def POLLY_2ndLevelDefaultTileSize : Option<
    "polly-2nd-level-default-tile-size",
    "The default 2nd-level tile size "
    "(if not enough were provided by --polly-2nd-level-tile-sizes)",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "16";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_2ndLevelTileSizes : ListOption<
    "polly-2nd-level-tile-sizes",
    "A tile size for each loop dimension, filled with "
    "--polly-default-tile-size",
    "int"> {
  let Registry = PollyOptsReg;
  let Hidden = 1;
  let CommaSeparated = 1;
  let Category = PollyCat;
}

def POLLY_RegisterTiling : Option<
    "polly-register-tiling",
    "Enable register tiling",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

def POLLY_RegisterTilingDefaultTileSize : Option<
    "polly-register-tiling-default-tile-size",
    "The default register tile size "
    "(if not enough were provided by --polly-register-tile-sizes)",
    "int"> {
  let Registry = PollyOptsReg;
  let Default = "2";
  let Hidden = 1;
  let Category = PollyCat;
}

def POLLY_RegisterTileSizes : ListOption<
    "polly-register-tile-sizes",
    "A tile size for each loop dimension, filled with "
    "--polly-register-tile-size",
    "int"> {
  let Registry = PollyOptsReg;
  let Hidden = 1;
  let CommaSeparated = 1;
  let Category = PollyCat;
}

def POLLY_PragmaBasedOpts : Option<
    "polly-pragma-based-opts",
    "Apply user-directed transformation from metadata",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "1";
  let Category = PollyCat;
}

def POLLY_Reschedule : Option<
    "polly-reschedule",
    "Optimize SCoPs using ISL",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "1";
  let Category = PollyCat;
}

def POLLY_PatternMatchingBasedOpts : Option<
    "polly-pattern-matching-based-opts",
    "Perform optimizations based on pattern matching",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "1";
  let Category = PollyCat;
}

def POLLY_Postopts : Option<
    "polly-postopts",
    "Apply post-rescheduling optimizations such as tiling "
    "(requires -polly-reschedule)",
    "bool"> {
  let Registry = PollyOptsReg;
  let Default = "1";
  let Category = PollyCat;
}

def POLLY_OptimizedScops : Option<
    "polly-optimized-scops",
    "Polly - Dump polyhedral description of Scops optimized with "
    "the isl scheduling optimizer and the set of post-scheduling "
    "transformations is applied on the schedule tree",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

def POLLY_PrintOptIsl : Option<
    "polly-print-opt-isl",
    "A polly pass",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}

// =========================================================================
// Transform/Simplify.cpp
// =========================================================================

def POLLY_PrintSimplify : Option<
    "polly-print-simplify",
    "Polly - Print Simplify actions",
    "bool"> {
  let Registry = PollyOptsReg;
  let Category = PollyCat;
}
