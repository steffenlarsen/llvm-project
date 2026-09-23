//===- PassesOptions.td - Option defs for the Passes library -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def PassesOptions : OptionsStruct<"llvm"> {}

// =========================================================================
// External enum: InliningAdvisorMode (from llvm/Analysis/InlineAdvisor.h)
// =========================================================================

def InliningAdvisorModeEnum : OptionEnum<"llvm::InliningAdvisorMode", [
    EnumMember<"Default", "default">,
    EnumMember<"Development", "development">,
    EnumMember<"Release", "release">]> {
  let External = 1;
  let Ident = "InliningAdvisorMode";
}

// =========================================================================
// External enum: AttributorRunOption (from llvm/Transforms/IPO/Attributor.h)
// =========================================================================

def AttributorRunOptionEnum : OptionEnum<"llvm::AttributorRunOption", [
    EnumMember<"FULL", "full">,
    EnumMember<"LIGHT", "light">,
    EnumMember<"MODULE", "module">,
    EnumMember<"MODULE_LIGHT", "module-light">,
    EnumMember<"CGSCC", "cgscc">,
    EnumMember<"CGSCC_LIGHT", "cgscc-light">,
    EnumMember<"NONE", "none">]> {
  let External = 1;
  let Ident = "AttributorRunOption";
}

// =========================================================================
// External enum: cl::boolOrDefault (from llvm/Support/BoolOrDefault.h)
// =========================================================================

def BoolOrDefaultEnum : OptionEnum<"llvm::cl::boolOrDefault", [
    EnumMember<"BOU_TRUE", "true">,
    EnumMember<"BOU_FALSE", "false">]> {
  let External = 1;
  let Ident = "BoolOrDefault";
}

// =========================================================================
// Options — PassBuilderPipelines.cpp
// =========================================================================

defm PAS_UseInlineAdvisor : EnumField<"enable-ml-inliner", InliningAdvisorModeEnum,
    "llvm::InliningAdvisorMode::Default",
    "Enable ML policy for inliner. Currently trained for -Oz only">;

defm PAS_EnablePGOInlineDeferral : BoolField<"enable-npm-pgo-inline-deferral",
    "true", "Enable inline deferral during PGO">;

defm PAS_EnableModuleInliner : BoolField<"enable-module-inliner",
    "false", "Enable module inliner">;

defm PAS_PerformMandatoryInliningsFirst : BoolField<"mandatory-inlining-first",
    "false",
    "Perform mandatory inlinings module-wide, before performing inlining">;

defm PAS_EnableEagerlyInvalidateAnalyses : BoolField<"eagerly-invalidate-analyses",
    "true", "Eagerly invalidate more analyses in default pipelines">;

defm PAS_EnableMergeFunctions : BoolField<"enable-merge-functions",
    "false", "Enable function merging as part of the optimization pipeline">;

defm PAS_EnablePostPGOLoopRotation : BoolField<"enable-post-pgo-loop-rotation",
    "true", "Run the loop rotation transformation after PGO instrumentation">;

defm PAS_EnableGlobalAnalyses : BoolField<"enable-global-analyses",
    "true", "Enable inter-procedural analyses">;

defm PAS_RunPartialInlining : BoolField<"enable-partial-inlining",
    "false", "Run Partial inlining pass">;

defm PAS_ExtraVectorizerPasses : BoolField<"extra-vectorizer-passes",
    "false", "Run cleanup optimization passes after vectorization">;

defm PAS_RunNewGVN : BoolField<"enable-newgvn",
    "false", "Run the NewGVN pass">;

defm PAS_EnableLoopInterchange : BoolField<"enable-loopinterchange",
    "true", "Enable the LoopInterchange Pass">;

defm PAS_EnableUnrollAndJam : BoolField<"enable-unroll-and-jam",
    "false", "Enable Unroll And Jam Pass">;

defm PAS_EnableLoopFlatten : BoolField<"enable-loop-flatten",
    "false", "Enable the LoopFlatten Pass">;

defm PAS_EnableDFAJumpThreading : BoolField<"enable-dfa-jump-thread",
    "true", "Enable DFA jump threading">;

defm PAS_EnableHotColdSplit : BoolField<"hot-cold-split",
    "false", "Enable hot-cold splitting pass">;

defm PAS_DisablePreInliner : BoolField<"disable-preinline",
    "false", "Disable pre-instrumentation inliner">;

defm PAS_PreInlineThreshold : ValueField<"preinline-threshold", "int", "75",
    "Control the amount of inlining in pre-instrumentation inliner "
    "(default = 75)">;

defm PAS_EnableGVNHoist : BoolField<"enable-gvn-hoist",
    "false", "Enable the GVN hoisting pass (default = off)">;

defm PAS_EnableGVNSink : BoolField<"enable-gvn-sink",
    "false", "Enable the GVN sinking pass (default = off)">;

defm PAS_EnableJumpTableToSwitch : BoolField<"enable-jump-table-to-switch",
    "true", "Enable JumpTableToSwitch pass (default = off)">;

defm PAS_EnableCHR : BoolField<"enable-chr",
    "true", "Enable control height reduction optimization (CHR)">;

defm PAS_FlattenedProfileUsed : BoolField<"flattened-profile-used",
    "false",
    "Indicate the sample profile being used is flattened, i.e., "
    "no inline hierarchy exists in the profile">;

defm PAS_EnableMatrix : BoolField<"enable-matrix",
    "false", "Enable lowering of the matrix intrinsics">;

defm PAS_EnableMergeICmps : BoolField<"enable-mergeicmps",
    "true", "Enable MergeICmps pass in the optimization pipeline">;

defm PAS_EnableConstraintElimination : BoolField<"enable-constraint-elimination",
    "true", "Enable pass to eliminate conditions based on linear constraints">;

defm PAS_AttributorRun : EnumField<"attributor-enable", AttributorRunOptionEnum,
    "llvm::AttributorRunOption::NONE",
    "Enable the attributor inter-procedural deduction pass">;

defm PAS_EnableSampledInstr : BoolField<"enable-sampled-instrumentation",
    "false", "Enable profile instrumentation sampling (default = off)">;

defm PAS_UseLoopVersioningLICM : BoolField<"enable-loop-versioning-licm",
    "false", "Enable the experimental Loop Versioning LICM pass">;

defm PAS_InstrumentColdFuncOnlyPath : ValueField<"instrument-cold-function-only-path",
    "std::string", "\"\"",
    "File path for cold function only instrumentation(requires use "
    "with --pgo-instrument-cold-function-only)">;

defm PAS_EnableDevirtualizeSpeculatively : BoolField<"enable-devirtualize-speculatively",
    "false", "Enable speculative devirtualization optimization">;

defm PAS_OptPipelineTriggerCrash : BoolField<"opt-pipeline-trigger-crash",
    "false", "Trigger crash in optimization pipeline">;

// =========================================================================
// Options — StandardInstrumentations.cpp
// =========================================================================

defm PAS_VerifyAnalysisInvalidation : BoolField<"verify-analysis-invalidation",
    "false", "">;

defm PAS_PrintChangedBefore : BoolField<"print-before-changed",
    "false", "Print before passes that change them">;

defm PAS_DotBinary : ValueField<"print-changed-dot-path", "std::string",
    "\"dot\"", "system dot used by change reporters">;

defm PAS_BeforeColour : ValueField<"dot-cfg-before-color", "std::string",
    "\"red\"", "Color for dot-cfg before elements">;

defm PAS_AfterColour : ValueField<"dot-cfg-after-color", "std::string",
    "\"forestgreen\"", "Color for dot-cfg after elements">;

defm PAS_CommonColour : ValueField<"dot-cfg-common-color", "std::string",
    "\"black\"", "Color for dot-cfg common elements">;

defm PAS_DotCfgDir : ValueField<"dot-cfg-dir", "std::string", "\"./\"",
    "Generate dot files into specified directory for changed IRs">;

defm PAS_PrintOnCrashPath : ValueField<"print-on-crash-path", "std::string",
    "\"\"", "Print the last form of the IR before crash to a file">;

defm PAS_PrintOnCrash : BoolField<"print-on-crash",
    "false",
    "Print the last form of the IR before crash "
    "(use -print-on-crash-path to dump to a file)">;

defm PAS_OptBisectPrintIRPath : ValueField<"opt-bisect-print-ir-path",
    "std::string", "\"\"",
    "Print IR to path when opt-bisect-limit is reached">;

defm PAS_PrintPassNumbers : BoolField<"print-pass-numbers",
    "false", "Print pass names and their ordinals">;

defm PAS_PrintBeforePassNumber : ListField<"print-before-pass-number",
    "unsigned",
    "Print IR before the passes with specified numbers as "
    "reported by print-pass-numbers">;

defm PAS_PrintAfterPassNumber : ListField<"print-after-pass-number",
    "unsigned",
    "Print IR after the passes with specified numbers as "
    "reported by print-pass-numbers">;

defm PAS_IRDumpDirectory : ValueField<"ir-dump-directory", "std::string",
    "\"\"",
    "If specified, IR printed using the "
    "-print-[before|after]{-all} options will be dumped into "
    "files in this directory rather than written to stderr">;

defm PAS_DroppedVarStats : BoolField<"dropped-variable-stats",
    "false", "Dump dropped debug variables stats">;

defm PAS_TestChanged : ValueField<"exec-on-ir-change", "std::string", "\"\"",
    "exe called with module IR after each pass that changes it">;

// =========================================================================
// Options — PassBuilder.cpp
// =========================================================================

defm PAS_PrintPipelinePasses : OptionalValueField<"print-pipeline-passes",
    "std::string", "std::string()",
    "Print string describing the pipeline (best-effort only).\n"
    "  - =text\tPrint a '-passes' compatible string describing the pipeline.\n"
    "  - =tree\tPrint a tree-like structure describing the pipeline.">;

// =========================================================================
// Options — RunCodeGen.cpp
// =========================================================================

defm PAS_ForceNewPMCodeGen : EnumField<"force-new-pm-codegen", BoolOrDefaultEnum,
    "llvm::cl::boolOrDefault::BOU_UNSET",
    "Whether to force the NewPM on/off. Not setting the "
    "option will default to what the target prefers.">;
