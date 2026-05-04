//===- optdriver.cpp - The LLVM Modular Optimizer -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Optimizations may be specified an arbitrary number of times on the command
// line, They are run in the order specified. Common driver library for re-use
// by potential downstream opt-variants.
//
//===----------------------------------------------------------------------===//

#include "NewPMDriver.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Analysis/AnalysisOptionsRegistration.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/RegionPass.h"
#include "llvm/Analysis/RuntimeLibcallInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/AsmParser/AsmParserOptionsRegistration.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/Bitcode/BitcodeOptionsRegistration.h"
#include "llvm/CGData/CGDataOptionsRegistration.h"
#include "llvm/CodeGen/CodeGenOptionsRegistration.h"
#include "llvm/Config/Targets.h"
#if LLVM_HAS_ARC_TARGET
#include "llvm/Target/ARC/ARCOptionsOptInfos.h"
#endif
#if LLVM_HAS_CSKY_TARGET
#include "llvm/Target/CSKY/CSKYOptionsOptInfos.h"
#endif
#if LLVM_HAS_M68K_TARGET
#include "llvm/Target/M68k/M68kOptionsOptInfos.h"
#endif
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/CodeGen/CommandFlagsOptInfos.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Frontend/OpenMP/OpenMPOptionsRegistration.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/IROptionsRegistration.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LLVMRemarkStreamer.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/InitializePasses.h"
#include "llvm/LTO/LTOOptionsRegistration.h"
#include "llvm/LinkAllIR.h"
#include "llvm/LinkAllPasses.h"
#include "llvm/MC/MCOptionsRegistration.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/MCTargetOptionsCommandFlags.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/ObjectOptionsRegistration.h"
#include "llvm/Passes/PassesOptionsRegistration.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/ProfileData/ProfileDataOptionsRegistration.h"
#include "llvm/Remarks/RemarksOptionsRegistration.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/PluginLoader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/SupportOptions.h"
#include "llvm/Support/SupportOptionsOptInfos.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptionsRegistration.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/AggressiveInstCombine/AggressiveInstCombineOptionsRegistration.h"
#include "llvm/Transforms/Coroutines/CoroutinesOptionsRegistration.h"
#include "llvm/Transforms/IPO/IPOOptionsRegistration.h"
#include "llvm/Transforms/IPO/WholeProgramDevirt.h"
#include "llvm/Transforms/InstCombine/InstCombineOptionsRegistration.h"
#include "llvm/Transforms/Instrumentation/InstrumentationOptionsRegistration.h"
#include "llvm/Transforms/ObjCARC/ObjCARCOptionsRegistration.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/ScalarOptionsOptInfos.h"
#include "llvm/Transforms/Scalar/ScalarOptionsRegistration.h"
#include "llvm/Transforms/Utils/AssignGUID.h"
#include "llvm/Transforms/Utils/UtilsOptionsRegistration.h"
#include "llvm/Transforms/Vectorize/VectorizeOptions.h"
#include "llvm/Transforms/Vectorize/VectorizeOptionsRegistration.h"
#include <deque>
#ifdef LINK_POLLY_INTO_TOOLS
#include "polly/PollyOptionsOptInfos.h"
#endif
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Debugify.h"
#include <algorithm>
#include <memory>
#include <optional>
using namespace llvm;
using namespace opt_tool;
using namespace clv2;

//===----------------------------------------------------------------------===//
// clv2 option descriptors for opt
//===----------------------------------------------------------------------===//

// This flag specifies a textual description of the optimization pass pipeline
// to run over the module.
inline constexpr OptionInfo<std::string> PassPipeline{
    "passes",
    "A textual (comma separated) description of the pass pipeline e.g.,"
    "-passes=\"foo,bar\", to have analysis passes available before a pass, "
    "add \"require<foo-analysis>\". See "
    "https://llvm.org/docs/NewPassManager.html#invoking-opt "
    "for more details on the pass pipeline syntax. "};

inline constexpr AliasInfo PassPipeline2{"p", "passes"};

inline constexpr OptionInfo<bool> PrintPasses{
    "print-passes",
    "Print available passes that can be specified in -passes=foo and exit"};

// --print-options / --print-all-options are clv2 builtins (see
// buildBuiltinEntries); opt used to redeclare them here as bools that nothing
// ever read, which shadowed the builtins and made both flags silent no-ops.

inline constexpr OptionInfo<std::string> InputFilenameOpt{
    "input", "<input bitcode file>", Positional{}};

inline constexpr OptionInfo<std::string> OutputFilename{
    "o", "Override output filename", value_desc("filename")};

inline constexpr OptionInfo<bool> Force{"f",
                                        "Enable binary output on terminals"};

inline constexpr OptionInfo<bool> NoOutput{
    "disable-output", "Do not write result bitcode file", Hidden};

inline constexpr OptionInfo<bool> OutputAssembly{
    "S", "Write output as LLVM assembly"};

inline constexpr OptionInfo<bool> OutputThinLTOBC{
    "thinlto-bc", "Write output as ThinLTO-ready bitcode"};

inline constexpr OptionInfo<bool> SplitLTOUnit{
    "thinlto-split-lto-unit", "Enable splitting of a ThinLTO LTOUnit"};

inline constexpr OptionInfo<bool> UnifiedLTO{
    "unified-lto",
    "Use unified LTO piplines. Ignored unless -thinlto-bc is also specified.",
    Hidden};

inline constexpr OptionInfo<std::string> ThinLinkBitcodeFile{
    "thin-link-bitcode-file",
    "A file in which to write minimized bitcode for the thin link only",
    value_desc("filename")};

inline constexpr OptionInfo<bool> NoVerify{"disable-verify",
                                           "Do not run the verifier", Hidden};

inline constexpr OptionInfo<bool> NoUpgradeDebugInfo{
    "disable-upgrade-debug-info", "Generate invalid output", ReallyHidden};

inline constexpr OptionInfo<bool> VerifyEach{"verify-each",
                                             "Verify after each transform"};

inline constexpr OptionInfo<bool> DisableDITypeMap{
    "disable-debug-info-type-map",
    "Don't use a uniquing type map for debug info"};

inline constexpr OptionInfo<bool> StripDebug{
    "strip-debug", "Strip debugger symbol info from translation unit"};

inline constexpr OptionInfo<bool> StripNamedMetadata{
    "strip-named-metadata", "Strip module-level named metadata"};

inline constexpr OptionInfo<bool> OptLevelO0{
    "O0", "Optimization level 0. Similar to clang -O0. "
          "Same as -passes=\"default<O0>\""};
inline constexpr OptionInfo<bool> OptLevelO1{
    "O1", "Optimization level 1. Similar to clang -O1. "
          "Same as -passes=\"default<O1>\""};
inline constexpr OptionInfo<bool> OptLevelO2{
    "O2", "Optimization level 2. Similar to clang -O2. "
          "Same as -passes=\"default<O2>\""};
inline constexpr OptionInfo<bool> OptLevelOs{
    "Os", "Like -O2 but size-conscious. Similar to clang -Os. "
          "Same as -passes=\"default<Os>\""};
inline constexpr OptionInfo<bool> OptLevelOz{
    "Oz", "Like -O2 but optimize for code size above all else. Similar to "
          "clang -Oz. Same as -passes=\"default<Oz>\""};
inline constexpr OptionInfo<bool> OptLevelO3{
    "O3", "Optimization level 3. Similar to clang -O3. "
          "Same as -passes=\"default<O3>\""};

inline constexpr OptionInfo<unsigned> CodeGenOptLevelCL{
    "codegen-opt-level",
    "Override optimization level for codegen hooks, legacy PM only"};

inline constexpr OptionInfo<std::string> TargetTriple{
    "mtriple", "Override target triple for module"};

inline constexpr OptionInfo<bool> EmitSummaryIndex{"module-summary",
                                                   "Emit module summary index"};

inline constexpr OptionInfo<bool> EmitModuleHash{"module-hash",
                                                 "Emit module hash"};

inline constexpr OptionInfo<bool> DisableSimplifyLibCalls{
    "disable-simplify-libcalls", "Disable simplify-libcalls"};

inline constexpr ListOptionInfo<std::string> DisableBuiltins{
    "disable-builtin", "Disable specific target library builtin function",
    ZeroOrMore};

inline constexpr ListOptionInfo<std::string> EnableBuiltins{
    "enable-builtin", "Enable specific target library builtin functions",
    ZeroOrMore};

inline constexpr OptionInfo<bool> EnableDebugify{
    "enable-debugify",
    "Start the pipeline with debugify and end it with check-debugify"};

inline constexpr OptionInfo<bool> VerifyDebugInfoPreserve{
    "verify-debuginfo-preserve",
    "Start the pipeline with collecting and end it with checking of "
    "debug info preservation."};

#if defined(LLVM_ENABLE_PROFCHECK)
inline constexpr OptionInfo<bool> EnableProfileVerification{
    "enable-profcheck",
    "Start the pipeline with prof-inject and end it with prof-verify",
    Init{true}};
#else
inline constexpr OptionInfo<bool> EnableProfileVerification{
    "enable-profcheck",
    "Start the pipeline with prof-inject and end it with prof-verify"};
#endif

inline constexpr OptionInfo<std::string> ClDataLayout{
    "data-layout", "data layout string to use", value_desc("layout-string")};

inline constexpr OptionInfo<bool> RunTwice{
    "run-twice",
    "Run all passes twice, re-using the same pass manager (legacy PM only).",
    Hidden};

inline constexpr OptionInfo<bool> DiscardValueNames{
    "discard-value-names", "Discard names from Value (other than GlobalValue).",
    Hidden};

inline constexpr OptionInfo<bool> TimeTrace{"time-trace", "Record time trace"};

inline constexpr OptionInfo<unsigned> TimeTraceGranularity{
    "time-trace-granularity",
    "Minimum time granularity (in microseconds) traced by time profiler",
    Init{500u}, Hidden};

inline constexpr OptionInfo<std::string> TimeTraceFile{
    "time-trace-file", "Specify time trace file destination",
    value_desc("filename")};

inline constexpr OptionInfo<bool> RemarksWithHotness{
    "pass-remarks-with-hotness",
    "With PGO, include profile count in optimization remarks", Hidden};

// RemarksHotnessThreshold uses a custom parser for std::optional<uint64_t>
// (values "auto" or an integer).  Declare as string; parse manually in main().
inline constexpr OptionInfo<std::string> RemarksHotnessThreshold{
    "pass-remarks-hotness-threshold",
    "Minimum profile count required for an optimization remark to be output. "
    "Use 'auto' to apply the threshold from profile summary",
    value_desc("N or 'auto'"), Hidden};

inline constexpr OptionInfo<std::string> RemarksFilename{
    "pass-remarks-output", "Output filename for pass remarks",
    value_desc("filename")};

inline constexpr OptionInfo<std::string> RemarksPasses{
    "pass-remarks-filter",
    "Only record optimization remarks from passes whose names match the given "
    "regular expression",
    value_desc("regex")};

inline constexpr OptionInfo<std::string> RemarksFormat{
    "pass-remarks-format",
    "The format used for serializing remarks (default: YAML)",
    value_desc("format"), Init{"yaml"}};

inline constexpr ListOptionInfo<std::string> PassPlugins{
    "load-pass-plugin", "Load passes from plugin library", ZeroOrMore};

// CG_* option descriptors are provided by CommandFlagsOptInfos.h (CGOptsReg).
// CG_* from CommandFlagsOptInfos.h, MC_* from MCOptionsOptInfos.h.

//===----------------------------------------------------------------------===//
// NewPMDriver options
//===----------------------------------------------------------------------===//

inline constexpr OptionInfo<bool> NPM_DebugifyEach{
    "debugify-each",
    "Start each pass with debugify and end it with check-debugify"};

inline constexpr OptionInfo<std::string> NPM_DebugifyExport{
    "debugify-export", "Export per-pass debugify statistics to this file",
    value_desc("filename")};

inline constexpr OptionInfo<bool> NPM_VerifyEachDebugInfoPreserve{
    "verify-debuginfo-preserve-each",
    "Start each pass with collecting and end it with checking of "
    "debug info preservation."};
// Backward-compatibility alias for the renamed option.
inline constexpr AliasInfo NPM_VerifyEachDebugInfoPreserveAlias{
    "verify-each-debuginfo-preserve", "verify-debuginfo-preserve-each"};

inline constexpr OptionInfo<std::string> NPM_VerifyDIPreserveExport{
    "verify-di-preserve-export",
    "Export per-pass debug info preservation issues to this file",
    value_desc("filename")};

inline constexpr OptionInfo<bool> NPM_EnableLoopFusion{
    "enable-loopfusion", "Enable the LoopFusion Pass", Hidden};

inline constexpr OptionInfo<std::string> NPM_DebugPM{
    "debug-pass-manager",
    "Print pass management debugging information (none/quiet/verbose)",
    ValueOptional, Hidden};

inline constexpr OptionInfo<std::string> NPM_AAPipeline{
    "aa-pipeline",
    "A textual description of the alias analysis pipeline for handling "
    "managed aliasing queries",
    Init{"default"}, Hidden};

inline constexpr OptionInfo<std::string> NPM_PeepholeEP{
    "passes-ep-peephole",
    "A textual description of the function pass pipeline inserted at "
    "the Peephole extension point into default pipelines",
    Hidden};

inline constexpr OptionInfo<std::string> NPM_LateLoopOptimizationsEP{
    "passes-ep-late-loop-optimizations",
    "A textual description of the loop pass pipeline inserted at "
    "the LateLoopOptimizations extension point into default pipelines",
    Hidden};

inline constexpr OptionInfo<std::string> NPM_LoopOptimizerEndEP{
    "passes-ep-loop-optimizer-end",
    "A textual description of the loop pass pipeline inserted at "
    "the LoopOptimizerEnd extension point into default pipelines",
    Hidden};

inline constexpr OptionInfo<std::string> NPM_ScalarOptimizerLateEP{
    "passes-ep-scalar-optimizer-late",
    "A textual description of the function pass pipeline inserted at "
    "the ScalarOptimizerLate extension point into default pipelines",
    Hidden};

inline constexpr OptionInfo<std::string> NPM_CGSCCOptimizerLateEP{
    "passes-ep-cgscc-optimizer-late",
    "A textual description of the CGSCC pass pipeline inserted at "
    "the CGSCCOptimizerLate extension point into default pipelines",
    Hidden};

inline constexpr OptionInfo<std::string> NPM_VectorizerStartEP{
    "passes-ep-vectorizer-start",
    "A textual description of the function pass pipeline inserted at "
    "the VectorizerStart extension point into default pipelines",
    Hidden};

inline constexpr OptionInfo<std::string> NPM_VectorizerEndEP{
    "passes-ep-vectorizer-end",
    "A textual description of the function pass pipeline inserted at "
    "the VectorizerEnd extension point into default pipelines",
    Hidden};

inline constexpr OptionInfo<std::string> NPM_PipelineStartEP{
    "passes-ep-pipeline-start",
    "A textual description of the module pass pipeline inserted at "
    "the beginning of the pipeline",
    Hidden};

inline constexpr OptionInfo<std::string> NPM_PipelineEarlySimplificationEP{
    "passes-ep-pipeline-early-simplification",
    "A textual description of the module pass pipeline inserted at "
    "the EarlySimplification extension point into default pipelines",
    Hidden};

inline constexpr OptionInfo<std::string> NPM_OptimizerEarlyEP{
    "passes-ep-optimizer-early",
    "A textual description of the module pass pipeline inserted at "
    "the OptimizerEarly extension point into default pipelines",
    Hidden};

inline constexpr OptionInfo<std::string> NPM_OptimizerLastEP{
    "passes-ep-optimizer-last",
    "A textual description of the module pass pipeline inserted at "
    "the OptimizerLast extension point into default pipelines",
    Hidden};

inline constexpr OptionInfo<std::string> NPM_FullLTOEarlyEP{
    "passes-ep-full-link-time-optimization-early",
    "A textual description of the module pass pipeline inserted at "
    "the FullLinkTimeOptimizationEarly extension point into default pipelines",
    Hidden};

inline constexpr OptionInfo<std::string> NPM_FullLTOLastEP{
    "passes-ep-full-link-time-optimization-last",
    "A textual description of the module pass pipeline inserted at "
    "the FullLinkTimeOptimizationLast extension point into default pipelines",
    Hidden};

inline constexpr OptionInfo<bool> NPM_DisablePipelineVerification{
    "disable-pipeline-verification",
    "Only has an effect when specified with -print-pipeline-passes. "
    "Disables verifying that the textual pipeline string printed by "
    "-print-pipeline-passes can be used to create a pipeline.",
    Hidden};

inline constexpr OptionInfo<std::string> NPM_PGOKind{
    "pgo-kind",
    "The kind of PGO to use (ir-instr-gen/ir-instr-use/ir-sample-use)", Hidden};

inline constexpr OptionInfo<std::string> NPM_ProfileFile{
    "profile-file", "Profile file for PGO transformations",
    value_desc("filename"), Hidden};

inline constexpr OptionInfo<std::string> NPM_MemoryProfileFile{
    "memory-profile-file", "Profile file for memory profile transformations",
    value_desc("filename"), Hidden};

inline constexpr OptionInfo<std::string> NPM_CSPGOKind{
    "cspgo-kind",
    "The kind of context sensitive PGO to use "
    "(cs-ir-instr-gen/cs-ir-instr-use)",
    Hidden};

inline constexpr OptionInfo<std::string> NPM_CSProfileGenFile{
    "cs-profilegen-file", "Profile file for CS PGO profile collection",
    value_desc("filename"), Hidden};

inline constexpr OptionInfo<std::string> NPM_ProfileRemappingFile{
    "profile-remapping-file", "Path to the profile remapping file.",
    value_desc("filename"), Hidden};

inline constexpr OptionInfo<std::string> NPM_PGOColdFuncAttr{
    "pgo-cold-func-opt",
    "Specify the optimization used for cold functions marked by PGO "
    "(default/optsize/minsize/optnone)",
    Hidden};

inline constexpr OptionInfo<bool> NPM_DebugInfoForProfiling{
    "debug-info-for-profiling",
    "Emit special debug info to enable PGO profile generation.", Hidden};

inline constexpr OptionInfo<bool> NPM_PseudoProbeForProfiling{
    "pseudo-probe-for-profiling",
    "Emit pseudo probes to enable PGO profile generation.", Hidden};

inline constexpr OptionInfo<bool> NPM_DisableLoopUnrolling{
    "disable-loop-unrolling", "Disable loop unrolling in all relevant passes"};

//===----------------------------------------------------------------------===//
// Pass-option flags kept as shims.  The supported spelling is a pipeline-string
// parameter (e.g. -passes="aa-eval<print-all>"); these are translated to it so
// existing command lines keep working.
// When set, they are translated into pipeline-string parameters at the point
// where the Pipeline string is built, before it is handed to runPassPipeline().
//===----------------------------------------------------------------------===//

// aa-eval printer options
inline constexpr OptionInfo<bool> LegacyPrintAllAliasModRefInfo{
    "print-all-alias-modref-info",
    "Print all alias and modref info for every instruction", ReallyHidden};
inline constexpr OptionInfo<bool> LegacyPrintNoAliases{
    "print-no-aliases", "Print no-alias results", ReallyHidden};
inline constexpr OptionInfo<bool> LegacyPrintMayAliases{
    "print-may-aliases", "Print may-alias results", ReallyHidden};
inline constexpr OptionInfo<bool> LegacyPrintPartialAliases{
    "print-partial-aliases", "Print partial-alias results", ReallyHidden};
inline constexpr OptionInfo<bool> LegacyPrintMustAliases{
    "print-must-aliases", "Print must-alias results", ReallyHidden};
inline constexpr OptionInfo<bool> LegacyPrintNoModRef{
    "print-no-modref", "Print no-modref results", ReallyHidden};
inline constexpr OptionInfo<bool> LegacyPrintRef{
    "print-ref", "Print ref results", ReallyHidden};
inline constexpr OptionInfo<bool> LegacyPrintMod{
    "print-mod", "Print mod results", ReallyHidden};
inline constexpr OptionInfo<bool> LegacyPrintModRef{
    "print-modref", "Print modref results", ReallyHidden};
inline constexpr OptionInfo<bool> LegacyEvaluateAAMetadata{
    "evaluate-aa-metadata", "Evaluate alias analysis metadata", ReallyHidden};

// ScalarEvolution printer option (default true; pass =0 to disable)
inline constexpr OptionInfo<bool> LegacyScalarEvolutionClassifyExpressions{
    "scalar-evolution-classify-expressions",
    "Classify expressions by SCEV type for fast processing", ReallyHidden,
    Init{true}};

// RegionInfo printer option
inline constexpr OptionInfo<std::string> LegacyPrintRegionStyle{
    "print-region-style", "Style of printing regions (bb, rn)", ReallyHidden};

// DependenceAnalysis printer option
inline constexpr OptionInfo<std::string> LegacyDAEnableDependenceTest{
    "da-enable-dependence-test",
    "Run only specified dependence test routine "
    "(default, all, strong-siv, weak-crossing-siv, exact-siv, "
    "weak-zero-siv, exact-rdiv, gcd-miv, banerjee-miv)",
    ReallyHidden};

// Dead flag — accepted for backward compat, has no effect
inline constexpr OptionInfo<bool> LegacyCostModelReduxCost{
    "costmodel-reduxcost", "Should the cost of reduction be taken into account",
    ReallyHidden};

// CostModel printer options
inline constexpr OptionInfo<std::string> LegacyCostKind{
    "cost-kind",
    "Target cost kind (throughput, latency, code-size, size-latency, all)",
    ReallyHidden};
inline constexpr OptionInfo<std::string> LegacyIntrinsicCostStrategy{
    "intrinsic-cost-strategy",
    "Intrinsic cost strategy "
    "(instruction-cost, intrinsic-cost, type-based-intrinsic-cost)",
    ReallyHidden};

// Tool-only options (including MC and NewPM flags that don't have shared
// library registries yet).
inline constexpr OptionsRegistry<
    &PassPipeline, &PassPipeline2, &PrintPasses, &InputFilenameOpt,
    &OutputFilename, &Force, &NoOutput, &OutputAssembly, &OutputThinLTOBC,
    &SplitLTOUnit, &UnifiedLTO, &ThinLinkBitcodeFile, &NoVerify,
    &NoUpgradeDebugInfo, &VerifyEach, &DisableDITypeMap, &StripDebug,
    &StripNamedMetadata, &OptLevelO0, &OptLevelO1, &OptLevelO2, &OptLevelOs,
    &OptLevelOz, &OptLevelO3, &CodeGenOptLevelCL, &TargetTriple,
    &EmitSummaryIndex, &EmitModuleHash, &DisableSimplifyLibCalls,
    &DisableBuiltins, &EnableBuiltins, &EnableDebugify,
    &VerifyDebugInfoPreserve, &EnableProfileVerification, &ClDataLayout,
    &RunTwice, &DiscardValueNames, &TimeTrace, &TimeTraceGranularity,
    &TimeTraceFile, &RemarksWithHotness, &RemarksHotnessThreshold,
    &RemarksFilename, &RemarksPasses, &RemarksFormat, &PassPlugins,
    // NewPMDriver options
    &NPM_DebugifyEach, &NPM_DebugifyExport, &NPM_VerifyEachDebugInfoPreserve,
    &NPM_VerifyEachDebugInfoPreserveAlias, &NPM_VerifyDIPreserveExport,
    &NPM_EnableLoopFusion, &NPM_DebugPM, &NPM_AAPipeline, &NPM_PeepholeEP,
    &NPM_LateLoopOptimizationsEP, &NPM_LoopOptimizerEndEP,
    &NPM_ScalarOptimizerLateEP, &NPM_CGSCCOptimizerLateEP,
    &NPM_VectorizerStartEP, &NPM_VectorizerEndEP, &NPM_PipelineStartEP,
    &NPM_PipelineEarlySimplificationEP, &NPM_OptimizerEarlyEP,
    &NPM_OptimizerLastEP, &NPM_FullLTOEarlyEP, &NPM_FullLTOLastEP,
    &NPM_DisablePipelineVerification, &NPM_PGOKind, &NPM_ProfileFile,
    &NPM_MemoryProfileFile, &NPM_CSPGOKind, &NPM_CSProfileGenFile,
    &NPM_ProfileRemappingFile, &NPM_PGOColdFuncAttr, &NPM_DebugInfoForProfiling,
    &NPM_PseudoProbeForProfiling, &NPM_DisableLoopUnrolling,
    // Legacy pass-option backward-compat flags
    &LegacyPrintAllAliasModRefInfo, &LegacyPrintNoAliases,
    &LegacyPrintMayAliases, &LegacyPrintPartialAliases, &LegacyPrintMustAliases,
    &LegacyPrintNoModRef, &LegacyPrintRef, &LegacyPrintMod, &LegacyPrintModRef,
    &LegacyEvaluateAAMetadata, &LegacyScalarEvolutionClassifyExpressions,
    &LegacyPrintRegionStyle, &LegacyDAEnableDependenceTest,
    &LegacyCostModelReduxCost, &LegacyCostKind, &LegacyIntrinsicCostStrategy>
    OptToolReg;

// Compose tool and library registries into a single parse.
// Registries parsed by this tool, with their bridge functions.
static void configureOptRegistries(clv2::OptionParser &P) {
  P.add<&OptToolReg>();
  registerCGOptsOptions(P);
  registerMCOptsOptions(P);
  P.add<&SupportOptsReg, support::applySupportOptions>();
  registerRemarksOptsOptions(P);
  registerObjectOptsOptions(P);
  registerAsmParserOptsOptions(P);
  registerPassesOptsOptions(P);
  registerLTOOptsOptions(P);
  registerIROptsOptions(P);
  registerBitcodeOptsOptions(P);
  registerAggressiveInstCombineOptsOptions(P);
  registerCoroutinesOptsOptions(P);
  registerObjCARCOptsOptions(P);
  registerInstCombineOptsOptions(P);
  registerTransformUtilsOptsOptions(P);
  registerVectorizeOptsOptions(P);
  registerAnalysisOptsOptions(P);
  registerScalarOptsOptions(P);
  registerIPOOptsOptions(P);
  registerInstrumentationOptsOptions(P);
  registerProfileDataOptsOptions(P);
  registerCGPassAsmPrintOptions(P);
  registerCGPassCore1Options(P);
  registerCGPassCore2Options(P);
  registerCGPassGISelOptions(P);
  registerCGPassMachine1Options(P);
  registerCGPassMachine2Options(P);
  registerCGPassAllocOptions(P);
  registerCGPassSched1Options(P);
  registerCGPassSched2Options(P);
  registerCGPassSelDAGOptions(P);
  registerCGDataOptsOptions(P);
  registerOMPOptsOptions(P);
#if LLVM_HAS_ARC_TARGET
  P.add<&clv2::ARCOptsReg>();
#endif
#if LLVM_HAS_CSKY_TARGET
  P.add<&clv2::CSKYOptsReg>();
#endif
#if LLVM_HAS_M68K_TARGET
  P.add<&clv2::M68kOptsReg>();
#endif
  registerX86Options(P);
  registerAArch64Options(P);
  registerAMDGPUOptionsWithBridge(P);
  registerARMOptions(P);
  registerHexagonOptions(P);
  registerRISCVOptions(P);
  registerPowerPCOptions(P);
  registerMipsOptions(P);
  registerSystemZOptions(P);
  registerSparcOptions(P);
  registerWebAssemblyOptions(P);
  registerLoongArchOptions(P);
  registerNVPTXOptions(P);
  registerLanaiOptions(P);
  registerBPFOptions(P);
  registerSPIRVOptions(P);
  registerMSP430Options(P);
  registerXCoreOptions(P);
#ifdef LINK_POLLY_INTO_TOOLS
  // Polly is statically linked into opt via the extension mechanism, so its
  // options have to be registered here or every -polly-* flag is rejected.
  P.add<&PollyOptsReg, polly_opts::applyPollyOptions>();
#endif
}

//===----------------------------------------------------------------------===//
// Legacy pass-option translation helpers.
//

/// Append `NewParams` (semicolon-separated tokens) into every occurrence of
/// `PassName` in the pipeline string `Pipeline`.
///
/// Handles two forms of pass names:
///   - Simple:  "aa-eval"       → "aa-eval<p>"        or "aa-eval<existing;p>"
///   - Nested:  "print<da>"     → "print<da<p>>"      or
///   "print<da<existing;p>>"
///
/// A match is valid only when `PassName` is preceded by a pipeline delimiter
/// (start of string, comma, or opening paren) and followed by a delimiter,
/// angle-bracket, or end of string — so "not-aa-eval" is never matched.
static std::string injectPassParams(StringRef Pipeline, StringRef PassName,
                                    StringRef NewParams) {
  if (NewParams.empty())
    return Pipeline.str();

  auto isDelim = [](char C) { return C == ',' || C == '(' || C == ')'; };

  // Whether PassName itself ends with '>' (e.g. "print<da>").
  // For such names, the pipeline may contain either "print<da>" (not yet
  // parametrized) or "print<da<existing>>" (already parametrized).  We search
  // for the common prefix "print<da" and then check whether the next char is
  // '>' (unparametrized) or '<' (already parametrized).
  bool NameHasAngles = PassName.ends_with('>');
  // The search key: for angle-bracket names, strip the trailing '>'.
  StringRef SearchKey = NameHasAngles ? PassName.drop_back(1) : PassName;

  std::string Result;
  Result.reserve(Pipeline.size() + NewParams.size() + 8);

  size_t Pos = 0;
  while (Pos < Pipeline.size()) {
    size_t Found = Pipeline.find(SearchKey, Pos);
    if (Found == StringRef::npos) {
      Result += Pipeline.substr(Pos);
      break;
    }

    // Validate left boundary.
    if (Found != 0 && !isDelim(Pipeline[Found - 1])) {
      Result += Pipeline.substr(Pos, Found - Pos + 1);
      Pos = Found + 1;
      continue;
    }

    // The char immediately after SearchKey determines the match form.
    size_t After = Found + SearchKey.size();
    bool AlreadyParametrized = false;
    bool ValidMatch = false;
    if (NameHasAngles) {
      // Expect '>' (plain) or '<' (already parametrized).
      if (After < Pipeline.size() && Pipeline[After] == '>') {
        ValidMatch = true;
        After++; // consume the '>'
      } else if (After < Pipeline.size() && Pipeline[After] == '<') {
        ValidMatch = true;
        AlreadyParametrized = true;
        // After stays pointing at '<'
      }
    } else {
      // Simple name: must be followed by delimiter, '<', or EOS.
      if (After >= Pipeline.size() || isDelim(Pipeline[After]) ||
          Pipeline[After] == '<') {
        ValidMatch = true;
        AlreadyParametrized =
            (After < Pipeline.size() && Pipeline[After] == '<');
      }
    }

    if (!ValidMatch) {
      Result += Pipeline.substr(Pos, Found - Pos + 1);
      Pos = Found + 1;
      continue;
    }

    // Valid match — copy prefix and the pass name.
    Result += Pipeline.substr(Pos, Found - Pos);
    Result += SearchKey; // emit name without trailing '>'
    Pos = After;

    if (AlreadyParametrized) {
      // Params already exist — find the closing bracket(s) and append.
      size_t CloseChar;
      StringRef CloseStr;
      if (NameHasAngles) {
        // e.g. "print<da<existing>>" — close with ">>"
        CloseStr = ">>";
        CloseChar = 2;
      } else {
        // e.g. "aa-eval<existing>" — close with ">"
        CloseStr = ">";
        CloseChar = 1;
      }
      size_t Close = Pipeline.find(CloseStr, Pos);
      if (Close == StringRef::npos) {
        // Malformed — emit unchanged and stop.
        if (NameHasAngles)
          Result += '>';
        Result += Pipeline.substr(Pos);
        Pos = Pipeline.size();
        continue;
      }
      Result += Pipeline.substr(Pos, Close - Pos); // "<existing"
      Result += ';';
      Result += NewParams;
      Result += CloseStr;
      Pos = Close + CloseChar;
    } else {
      // Not yet parametrized — wrap in new brackets.
      if (NameHasAngles) {
        Result += '<';
        Result += NewParams;
        Result += ">>";
      } else {
        Result += '<';
        Result += NewParams;
        Result += '>';
      }
    }
  }
  return Result;
}

//===----------------------------------------------------------------------===//
// CodeGen-related helper functions.
//

namespace {
struct TimeTracerRAII {
  bool Active;
  std::string TraceFile;
  std::string OutFile;

  TimeTracerRAII(StringRef ProgramName, bool EnableTrace, unsigned Granularity,
                 StringRef TraceFileArg, StringRef OutputFilenameArg)
      : Active(EnableTrace), TraceFile(TraceFileArg),
        OutFile(OutputFilenameArg) {
    if (Active)
      timeTraceProfilerInitialize(Granularity, ProgramName);
  }
  ~TimeTracerRAII() {
    if (!Active)
      return;
    if (auto E = timeTraceProfilerWrite(TraceFile, OutFile)) {
      handleAllErrors(std::move(E), [&](const StringError &SE) {
        errs() << SE.getMessage() << "\n";
      });
      return;
    }
    timeTraceProfilerCleanup();
  }
};
} // namespace

// For use in NPM transition. Currently this contains most codegen-specific
// passes. Remove passes from here when porting to the NPM.
// TODO: use a codegen version of PassRegistry.def/PassBuilder::is*Pass() once
// it exists.
static bool shouldPinPassToLegacyPM(StringRef Pass) {
  static constexpr StringLiteral PassNameExactToIgnore[] = {
      "nvvm-reflect",
      "nvvm-intr-range",
      "amdgpu-simplifylib",
      "amdgpu-image-intrinsic-opt",
      "amdgpu-usenative",
      "amdgpu-promote-alloca",
      "amdgpu-promote-alloca-to-vector",
      "amdgpu-lower-kernel-attributes",
      "amdgpu-propagate-attributes-early",
      "amdgpu-propagate-attributes-late",
      "amdgpu-printf-runtime-binding",
      "amdgpu-always-inline"};
  if (llvm::is_contained(PassNameExactToIgnore, Pass))
    return false;

  static constexpr StringLiteral PassNamePrefix[] = {
      "x86-",    "xcore-", "wasm-",  "systemz-", "ppc-",    "nvvm-",
      "nvptx-",  "mips-",  "lanai-", "hexagon-", "bpf-",    "avr-",
      "thumb2-", "arm-",   "si-",    "gcn-",     "amdgpu-", "aarch64-",
      "amdgcn-", "polly-", "riscv-", "dxil-"};
  static constexpr StringLiteral PassNameContain[] = {"-eh-prepare"};
  static constexpr StringLiteral PassNameExact[] = {
      "safe-stack",
      "cost-model",
      "codegenprepare",
      "interleaved-load-combine",
      "unreachableblockelim",
      "verify-safepoint-ir",
      "atomic-expand",
      "expandvp",
      "mve-tail-predication",
      "interleaved-access",
      "global-merge",
      "pre-isel-intrinsic-lowering",
      "expand-reductions",
      "indirectbr-expand",
      "generic-to-nvvm",
      "expand-memcmp",
      "loop-reduce",
      "lower-amx-type",
      "lower-amx-intrinsics",
      "polyhedral-info",
      "print-polyhedral-info",
      "replace-with-veclib",
      "jmc-instrumenter",
      "dot-regions",
      "dot-regions-only",
      "view-regions",
      "view-regions-only",
      "select-optimize",
      "structurizecfg",
      "fix-irreducible",
      "expand-ir-insts",
      "inline-asm-prepare",
      "scalarizer",
  };
  for (StringLiteral P : PassNamePrefix)
    if (Pass.starts_with(P))
      return true;
  for (StringLiteral P : PassNameContain)
    if (Pass.contains(P))
      return true;
  return llvm::is_contained(PassNameExact, Pass);
}

// For use in NPM transition.
// PassNames contains pass argument names collected by runtime option callbacks.
namespace {
/// Parse state for one legacy-pass flag: where to record it, and under which
/// name.  Recorded via a callback rather than by scanning occurrence counts
/// afterwards, so that LegacyPassNames keeps command-line order.
struct LegacyPassOption {
  llvm::SmallVector<std::string, 16> *Names = nullptr;
  std::string Name;
  std::optional<llvm::clv2::RuntimeOption<bool>> Opt;
};
} // namespace

static bool recordLegacyPass(void *Ctx, const bool &) {
  auto *LP = static_cast<LegacyPassOption *>(Ctx);
  LP->Names->push_back(LP->Name);
  return true;
}

static bool shouldForceLegacyPM(ArrayRef<std::string> PassNames) {
  const PassRegistry &PR = *PassRegistry::getPassRegistry();
  for (StringRef Name : PassNames)
    if (shouldPinPassToLegacyPM(Name) && PR.getPassInfo(Name))
      return true;
  return false;
}

//===----------------------------------------------------------------------===//
// main for opt
//
extern "C" int
optMain(int argc, char **argv,
        ArrayRef<std::function<void(PassBuilder &)>> PassBuilderCallbacks) {
  InitLLVM X(argc, argv);
  registerPluginLoaderOption();

  // Enable debug stream buffering.
  EnableDebugBuffering = true;

  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmPrinters();
  InitializeAllAsmParsers();

  // Initialize passes
  PassRegistry &Registry = *PassRegistry::getPassRegistry();
  initializeCore(Registry);
  initializeScalarOpts(Registry);
  initializeVectorization(Registry);
  initializeIPO(Registry);
  initializeAnalysis(Registry);
  initializeTransformUtils(Registry);
  initializeInstCombine(Registry);
  initializeTarget(Registry);
  // For codegen passes, only passes that do IR to IR transformation are
  // supported.
  initializeExpandIRInstsLegacyPassPass(Registry);
  initializeScalarizeMaskedMemIntrinLegacyPassPass(Registry);
  initializeSelectOptimizePass(Registry);
  initializeInlineAsmPreparePass(Registry);
  initializeCodeGenPrepareLegacyPassPass(Registry);
  initializeAtomicExpandLegacyPass(Registry);
  initializeWinEHPreparePass(Registry);
  initializeDwarfEHPrepareLegacyPassPass(Registry);
  initializeSafeStackLegacyPassPass(Registry);
  initializeSjLjEHPreparePass(Registry);
  initializePreISelIntrinsicLoweringLegacyPassPass(Registry);
  initializeGlobalMergePass(Registry);
  initializeIndirectBrExpandLegacyPassPass(Registry);
  initializeInterleavedLoadCombinePass(Registry);
  initializeInterleavedAccessPass(Registry);
  initializePostInlineEntryExitInstrumenterPass(Registry);
  initializeUnreachableBlockElimLegacyPassPass(Registry);
  initializeExpandReductionsPass(Registry);
  initializeWasmEHPreparePass(Registry);
  initializeWriteBitcodePassPass(Registry);
  initializeReplaceWithVeclibLegacyPass(Registry);
  initializeJMCInstrumenterPass(Registry);

  static constexpr clv2::OptionCategory LegacyPassCategory{
      "Optimizations available (use \"-passes=\" for the new pass manager)"};

  // Build OptionEntry objects for each legacy pass.  When a pass flag is
  // specified on the command line, the recordLegacyPass callback appends its
  // name to LegacyPassNames so shouldForceLegacyPM() and the LPM builder can
  // inspect which passes were requested.
  SmallVector<std::string, 16> LegacyPassNames;
  std::vector<clv2::detail::OptionEntry> LegacyEntries;
  // Per-pass parse state.  Pass names come from the registry, so each
  // descriptor is built at runtime; a deque keeps addresses stable because the
  // parser holds a pointer to each RuntimeOption.  This must outlive the parse.
  std::deque<LegacyPassOption> LegacyPassOptions;
  {
    struct PassEnumerator : PassRegistrationListener {
      std::vector<clv2::detail::OptionEntry> &Entries;
      SmallVector<std::string, 16> &Names;
      std::deque<LegacyPassOption> &Opts;
      PassEnumerator(std::vector<clv2::detail::OptionEntry> &E,
                     SmallVector<std::string, 16> &N,
                     std::deque<LegacyPassOption> &O)
          : Entries(E), Names(N), Opts(O) {}
      void passEnumerate(const PassInfo *PI) override {
        StringRef Arg = PI->getPassArgument();
        if (Arg.empty() || !PI->getNormalCtor())
          return;
        // RuntimeOption is immovable, so emplace then fill.
        Opts.emplace_back();
        LegacyPassOption &LP = Opts.back();
        LP.Names = &Names;
        LP.Name = Arg.str();
        LP.Opt.emplace(Arg, PI->getPassName(), clv2::ValueDisallowed,
                       clv2::cat(LegacyPassCategory),
                       clv2::CtxCallback<bool>{&recordLegacyPass, &LP});
        Entries.push_back(LP.Opt->makeEntry());
      }
    };
    PassEnumerator PE(LegacyEntries, LegacyPassNames, LegacyPassOptions);
    Registry.enumerateWith(&PE);
  }
  // Pre-load pass plugins so that options they register are available
  // before parsing.
  SmallVector<PassPlugin, 1> PreloadedPlugins;
  for (int I = 1; I < argc; ++I) {
    StringRef Arg(argv[I]);
    StringRef PluginPath;
    if (Arg.starts_with("--load-pass-plugin="))
      PluginPath = Arg.substr(strlen("--load-pass-plugin="));
    else if (Arg.starts_with("-load-pass-plugin="))
      PluginPath = Arg.substr(strlen("-load-pass-plugin="));
    else if ((Arg == "--load-pass-plugin" || Arg == "-load-pass-plugin") &&
             I + 1 < argc)
      PluginPath = argv[++I];
    if (!PluginPath.empty()) {
      auto Plugin = PassPlugin::Load(PluginPath.str());
      if (Plugin) {
        PreloadedPlugins.emplace_back(Plugin.get());
      } else {
        errs() << "Could not load library '" << PluginPath
               << "': " << toString(Plugin.takeError()) << '\n';
        return 1;
      }
    }
  }

  clv2::OptionParser P;
  configureOptRegistries(P);
  // Pick up options that libraries register at static-init time.
  P.enableGlobalDynamicEntries();
  for (auto &E : LegacyEntries)
    P.addDynamicEntry(std::move(E));
  auto OptsCtxOwner = P.parse(
      argc, argv, "llvm .bc -> .bc modular optimizer and analysis printer\n",
      /*Errs=*/nullptr);
  const auto &OptsCtx = *OptsCtxOwner;
  const auto *Opts = OptsCtx.getViewPtr<&OptToolReg>();

  // Unpack all options from the parsed result.
  const std::string &PassPipelineStr = Opts->get<&PassPipeline>();
  const bool DoPrintPasses = Opts->get<&PrintPasses>();

  std::string InputFile = Opts->get<&InputFilenameOpt>();
  if (InputFile.empty())
    InputFile = "-";

  // Pass library views directly — no manual unpacking needed.
  // Library bridges are called automatically by OptionParser::parse().
  setTPCValues(CGPassBuilderOption{});

  std::string OutputFile = Opts->get<&OutputFilename>();
  const bool DoForce = Opts->get<&Force>();
  bool DoNoOutput = Opts->get<&NoOutput>();
  const bool DoOutputAssembly = Opts->get<&OutputAssembly>();
  const bool DoOutputThinLTOBC = Opts->get<&OutputThinLTOBC>();
  const bool DoSplitLTOUnit = Opts->get<&SplitLTOUnit>();
  const bool DoUnifiedLTO = Opts->get<&UnifiedLTO>();
  const std::string &ThinLinkFile = Opts->get<&ThinLinkBitcodeFile>();
  const bool DoNoVerify = Opts->get<&NoVerify>();
  const bool DoNoUpgradeDebugInfo = Opts->get<&NoUpgradeDebugInfo>();
  const bool DoVerifyEach = Opts->get<&VerifyEach>();
  const bool DoDisableDITypeMap = Opts->get<&DisableDITypeMap>();
  const bool DoStripDebug = Opts->get<&StripDebug>();
  const bool DoStripNamedMetadata = Opts->get<&StripNamedMetadata>();
  const bool DoOptO0 = Opts->get<&OptLevelO0>();
  const bool DoOptO1 = Opts->get<&OptLevelO1>();
  const bool DoOptO2 = Opts->get<&OptLevelO2>();
  const bool DoOptOs = Opts->get<&OptLevelOs>();
  const bool DoOptOz = Opts->get<&OptLevelOz>();
  const bool DoOptO3 = Opts->get<&OptLevelO3>();
  const unsigned CGOptLevel = Opts->get<&CodeGenOptLevelCL>();
  const std::string &TripleOverride = Opts->get<&TargetTriple>();
  const bool DoEmitSummaryIndex = Opts->get<&EmitSummaryIndex>();
  const bool DoEmitModuleHash = Opts->get<&EmitModuleHash>();
  const bool DoDisableSimplifyLibCalls = Opts->get<&DisableSimplifyLibCalls>();
  const std::vector<std::string> &DisableBuiltinList =
      Opts->get<&DisableBuiltins>();
  const std::vector<std::string> &EnableBuiltinList =
      Opts->get<&EnableBuiltins>();
  const bool DoEnableDebugify = Opts->get<&EnableDebugify>();
  const bool DoVerifyDIPreserve = Opts->get<&VerifyDebugInfoPreserve>();
  const bool DoEnableProfcheck = Opts->get<&EnableProfileVerification>();
  const std::string &DataLayoutStr = Opts->get<&ClDataLayout>();
  const bool DoRunTwice = Opts->get<&RunTwice>();
  const bool DoDiscardValueNames = Opts->get<&DiscardValueNames>();
  const bool DoTimeTrace = Opts->get<&TimeTrace>();
  const unsigned TimeTraceGran = Opts->get<&TimeTraceGranularity>();
  const std::string &TimeTraceFileStr = Opts->get<&TimeTraceFile>();
  const bool DoRemarksWithHotness = Opts->get<&RemarksWithHotness>();
  const std::string &RemarksHotnessStr = Opts->get<&RemarksHotnessThreshold>();
  const std::string &RemarksOutputFile = Opts->get<&RemarksFilename>();
  const std::string &RemarksPassFilter = Opts->get<&RemarksPasses>();
  const std::string &RemarksFormatStr = Opts->get<&RemarksFormat>();
  // Populate NPMOptions from clv2-parsed values.
  NPMOptions NPMOpts;
  NPMOpts.DebugifyEach = Opts->get<&NPM_DebugifyEach>();
  NPMOpts.DebugifyExport = Opts->get<&NPM_DebugifyExport>();
  NPMOpts.VerifyEachDebugInfoPreserve =
      Opts->get<&NPM_VerifyEachDebugInfoPreserve>();
  NPMOpts.VerifyDIPreserveExport = Opts->get<&NPM_VerifyDIPreserveExport>();
  NPMOpts.EnableLoopFusion = Opts->get<&NPM_EnableLoopFusion>();
  {
    // -debug-pass-manager with no value → Normal; with "verbose" → Verbose;
    // with "quiet" → Quiet; absent → None.
    const std::string &DPM = Opts->get<&NPM_DebugPM>();
    if (!Opts->specified<&NPM_DebugPM>())
      NPMOpts.DebugPM = DebugLogging::None;
    else if (DPM.empty() || DPM == "normal")
      NPMOpts.DebugPM = DebugLogging::Normal;
    else if (DPM == "verbose")
      NPMOpts.DebugPM = DebugLogging::Verbose;
    else if (DPM == "quiet")
      NPMOpts.DebugPM = DebugLogging::Quiet;
    else
      NPMOpts.DebugPM = DebugLogging::Normal;
  }
  NPMOpts.AAPipeline = Opts->get<&NPM_AAPipeline>();
  NPMOpts.PeepholeEPPipeline = Opts->get<&NPM_PeepholeEP>();
  NPMOpts.LateLoopOptimizationsEPPipeline =
      Opts->get<&NPM_LateLoopOptimizationsEP>();
  NPMOpts.LoopOptimizerEndEPPipeline = Opts->get<&NPM_LoopOptimizerEndEP>();
  NPMOpts.ScalarOptimizerLateEPPipeline =
      Opts->get<&NPM_ScalarOptimizerLateEP>();
  NPMOpts.CGSCCOptimizerLateEPPipeline = Opts->get<&NPM_CGSCCOptimizerLateEP>();
  NPMOpts.VectorizerStartEPPipeline = Opts->get<&NPM_VectorizerStartEP>();
  NPMOpts.VectorizerEndEPPipeline = Opts->get<&NPM_VectorizerEndEP>();
  NPMOpts.PipelineStartEPPipeline = Opts->get<&NPM_PipelineStartEP>();
  NPMOpts.PipelineEarlySimplificationEPPipeline =
      Opts->get<&NPM_PipelineEarlySimplificationEP>();
  NPMOpts.OptimizerEarlyEPPipeline = Opts->get<&NPM_OptimizerEarlyEP>();
  NPMOpts.OptimizerLastEPPipeline = Opts->get<&NPM_OptimizerLastEP>();
  NPMOpts.FullLinkTimeOptimizationEarlyEPPipeline =
      Opts->get<&NPM_FullLTOEarlyEP>();
  NPMOpts.FullLinkTimeOptimizationLastEPPipeline =
      Opts->get<&NPM_FullLTOLastEP>();
  NPMOpts.DisablePipelineVerification =
      Opts->get<&NPM_DisablePipelineVerification>();
  {
    const std::string &K = Opts->get<&NPM_PGOKind>();
    if (K == "ir-instr-gen" || K == "pgo-instr-gen-pipeline")
      NPMOpts.PGOKindFlag = InstrGen;
    else if (K == "ir-instr-use" || K == "pgo-instr-use-pipeline")
      NPMOpts.PGOKindFlag = InstrUse;
    else if (K == "ir-sample-use" || K == "pgo-sample-use-pipeline")
      NPMOpts.PGOKindFlag = SampleUse;
    else
      NPMOpts.PGOKindFlag = NoPGO;
  }
  NPMOpts.ProfileFile = Opts->get<&NPM_ProfileFile>();
  NPMOpts.MemoryProfileFile = Opts->get<&NPM_MemoryProfileFile>();
  {
    const std::string &K = Opts->get<&NPM_CSPGOKind>();
    if (K == "cs-ir-instr-gen" || K == "cspgo-instr-gen-pipeline")
      NPMOpts.CSPGOKindFlag = CSInstrGen;
    else if (K == "cs-ir-instr-use" || K == "cspgo-instr-use-pipeline")
      NPMOpts.CSPGOKindFlag = CSInstrUse;
    else
      NPMOpts.CSPGOKindFlag = NoCSPGO;
  }
  NPMOpts.CSProfileGenFile = Opts->get<&NPM_CSProfileGenFile>();
  NPMOpts.ProfileRemappingFile = Opts->get<&NPM_ProfileRemappingFile>();
  {
    const std::string &K = Opts->get<&NPM_PGOColdFuncAttr>();
    if (K == "optsize")
      NPMOpts.PGOColdFuncAttr = PGOColdFuncAttrKind::OptSize;
    else if (K == "minsize")
      NPMOpts.PGOColdFuncAttr = PGOColdFuncAttrKind::MinSize;
    else if (K == "optnone")
      NPMOpts.PGOColdFuncAttr = PGOColdFuncAttrKind::OptNone;
    else
      NPMOpts.PGOColdFuncAttr = PGOColdFuncAttrKind::Default;
  }
  NPMOpts.DebugInfoForProfiling = Opts->get<&NPM_DebugInfoForProfiling>();
  NPMOpts.PseudoProbeForProfiling = Opts->get<&NPM_PseudoProbeForProfiling>();
  NPMOpts.DisableLoopUnrolling = Opts->get<&NPM_DisableLoopUnrolling>();

  // Parse the optional hotness threshold ("N" or "auto").
  std::optional<uint64_t> HotnessThreshold;
  if (!RemarksHotnessStr.empty()) {
    if (RemarksHotnessStr == "auto")
      HotnessThreshold = std::nullopt;
    else
      HotnessThreshold = (uint64_t)std::stoull(RemarksHotnessStr);
  } else {
    HotnessThreshold = 0; // default: no threshold
  }

  // Use pre-loaded plugins (loaded before parsing so their options are known).
  SmallVector<PassPlugin, 1> PluginList(std::move(PreloadedPlugins));

  LLVMContext Context(OptsCtx);

  // TODO: remove shouldForceLegacyPM().
  const bool UseNPM =
      !shouldForceLegacyPM(LegacyPassNames) || Opts->specified<&PassPipeline>();

  // Warn when NPM is in use but the user passed a legacy pass name.
  if (UseNPM && !LegacyPassNames.empty()) {
    errs() << "The `opt -passname` syntax for the new pass manager is "
              "not supported, please use `opt -passes=<pipeline>` (or the "
              "`-p` alias for a more concise version).\n";
    errs() << "See https://llvm.org/docs/NewPassManager.html#invoking-opt "
              "for more details on the pass pipeline syntax.\n\n";
    return 1;
  }

  if (!UseNPM && !PluginList.empty()) {
    errs() << argv[0] << ": "
           << "--load-pass-plugin specified with legacy PM.\n";
    return 1;
  }

  // FIXME: once the legacy PM code is deleted, move runPassPipeline() here and
  // construct the PassBuilder before parsing IR so we can reuse the same
  // PassBuilder for print passes.
  if (DoPrintPasses) {
    printPasses(outs());
    return 0;
  }

  auto GetCodeGenOptLevel = [&]() {
    return static_cast<CodeGenOptLevel>(CGOptLevel);
  };

  // If user just wants to list available options, skip module loading.
  auto MAttrs = codegen::getMAttrs(OptsCtx);
  std::string CPUStr = codegen::getCPUStr(OptsCtx);
  std::string TuneCPUStr = codegen::getTuneCPUStr(OptsCtx);
  bool SkipModule =
      CPUStr == "help" || TuneCPUStr == "help" || is_contained(MAttrs, "help");
  if (SkipModule) {
    Triple TheTriple;
    if (!TripleOverride.empty())
      TheTriple = Triple(Triple::normalize(TripleOverride));
    else
      TheTriple = Triple(sys::getDefaultTargetTriple());

    std::string Error;
    const Target *TheTarget = TargetRegistry::lookupTarget(
        codegen::getMArch(OptsCtx), TheTriple, Error);
    if (!TheTarget) {
      errs() << argv[0] << ": " << Error << "\n";
      return 1;
    }

    // Pass "help" as CPU for -mtune=help
    std::string SkipModuleCPU = (TuneCPUStr == "help" ? "help" : CPUStr);
    TargetOptions Options =
        codegen::InitTargetOptionsFromCodeGenFlags(TheTriple, OptsCtx);
    // Create the target machine just to print the help info. Use unique_ptr
    // to avoid a memory leak.
    std::unique_ptr<TargetMachine> TM(TheTarget->createTargetMachine(
        TheTriple, SkipModuleCPU, codegen::getFeaturesStr(OptsCtx), Options,
        codegen::getExplicitRelocModel(OptsCtx),
        codegen::getExplicitCodeModel(OptsCtx), GetCodeGenOptLevel()));
    if (!TM) {
      errs() << argv[0] << ": could not allocate target machine\n";
      return 1;
    }

    // If we don't have a module then just exit now. We do this down
    // here since the CPU/Feature help is underneath the target machine
    // creation.
    return 0;
  }

  TimeTracerRAII TimeTracer(argv[0], DoTimeTrace, TimeTraceGran,
                            TimeTraceFileStr, OutputFile);

  SMDiagnostic Err;

  Context.setDiscardValueNames(DoDiscardValueNames);
  if (!DoDisableDITypeMap)
    Context.enableDebugTypeODRUniquing();

  Expected<LLVMRemarkFileHandle> RemarksFileOrErr =
      setupLLVMOptimizationRemarks(Context, RemarksOutputFile,
                                   RemarksPassFilter, RemarksFormatStr,
                                   DoRemarksWithHotness, HotnessThreshold);
  if (Error E = RemarksFileOrErr.takeError()) {
    errs() << toString(std::move(E)) << '\n';
    return 1;
  }
  LLVMRemarkFileHandle RemarksFileHandle = std::move(*RemarksFileOrErr);

  codegen::MaybeEnableStatistics(OptsCtx);

  std::string ABIName = mc::getABIName(OptsCtx); // FIXME: Handle module flag.

  // Load the input module...
  auto SetDataLayout = [&](StringRef IRTriple,
                           StringRef IRLayout) -> std::optional<std::string> {
    // Data layout specified on the command line has the highest priority.
    if (!DataLayoutStr.empty())
      return DataLayoutStr;
    // If an explicit data layout is already defined in the IR, don't infer.
    if (!IRLayout.empty())
      return std::nullopt;

    // If an explicit triple was specified (either in the IR or on the
    // command line), use that to infer the default data layout. However, the
    // command line target triple should override the IR file target triple.
    std::string TripleStr = TripleOverride.empty()
                                ? IRTriple.str()
                                : Triple::normalize(TripleOverride);
    // If the triple string is still empty, we don't fall back to
    // sys::getDefaultTargetTriple() since we do not want to have differing
    // behaviour dependent on the configured default triple. Therefore, if the
    // user did not pass -mtriple or define an explicit triple/datalayout in
    // the IR, we should default to an empty (default) DataLayout.
    if (TripleStr.empty())
      return std::nullopt;

    Triple TT(TripleStr);

    std::string Str = TT.computeDataLayout(ABIName);
    if (Str.empty()) {
      errs() << argv[0]
             << ": warning: failed to infer data layout from target triple\n";
      return std::nullopt;
    }
    return Str;
  };
  std::unique_ptr<Module> M;
  if (DoNoUpgradeDebugInfo)
    M = parseAssemblyFileWithIndexNoUpgradeDebugInfo(InputFile, Err, Context,
                                                     nullptr, SetDataLayout)
            .Mod;
  else
    M = parseIRFile(InputFile, Err, Context, ParserCallbacks(SetDataLayout));

  if (!M) {
    Err.print(argv[0], errs());
    return 1;
  }

  // Strip debug info before running the verifier.
  if (DoStripDebug)
    StripDebugInfo(*M);

  // Erase module-level named metadata, if requested.
  if (DoStripNamedMetadata) {
    while (!M->named_metadata_empty()) {
      NamedMDNode *NMD = &*M->named_metadata_begin();
      M->eraseNamedMetadata(NMD);
    }
  }

  // If we are supposed to override the target triple, do so now.
  if (!TripleOverride.empty())
    M->setTargetTriple(Triple(Triple::normalize(TripleOverride)));

  // Immediately run the verifier to catch any problems before starting up the
  // pass pipelines.  Otherwise we can crash on broken code during
  // doInitialization().
  if (!DoNoVerify && verifyModule(*M, &errs())) {
    errs() << argv[0] << ": " << InputFile
           << ": error: input module is broken!\n";
    return 1;
  }

  // Manually assign GUIDs -- updateVCallVisibilityInModule accesses GUIDs, and
  // there's no way to specify it in the pass pipeline since this runs before
  // any pass given on the command line.
  if (hasWholeProgramVisibility(/*WholeProgramVisibilityEnabledInLTO=*/false,
                                M.get(),
                                /*Ctx=*/llvm::clv2::defaultOptionsContext()))
    AssignGUIDPass::runOnModule(*M);

  // Enable testing of whole program devirtualization on this module by invoking
  // the facility for updating public visibility to linkage unit visibility when
  // specified by an internal option. This is normally done during LTO which is
  // not performed via opt.
  updateVCallVisibilityInModule(
      *M,
      /*WholeProgramVisibilityEnabledInLTO=*/false,
      // FIXME: These need linker information via a
      // TBD new interface.
      /*DynamicExportSymbols=*/{},
      /*ValidateAllVtablesHaveTypeInfos=*/false,
      /*IsVisibleToRegularObj=*/[](StringRef) { return true; });

  // Figure out what stream we are supposed to write to...
  std::unique_ptr<ToolOutputFile> Out;
  std::unique_ptr<ToolOutputFile> ThinLinkOut;
  if (DoNoOutput) {
    if (!OutputFile.empty())
      errs() << "WARNING: The -o (output filename) option is ignored when\n"
                "the --disable-output option is used.\n";
  } else {
    // Default to standard output.
    if (OutputFile.empty())
      OutputFile = "-";

    std::error_code EC;
    sys::fs::OpenFlags Flags =
        DoOutputAssembly ? sys::fs::OF_TextWithCRLF : sys::fs::OF_None;
    Out.reset(new ToolOutputFile(OutputFile, EC, Flags));
    if (EC) {
      errs() << EC.message() << '\n';
      return 1;
    }

    if (!ThinLinkFile.empty()) {
      ThinLinkOut.reset(new ToolOutputFile(ThinLinkFile, EC, sys::fs::OF_None));
      if (EC) {
        errs() << EC.message() << '\n';
        return 1;
      }
    }
  }

  Triple ModuleTriple(M->getTargetTriple());
  // Avoid setting target function attributes if no arch is found, by resetting
  // them first
  CPUStr.clear();
  TuneCPUStr.clear();
  std::string FeaturesStr;
  std::unique_ptr<TargetMachine> TM;
  if (ModuleTriple.getArch()) {
    CPUStr = codegen::getCPUStr(OptsCtx);
    TuneCPUStr = codegen::getTuneCPUStr(OptsCtx);
    FeaturesStr = codegen::getFeaturesStr(OptsCtx);
    Expected<std::unique_ptr<TargetMachine>> ExpectedTM =
        codegen::createTargetMachineForTriple(ModuleTriple, *&OptsCtx,
                                              GetCodeGenOptLevel());
    if (auto E = ExpectedTM.takeError()) {
      errs() << argv[0] << ": WARNING: failed to create target machine for '"
             << ModuleTriple.str() << "': " << toString(std::move(E)) << "\n";
    } else {
      TM = std::move(*ExpectedTM);
      TM->setOptionsContext(*&OptsCtx);
    }
  } else if (ModuleTriple.getArchName() != "unknown" &&
             ModuleTriple.getArchName() != "") {
    errs() << argv[0] << ": unrecognized architecture '"
           << ModuleTriple.getArchName() << "' provided.\n";
    return 1;
  }

  TargetOptions CodeGenFlagsOptions;
  const TargetOptions *Options = TM ? &TM->Options : &CodeGenFlagsOptions;
  if (!TM) {
    CodeGenFlagsOptions =
        codegen::InitTargetOptionsFromCodeGenFlags(ModuleTriple, OptsCtx);
  }

  // Override function attributes based on CPUStr, TuneCPUStr, FeaturesStr, and
  // command line flags.
  codegen::setFunctionAttributes(*M, CPUStr, FeaturesStr, TuneCPUStr);

  // If the output is set to be emitted to standard out, and standard out is a
  // console, print out a warning message and refuse to do it.  We don't
  // impress anyone by spewing tons of binary goo to a terminal.
  if (!DoForce && !DoNoOutput && !DoOutputAssembly)
    if (CheckBitcodeOutputToConsole(Out->os()))
      DoNoOutput = true;

  if (DoOutputThinLTOBC) {
    M->addModuleFlag(Module::Error, "EnableSplitLTOUnit", DoSplitLTOUnit);
    if (DoUnifiedLTO)
      M->addModuleFlag(Module::Error, "UnifiedLTO", 1);
  }

  // Add an appropriate TargetLibraryInfo pass for the module's triple.
  TargetLibraryInfoImpl TLII(ModuleTriple, Options->VecLib);

  // The -disable-simplify-libcalls flag actually disables all builtin optzns.
  if (DoDisableSimplifyLibCalls)
    TLII.disableAllFunctions();
  else {
    // Disable individual builtin functions in TargetLibraryInfo.
    for (const std::string &FuncName : DisableBuiltinList) {
      if (LibFunc F = TLII.getLibFunc(FuncName))
        TLII.setUnavailable(F);
      else {
        errs() << argv[0] << ": cannot disable nonexistent builtin function "
               << FuncName << '\n';
        return 1;
      }
    }

    for (const std::string &FuncName : EnableBuiltinList) {
      if (LibFunc F = TLII.getLibFunc(FuncName))
        TLII.setAvailable(F);
      else {
        errs() << argv[0] << ": cannot enable nonexistent builtin function "
               << FuncName << '\n';
        return 1;
      }
    }
  }

  if (UseNPM) {
    if (legacy::debugPassSpecified(*&OptsCtx)) {
      errs() << "-debug-pass does not work with the new PM, either use "
                "-debug-pass-manager, or use the legacy PM\n";
      return 1;
    }
    auto NumOLevel = DoOptO0 + DoOptO1 + DoOptO2 + DoOptO3 + DoOptOs + DoOptOz;
    if (NumOLevel > 1) {
      errs() << "Cannot specify multiple -O#\n";
      return 1;
    }
    if (NumOLevel > 0 && Opts->specified<&PassPipeline>()) {
      errs() << "Cannot specify -O# and --passes=/--foo-pass, use "
                "-passes='default<O#>,other-pass'\n";
      return 1;
    }
    std::string Pipeline = PassPipelineStr;

    if (DoOptO0)
      Pipeline = "default<O0>";
    if (DoOptO1)
      Pipeline = "default<O1>";
    if (DoOptO2)
      Pipeline = "default<O2>";
    if (DoOptO3)
      Pipeline = "default<O3>";
    if (DoOptOs)
      Pipeline = "default<Os>";
    if (DoOptOz)
      Pipeline = "default<Oz>";

    // Translate legacy pass-option flags into pipeline-string parameters.
    // Handled here so existing command lines keep working unchanged.
    {
      // Normalize old outer-params syntax "print<X><params>" →
      // "print<X<params>>" for the parametrized print passes whose names
      // contain angle brackets. The old FUNCTION_PASS_WITH_PARAMS macro
      // accepted "print<da><normalized-results>" (params outside the name's
      // angle brackets); the new dispatch expects
      // "print<da<normalized-results>>" (params nested inside).
      for (StringRef Base :
           {"cost-model", "scalar-evolution", "regions", "da"}) {
        std::string OldPrefix = ("print<" + Base + "><").str();
        size_t P = 0;
        while ((P = Pipeline.find(OldPrefix, P)) != std::string::npos) {
          // Find the closing '>' for the outer params.
          size_t Close = Pipeline.find('>', P + OldPrefix.size());
          if (Close == std::string::npos)
            break;
          // Rewrite: "print<BASE><PARAMS>" → "print<BASE<PARAMS>>"
          std::string OldToken = Pipeline.substr(P, Close - P + 1);
          StringRef Params =
              StringRef(OldToken).drop_front(OldPrefix.size()).drop_back(1);
          std::string NewToken = ("print<" + Base + "<" + Params + ">>").str();
          Pipeline.replace(P, OldToken.size(), NewToken);
          P += NewToken.size();
        }
      }

      // aa-eval: 10 boolean print flags
      SmallString<128> AAEvalParams;
      auto appendAAParam = [&](bool Val, StringRef Token) {
        if (Val) {
          if (!AAEvalParams.empty())
            AAEvalParams += ';';
          AAEvalParams += Token;
        }
      };
      appendAAParam(Opts->get<&LegacyPrintAllAliasModRefInfo>(), "print-all");
      appendAAParam(Opts->get<&LegacyPrintNoAliases>(), "print-no-aliases");
      appendAAParam(Opts->get<&LegacyPrintMayAliases>(), "print-may-aliases");
      appendAAParam(Opts->get<&LegacyPrintPartialAliases>(),
                    "print-partial-aliases");
      appendAAParam(Opts->get<&LegacyPrintMustAliases>(), "print-must-aliases");
      appendAAParam(Opts->get<&LegacyPrintNoModRef>(), "print-no-modref");
      appendAAParam(Opts->get<&LegacyPrintRef>(), "print-ref");
      appendAAParam(Opts->get<&LegacyPrintMod>(), "print-mod");
      appendAAParam(Opts->get<&LegacyPrintModRef>(), "print-modref");
      appendAAParam(Opts->get<&LegacyEvaluateAAMetadata>(),
                    "evaluate-aa-metadata");
      if (!AAEvalParams.empty())
        Pipeline = injectPassParams(Pipeline, "aa-eval", AAEvalParams);

      // scalar-evolution: classify flag (default true; =0 disables)
      if (!Opts->get<&LegacyScalarEvolutionClassifyExpressions>())
        Pipeline = injectPassParams(Pipeline, "print<scalar-evolution>",
                                    "no-classify");

      // print-region-style: bb or rn
      StringRef RegionStyle = Opts->get<&LegacyPrintRegionStyle>();
      if (!RegionStyle.empty())
        Pipeline = injectPassParams(Pipeline, "print<regions>", RegionStyle);

      // da-enable-dependence-test: value maps 1:1 to parser tokens
      StringRef DATest = Opts->get<&LegacyDAEnableDependenceTest>();
      if (!DATest.empty())
        Pipeline = injectPassParams(Pipeline, "print<da>", DATest);

      // costmodel-reduxcost: accepted but intentionally unused (was always
      // dead)
      (void)Opts->get<&LegacyCostModelReduxCost>();

      // cost-kind and intrinsic-cost-strategy for print<cost-model>
      {
        SmallString<64> CostModelParams;
        auto appendCMParam = [&](StringRef P) {
          if (!CostModelParams.empty())
            CostModelParams += ';';
          CostModelParams += P;
        };
        StringRef CostKindVal = Opts->get<&LegacyCostKind>();
        if (!CostKindVal.empty())
          appendCMParam(("kind=" + CostKindVal).str());
        StringRef IntrinsicCostVal = Opts->get<&LegacyIntrinsicCostStrategy>();
        if (!IntrinsicCostVal.empty())
          appendCMParam(("intrinsic-cost=" + IntrinsicCostVal).str());
        if (!CostModelParams.empty())
          Pipeline =
              injectPassParams(Pipeline, "print<cost-model>", CostModelParams);
      }
    }

    // Set the process-wide DependenceInfo default test type so that passes
    // like loop-interchange (which create DependenceInfo internally) also
    // respect the legacy -da-enable-dependence-test flag.
    {
      using DTT = DependenceTestType;
      StringRef DATest = Opts->get<&LegacyDAEnableDependenceTest>();
      if (!DATest.empty()) {
        DTT T = StringSwitch<DTT>(DATest)
                    .Case("all", DTT::All)
                    .Case("strong-siv", DTT::StrongSIV)
                    .Case("weak-crossing-siv", DTT::WeakCrossingSIV)
                    .Case("exact-siv", DTT::ExactSIV)
                    .Case("weak-zero-siv", DTT::WeakZeroSIV)
                    .Case("exact-rdiv", DTT::ExactRDIV)
                    .Case("gcd-miv", DTT::GCDMIV)
                    .Case("banerjee-miv", DTT::BanerjeeMIV)
                    .Default(DTT::Default);
        DependenceInfo::setDefaultTestType(T);
      }
    }

    OutputKind OK = OK_NoOutput;
    if (!DoNoOutput)
      OK = DoOutputAssembly ? OK_OutputAssembly
                            : (DoOutputThinLTOBC ? OK_OutputThinLTOBitcode
                                                 : OK_OutputBitcode);

    VerifierKind VK = VerifierKind::InputOutput;
    if (DoNoVerify)
      VK = VerifierKind::None;
    else if (DoVerifyEach)
      VK = VerifierKind::EachPass;

    // The user has asked to use the new pass manager and provided a pipeline
    // string. Hand off the rest of the functionality to the new code for that
    // layer.
    if (!runPassPipeline(
            argv[0], *M, TM.get(), &TLII, Out.get(), ThinLinkOut.get(),
            RemarksFileHandle ? RemarksFileHandle.get() : nullptr, Pipeline,
            PluginList, PassBuilderCallbacks, OK, VK,
            /* ShouldPreserveAssemblyUseListOrder */ false,
            /* ShouldPreserveBitcodeUseListOrder */ true, DoEmitSummaryIndex,
            DoEmitModuleHash, DoEnableDebugify, DoVerifyDIPreserve,
            DoEnableProfcheck, DoUnifiedLTO, NPMOpts))
      return 1;
    return codegen::MaybeSaveStatistics(OutputFile, "opt", OptsCtx);
  }

  if (DoOptO0 || DoOptO1 || DoOptO2 || DoOptOs || DoOptOz || DoOptO3) {
    errs() << "Cannot use -O# with legacy PM.\n";
    return 1;
  }
  if (DoEmitSummaryIndex) {
    errs() << "Cannot use -module-summary with legacy PM.\n";
    return 1;
  }
  if (DoEmitModuleHash) {
    errs() << "Cannot use -module-hash with legacy PM.\n";
    return 1;
  }
  if (DoOutputThinLTOBC) {
    errs() << "Cannot use -thinlto-bc with legacy PM.\n";
    return 1;
  }

  // Build the legacy pass list from LegacyPassNames, which contains argument
  // names collected by the runtime option callbacks registered above.
  SmallVector<std::string, 16> DeduplicatedNames;
  StringSet<> Seen;
  for (auto &Name : LegacyPassNames)
    if (Seen.insert(Name).second)
      DeduplicatedNames.push_back(std::move(Name));

  const PassRegistry &PR = *PassRegistry::getPassRegistry();
  SmallVector<const PassInfo *, 8> LegacyPasses;
  for (StringRef Name : DeduplicatedNames) {
    const PassInfo *PI = PR.getPassInfo(Name);
    if (!PI) {
      // Registered as a runtime option but not in PassRegistry — shouldn't
      // happen normally, but guard against it.
      if (shouldPinPassToLegacyPM(Name)) {
        errs() << argv[0] << ": cannot find pass: " << Name << "\n";
        return 1;
      }
      continue; // Skip: it's a CodeGen flag or space-separated value.
    }
    LegacyPasses.push_back(PI);
  }

  // Create a PassManager to hold and optimize the collection of passes we are
  // about to build. If the -debugify-each option is set, wrap each pass with
  // the (-check)-debugify passes.
  DebugifyCustomPassManager Passes;
  DebugifyStatsMap DIStatsMap;
  DebugInfoPerPass DebugInfoBeforePass;
  if (NPMOpts.DebugifyEach) {
    Passes.setDebugifyMode(DebugifyMode::SyntheticDebugInfo);
    Passes.setDIStatsMap(DIStatsMap);
  } else if (NPMOpts.VerifyEachDebugInfoPreserve) {
    Passes.setDebugifyMode(DebugifyMode::OriginalDebugInfo);
    Passes.setDebugInfoBeforePass(DebugInfoBeforePass);
    if (!NPMOpts.VerifyDIPreserveExport.empty())
      Passes.setOrigDIVerifyBugsReportFilePath(NPMOpts.VerifyDIPreserveExport);
  }

  bool AddOneTimeDebugifyPasses =
      (DoEnableDebugify && !NPMOpts.DebugifyEach) ||
      (DoVerifyDIPreserve && !NPMOpts.VerifyEachDebugInfoPreserve);

  Passes.setOptionsContext(*&OptsCtx);
  Passes.add(new TargetLibraryInfoWrapperPass(TLII));
  Passes.add(new RuntimeLibraryInfoWrapper(
      Options->ExceptionModel, Options->EABIVersion, Options->MCOptions.ABIName,
      Options->VecLib));

  // Add internal analysis passes from the target machine.
  Passes.add(createTargetTransformInfoWrapperPass(TM ? TM->getTargetIRAnalysis()
                                                     : TargetIRAnalysis()));

  if (AddOneTimeDebugifyPasses) {
    if (DoEnableDebugify) {
      Passes.setDIStatsMap(DIStatsMap);
      Passes.add(createDebugifyModulePass());
    } else if (DoVerifyDIPreserve) {
      Passes.setDebugInfoBeforePass(DebugInfoBeforePass);
      Passes.add(createDebugifyModulePass(DebugifyMode::OriginalDebugInfo, "",
                                          &(Passes.getDebugInfoPerPass())));
    }
  }

  if (TM) {
    Pass *TPC = TM->createPassConfig(Passes);
    if (!TPC) {
      errs() << "Target Machine pass config creation failed.\n";
      return 1;
    }
    Passes.add(TPC);
  }

  // Create a new optimization pass for each one specified on the command line.
  for (const PassInfo *PassInf : LegacyPasses) {
    if (PassInf->getNormalCtor()) {
      Pass *P = PassInf->getNormalCtor()();
      if (P) {
        // Thread OptionsContext to passes that need CLI option values
        // at getAnalysisUsage time (before a Function is available).
        if (StringRef(PassInf->getPassArgument()) == "structurizecfg") {
          if (auto *O = clv2::getView<&clv2::ScalarOptsReg>(OptsCtx)) {
            if (O->specified<&clv2::SC_StructurizecfgSkipUniformRegions>() &&
                O->get<&clv2::SC_StructurizecfgSkipUniformRegions>()) {
              delete P;
              P = createStructurizeCFGPass(true);
            }
          }
        }
        // Add the pass to the pass manager.
        Passes.add(P);
        // If we are verifying all of the intermediate steps, add the verifier.
        if (DoVerifyEach)
          Passes.add(createVerifierPass());
      }
    } else {
      errs() << argv[0] << ": cannot create pass: " << PassInf->getPassName()
             << "\n";
    }
  }

  // Check that the module is well formed on completion of optimization
  if (!DoNoVerify && !DoVerifyEach)
    Passes.add(createVerifierPass());

  if (AddOneTimeDebugifyPasses) {
    if (DoEnableDebugify)
      Passes.add(createCheckDebugifyModulePass(false));
    else if (DoVerifyDIPreserve) {
      if (!NPMOpts.VerifyDIPreserveExport.empty())
        Passes.setOrigDIVerifyBugsReportFilePath(
            NPMOpts.VerifyDIPreserveExport);
      Passes.add(createCheckDebugifyModulePass(
          false, "", nullptr, DebugifyMode::OriginalDebugInfo,
          &(Passes.getDebugInfoPerPass()), NPMOpts.VerifyDIPreserveExport));
    }
  }

  // In run twice mode, we want to make sure the output is bit-by-bit
  // equivalent if we run the pass manager again, so setup two buffers and
  // a stream to write to them. Note that llc does something similar and it
  // may be worth to abstract this out in the future.
  SmallVector<char, 0> Buffer;
  SmallVector<char, 0> FirstRunBuffer;
  std::unique_ptr<raw_svector_ostream> BOS;
  raw_ostream *OS = nullptr;

  const bool ShouldEmitOutput = !DoNoOutput;

  // Write bitcode or assembly to the output as the last step...
  if (ShouldEmitOutput || DoRunTwice) {
    assert(Out);
    OS = &Out->os();
    if (DoRunTwice) {
      BOS = std::make_unique<raw_svector_ostream>(Buffer);
      OS = BOS.get();
    }
    if (DoOutputAssembly)
      Passes.add(createPrintModulePass(
          *OS, "", /* ShouldPreserveAssemblyUseListOrder */ false));
    else
      Passes.add(createBitcodeWriterPass(
          *OS, /* ShouldPreserveBitcodeUseListOrder */ true));
  }

  // Before executing passes, print the final values of the LLVM options.

  if (!DoRunTwice) {
    // Now that we have all of the passes ready, run them.
    Passes.run(*M);
  } else {
    // If requested, run all passes twice with the same pass manager to catch
    // bugs caused by persistent state in the passes.
    std::unique_ptr<Module> M2(CloneModule(*M));
    // Run all passes on the original module first, so the second run processes
    // the clone to catch CloneModule bugs.
    Passes.run(*M);
    FirstRunBuffer = Buffer;
    Buffer.clear();

    Passes.run(*M2);

    // Compare the two outputs and make sure they're the same
    assert(Out);
    if (Buffer.size() != FirstRunBuffer.size() ||
        (memcmp(Buffer.data(), FirstRunBuffer.data(), Buffer.size()) != 0)) {
      errs()
          << "Running the pass manager twice changed the output.\n"
             "Writing the result of the second run to the specified output.\n"
             "To generate the one-run comparison binary, just run without\n"
             "the compile-twice option\n";
      if (ShouldEmitOutput) {
        Out->os() << BOS->str();
        Out->keep();
      }
      if (RemarksFileHandle)
        RemarksFileHandle->keep();
      return 1;
    }
    if (ShouldEmitOutput)
      Out->os() << BOS->str();
  }

  if (NPMOpts.DebugifyEach && !NPMOpts.DebugifyExport.empty())
    exportDebugifyStats(NPMOpts.DebugifyExport, Passes.getDebugifyStatsMap());

  // If a pass reported an error via LLVMContext::emitError, fail without
  // writing the output module.
  if (Context.getDiagHandlerPtr()->HasErrors)
    return 1;

  // Declare success.
  if (!DoNoOutput)
    Out->keep();

  if (RemarksFileHandle)
    RemarksFileHandle->keep();

  if (ThinLinkOut)
    ThinLinkOut->keep();

  return codegen::MaybeSaveStatistics(OutputFile, "opt", OptsCtx);
}
