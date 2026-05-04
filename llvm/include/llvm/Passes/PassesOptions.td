//===- PassesOptions.td - CLV2 option defs for Passes -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def PassesOptsReg : Registry<"llvm::passes"> {
  // Getter bodies are compiled once, in PassesOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

// =========================================================================
// External enum: InliningAdvisorMode (from InlineAdvisor.h)
// =========================================================================

def InliningAdvisorModeDefault : EnumValue<"default",
    "Heuristics-based inliner version"> {
  let CppName = "Default";
}

def InliningAdvisorModeDevelopment : EnumValue<"development",
    "Use development mode (runtime-loadable model)"> {
  let CppName = "Development";
}

def InliningAdvisorModeRelease : EnumValue<"release",
    "Use release mode (AOT-compiled model)"> {
  let CppName = "Release";
}

def InliningAdvisorModeEnum : EnumType<"InliningAdvisorMode",
    [InliningAdvisorModeDefault, InliningAdvisorModeDevelopment,
     InliningAdvisorModeRelease]> {
  let External = 1;
}

// =========================================================================
// External enum: AttributorRunOption (from Attributor.h)
// =========================================================================

def AttributorRunOptionFull : EnumValue<"full",
    "enable all full attributor runs"> {
  let CppName = "FULL";
}

def AttributorRunOptionLight : EnumValue<"light",
    "enable all attributor-light runs"> {
  let CppName = "LIGHT";
}

def AttributorRunOptionModule : EnumValue<"module",
    "enable module-wide attributor runs"> {
  let CppName = "MODULE";
}

def AttributorRunOptionModuleLight : EnumValue<"module-light",
    "enable module-wide attributor-light runs"> {
  let CppName = "MODULE_LIGHT";
}

def AttributorRunOptionCgscc : EnumValue<"cgscc",
    "enable call graph SCC attributor runs"> {
  let CppName = "CGSCC";
}

def AttributorRunOptionCgsccLight : EnumValue<"cgscc-light",
    "enable call graph SCC attributor-light runs"> {
  let CppName = "CGSCC_LIGHT";
}

def AttributorRunOptionNone : EnumValue<"none",
    "disable attributor runs"> {
  let CppName = "NONE";
}

def AttributorRunOptionEnum : EnumType<"AttributorRunOption",
    [AttributorRunOptionFull, AttributorRunOptionLight,
     AttributorRunOptionModule, AttributorRunOptionModuleLight,
     AttributorRunOptionCgscc, AttributorRunOptionCgsccLight,
     AttributorRunOptionNone]> {
  let External = 1;
}

// =========================================================================
// Options — PassBuilderPipelines.cpp
// =========================================================================

def PAS_UseInlineAdvisor : Option<
    "enable-ml-inliner",
    "Enable ML policy for inliner. Currently trained for -Oz only",
    "InliningAdvisorMode"> {
  let Registry = PassesOptsReg;
  let Default = "default";
  let Hidden = 1;
}

def PAS_EnablePGOInlineDeferral : Option<
    "enable-npm-pgo-inline-deferral",
    "Enable inline deferral during PGO",
    "bool"> {
  let Registry = PassesOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def PAS_EnableModuleInliner : Option<
    "enable-module-inliner",
    "Enable module inliner",
    "bool"> {
  let Registry = PassesOptsReg;
  let Hidden = 1;
}

def PAS_PerformMandatoryInliningsFirst : Option<
    "mandatory-inlining-first",
    "Perform mandatory inlinings module-wide, before performing inlining",
    "bool"> {
  let Registry = PassesOptsReg;
  let Hidden = 1;
}

def PAS_EnableEagerlyInvalidateAnalyses : Option<
    "eagerly-invalidate-analyses",
    "Eagerly invalidate more analyses in default pipelines",
    "bool"> {
  let Registry = PassesOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def PAS_EnableMergeFunctions : Option<
    "enable-merge-functions",
    "Enable function merging as part of the optimization pipeline",
    "bool"> {
  let Registry = PassesOptsReg;
  let Hidden = 1;
}

def PAS_EnablePostPGOLoopRotation : Option<
    "enable-post-pgo-loop-rotation",
    "Run the loop rotation transformation after PGO instrumentation",
    "bool"> {
  let Registry = PassesOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def PAS_EnableGlobalAnalyses : Option<
    "enable-global-analyses",
    "Enable inter-procedural analyses",
    "bool"> {
  let Registry = PassesOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def PAS_RunPartialInlining : Option<
    "enable-partial-inlining",
    "Run Partial inlining pass",
    "bool"> {
  let Registry = PassesOptsReg;
  let Hidden = 1;
}

def PAS_ExtraVectorizerPasses : Option<
    "extra-vectorizer-passes",
    "Run cleanup optimization passes after vectorization",
    "bool"> {
  let Registry = PassesOptsReg;
  let Hidden = 1;
}

def PAS_RunNewGVN : Option<
    "enable-newgvn",
    "Run the NewGVN pass",
    "bool"> {
  let Registry = PassesOptsReg;
  let Hidden = 1;
}

def PAS_EnableLoopInterchange : Option<
    "enable-loopinterchange",
    "Enable the LoopInterchange Pass",
    "bool"> {
  let Registry = PassesOptsReg;
  let Hidden = 1;
}

def PAS_EnableUnrollAndJam : Option<
    "enable-unroll-and-jam",
    "Enable Unroll And Jam Pass",
    "bool"> {
  let Registry = PassesOptsReg;
  let Hidden = 1;
}

def PAS_EnableLoopFlatten : Option<
    "enable-loop-flatten",
    "Enable the LoopFlatten Pass",
    "bool"> {
  let Registry = PassesOptsReg;
  let Hidden = 1;
}

def PAS_EnableDFAJumpThreading : Option<
    "enable-dfa-jump-thread",
    "Enable DFA jump threading",
    "bool"> {
  let Registry = PassesOptsReg;
  let Hidden = 1;
}

def PAS_EnableHotColdSplit : Option<
    "hot-cold-split",
    "Enable hot-cold splitting pass",
    "bool"> {
  let Registry = PassesOptsReg;
}

def PAS_DisablePreInliner : Option<
    "disable-preinline",
    "Disable pre-instrumentation inliner",
    "bool"> {
  let Registry = PassesOptsReg;
  let Hidden = 1;
}

def PAS_PreInlineThreshold : Option<
    "preinline-threshold",
    "Control the amount of inlining in pre-instrumentation inliner "
    "(default = 75)",
    "int"> {
  let Registry = PassesOptsReg;
  let Default = "75";
  let Hidden = 1;
}

def PAS_EnableGVNHoist : Option<
    "enable-gvn-hoist",
    "Enable the GVN hoisting pass (default = off)",
    "bool"> {
  let Registry = PassesOptsReg;
}

def PAS_EnableGVNSink : Option<
    "enable-gvn-sink",
    "Enable the GVN sinking pass (default = off)",
    "bool"> {
  let Registry = PassesOptsReg;
}

def PAS_EnableJumpTableToSwitch : Option<
    "enable-jump-table-to-switch",
    "Enable JumpTableToSwitch pass (default = off)",
    "bool"> {
  let Registry = PassesOptsReg;
}

def PAS_EnableCHR : Option<
    "enable-chr",
    "Enable control height reduction optimization (CHR)",
    "bool"> {
  let Registry = PassesOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def PAS_FlattenedProfileUsed : Option<
    "flattened-profile-used",
    "Indicate the sample profile being used is flattened, i.e., "
    "no inline hierarchy exists in the profile",
    "bool"> {
  let Registry = PassesOptsReg;
  let Hidden = 1;
}

def PAS_EnableMatrix : Option<
    "enable-matrix",
    "Enable lowering of the matrix intrinsics",
    "bool"> {
  let Registry = PassesOptsReg;
  let Hidden = 1;
}

def PAS_EnableMergeICmps : Option<
    "enable-mergeicmps",
    "Enable MergeICmps pass in the optimization pipeline",
    "bool"> {
  let Registry = PassesOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def PAS_EnableConstraintElimination : Option<
    "enable-constraint-elimination",
    "Enable pass to eliminate conditions based on linear constraints",
    "bool"> {
  let Registry = PassesOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def PAS_AttributorRun : Option<
    "attributor-enable",
    "Enable the attributor inter-procedural deduction pass",
    "AttributorRunOption"> {
  let Registry = PassesOptsReg;
  let Default = "none";
  let Hidden = 1;
}

def PAS_EnableSampledInstr : Option<
    "enable-sampled-instrumentation",
    "Enable profile instrumentation sampling (default = off)",
    "bool"> {
  let Registry = PassesOptsReg;
  let Hidden = 1;
}

def PAS_UseLoopVersioningLICM : Option<
    "enable-loop-versioning-licm",
    "Enable the experimental Loop Versioning LICM pass",
    "bool"> {
  let Registry = PassesOptsReg;
  let Hidden = 1;
}

def PAS_InstrumentColdFuncOnlyPath : Option<
    "instrument-cold-function-only-path",
    "File path for cold function only instrumentation(requires use "
    "with --pgo-instrument-cold-function-only)",
    "std::string"> {
  let Registry = PassesOptsReg;
  let Hidden = 1;
}

def PAS_EnableDevirtualizeSpeculatively : Option<
    "enable-devirtualize-speculatively",
    "Enable speculative devirtualization optimization",
    "bool"> {
  let Registry = PassesOptsReg;
}

def PAS_OptPipelineTriggerCrash : Option<
    "opt-pipeline-trigger-crash",
    "Trigger crash in optimization pipeline",
    "bool"> {
  let Registry = PassesOptsReg;
  let Hidden = 1;
}

// =========================================================================
// Options — StandardInstrumentations.cpp
// =========================================================================

def PAS_VerifyAnalysisInvalidation : Option<
    "verify-analysis-invalidation", "",
    "bool"> {
  let Registry = PassesOptsReg;
  let Hidden = 1;
}

def PAS_PrintChangedBefore : Option<
    "print-before-changed",
    "Print before passes that change them",
    "bool"> {
  let Registry = PassesOptsReg;
  let Hidden = 1;
}

def PAS_DotBinary : Option<
    "print-changed-dot-path",
    "system dot used by change reporters",
    "std::string"> {
  let Registry = PassesOptsReg;
  let Default = "\"dot\"";
  let Hidden = 1;
}

def PAS_BeforeColour : Option<
    "dot-cfg-before-color",
    "Color for dot-cfg before elements",
    "std::string"> {
  let Registry = PassesOptsReg;
  let Default = "\"red\"";
  let Hidden = 1;
}

def PAS_AfterColour : Option<
    "dot-cfg-after-color",
    "Color for dot-cfg after elements",
    "std::string"> {
  let Registry = PassesOptsReg;
  let Default = "\"forestgreen\"";
  let Hidden = 1;
}

def PAS_CommonColour : Option<
    "dot-cfg-common-color",
    "Color for dot-cfg common elements",
    "std::string"> {
  let Registry = PassesOptsReg;
  let Default = "\"black\"";
  let Hidden = 1;
}

def PAS_DotCfgDir : Option<
    "dot-cfg-dir",
    "Generate dot files into specified directory for changed IRs",
    "std::string"> {
  let Registry = PassesOptsReg;
  let Default = "\"./\"";
  let Hidden = 1;
}

def PAS_PrintOnCrashPath : Option<
    "print-on-crash-path",
    "Print the last form of the IR before crash to a file",
    "std::string"> {
  let Registry = PassesOptsReg;
  let Hidden = 1;
}

def PAS_PrintOnCrash : Option<
    "print-on-crash",
    "Print the last form of the IR before crash "
    "(use -print-on-crash-path to dump to a file)",
    "bool"> {
  let Registry = PassesOptsReg;
  let Hidden = 1;
}

def PAS_OptBisectPrintIRPath : Option<
    "opt-bisect-print-ir-path",
    "Print IR to path when opt-bisect-limit is reached",
    "std::string"> {
  let Registry = PassesOptsReg;
  let Hidden = 1;
}

def PAS_PrintPassNumbers : Option<
    "print-pass-numbers",
    "Print pass names and their ordinals",
    "bool"> {
  let Registry = PassesOptsReg;
  let Hidden = 1;
}

def PAS_PrintBeforePassNumber : ListOption<
    "print-before-pass-number",
    "Print IR before the passes with specified numbers as "
    "reported by print-pass-numbers",
    "unsigned"> {
  let Registry = PassesOptsReg;
  let Hidden = 1;
  let CommaSeparated = 1;
}

def PAS_PrintAfterPassNumber : ListOption<
    "print-after-pass-number",
    "Print IR after the passes with specified numbers as "
    "reported by print-pass-numbers",
    "unsigned"> {
  let Registry = PassesOptsReg;
  let Hidden = 1;
  let CommaSeparated = 1;
}

def PAS_IRDumpDirectory : Option<
    "ir-dump-directory",
    "If specified, IR printed using the "
    "-print-[before|after]{-all} options will be dumped into "
    "files in this directory rather than written to stderr",
    "std::string"> {
  let Registry = PassesOptsReg;
  let ValueDesc = "filename";
  let Hidden = 1;
}

def PAS_DroppedVarStats : Option<
    "dropped-variable-stats",
    "Dump dropped debug variables stats",
    "bool"> {
  let Registry = PassesOptsReg;
  let Hidden = 1;
}

def PAS_TestChanged : Option<
    "exec-on-ir-change",
    "exe called with module IR after each pass that changes it",
    "std::string"> {
  let Registry = PassesOptsReg;
  let Hidden = 1;
}

// =========================================================================
// Options — PassBuilder.cpp
// =========================================================================

def PAS_PrintPipelinePasses : Option<
    "print-pipeline-passes",
    "Print string describing the pipeline (best-effort only).\n"
    "  - =text\tPrint a '-passes' compatible string describing the pipeline.\n"
    "  - =tree\tPrint a tree-like structure describing the pipeline.",
    "std::string"> {
  let Registry = PassesOptsReg;
  let ValueOptional = 1;
}
