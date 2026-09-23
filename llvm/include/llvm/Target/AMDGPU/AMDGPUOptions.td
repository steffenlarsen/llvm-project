//===- AMDGPUOptions.td - AMDGPU target options -----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def AMDGPUOptions : OptionsStruct<"llvm"> {}

//===----------------------------------------------------------------------===//
// Enum types
//===----------------------------------------------------------------------===//

// --- AMDGPULowerModuleLDSPass.cpp ---

def LDSLoweringKindEnum : OptionEnum<"LDSLoweringKind", [
    EnumMember<"module", "module">,
    EnumMember<"table", "table">,
    EnumMember<"kernel", "kernel">,
    EnumMember<"hybrid", "hybrid">]>;

// --- AMDGPUAtomicOptimizer.cpp ---
// AMDGPUScanOptions is deliberately its own self-contained enum, NOT an
// External FieldEnum reusing the real llvm::ScanOptions (identical
// members/ordinals): that type lives in the private
// lib/Target/AMDGPU/AMDGPU.h header, but the generated struct emits
// `FieldType KeyPath = Default;` as an in-class default member initializer,
// which requires the enum's complete definition in every TU that includes
// AMDGPUOptions.h -- including the external driver tools that have no
// include-path access to that private header. The static_cast conversion to
// the real llvm::ScanOptions is confined to the sole consumer call site in
// AMDGPUTargetMachine.cpp (getAMDGPUAtomicOptimizerStrategy).
def AMDGPUScanOptionsEnum : OptionEnum<"AMDGPUScanOptions", [
    EnumMember<"DPP", "DPP">,
    EnumMember<"Iterative", "Iterative">,
    EnumMember<"None", "None">]>;

// --- AMDGPUTargetMachine.cpp (sgpr/vgpr/wwm-regalloc-npm) ---
// Reuses the real, public llvm::RegAllocType (CGPassBuilderOption.h) so the
// 3 NPM regalloc options below produce that type directly, without a
// static_cast at the consumer. "Unset" (value 0) is intentionally not
// exposed as an EnumMember: it has no CLI spelling of its own for these
// options (it's used elsewhere, for the unrelated generic -regalloc-npm
// option, as a sentinel for "not specified").
def AMDGPURegAllocTypeEnum : OptionEnum<"RegAllocType", [
    EnumMember<"Default", "default">,
    EnumMember<"Basic", "basic">,
    EnumMember<"Fast", "fast">,
    EnumMember<"Greedy", "greedy">,
    EnumMember<"PBQP", "pbqp">]> {
  let External = 1;
}

// --- AMDGPUCoExecSchedStrategy.cpp ---

def CarriedLatencyEnum : OptionEnum<"CarriedLatency", [
    EnumMember<"Off", "off">,
    EnumMember<"Fence", "fence">,
    EnumMember<"All", "all">]>;

//===----------------------------------------------------------------------===//
// List options
//===----------------------------------------------------------------------===//

// --- AMDGPULibCalls.cpp ---
// Bare "-amdgpu-use-native" (no value) records a single empty-string list
// entry, matching the old clv2 ValueOptional ListOption spelling:
// AMDGPULibCalls::initNativeFuncs() treats a lone empty entry the same as
// an explicit "-amdgpu-use-native=all" (use native for everything). ListField
// itself has no bare-flag support, so this is hand-authored: the primary
// Joined def declares the field, and the bare Flag def uses Assign<> to
// push an empty string onto it (Assign<> always emits a plain "Field =
// Value;" assignment, so Value re-reads Field via the comma operator to
// append rather than overwrite).
def AMDGPU_UseNative : Joined<["-", "--"], "amdgpu-use-native=">,
    HelpText<"">, Flags<[HelpHidden]>,
    Field<"std::vector<std::string>", "AMDGPU_UseNative", "{}">,
    ListElement<"std::string", /*commaSplit=*/1, /*alwaysPrefix=*/0>;
def AMDGPU_UseNative_bare : Flag<["-", "--"], "amdgpu-use-native">,
    Flags<[HelpHidden]>,
    Assign<"AMDGPU_UseNative",
           "(AMDGPU_UseNative.push_back(std::string()), AMDGPU_UseNative)">;

//===----------------------------------------------------------------------===//
// Scalar options
//===----------------------------------------------------------------------===//

defm AMDGPU_StressCalls : BoolField<
    "amdgpu-stress-function-calls", "false",
    "Force all functions to be noinline", /*hidden=*/1>;

defm AMDGPU_IndirectCallSpecializationThreshold : ValueField<
    "amdgpu-indirect-call-specialization-threshold", "unsigned", "3",
    "A threshold controls whether an indirect call will be specialized">;

defm AMDGPU_BarrierSignalWaitLatency : ValueField<
    "amdgpu-barrier-signal-wait-latency", "unsigned", "16",
    "Synthetic latency between S_BARRIER_SIGNAL and S_BARRIER_WAIT "
    "to encourage scheduling independent work between them",
    /*hidden=*/1>;

defm AMDGPU_WidenConstantLoads : BoolField<
    "amdgpu-codegenprepare-widen-constant-loads", "false",
    "Widen sub-dword constant address space loads in AMDGPUCodeGenPrepare",
    /*hidden=*/1>;

defm AMDGPU_BreakLargePHIs : BoolField<
    "amdgpu-codegenprepare-break-large-phis", "true",
    "Break large PHI nodes for DAGISel", /*hidden=*/1>;

defm AMDGPU_ForceBreakLargePHIs : BoolField<
    "amdgpu-codegenprepare-force-break-large-phis", "false",
    "For testing purposes, always break large PHIs even if it "
    "isn't profitable.", /*hidden=*/1>;

defm AMDGPU_BreakLargePHIsThreshold : ValueField<
    "amdgpu-codegenprepare-break-large-phis-threshold", "unsigned", "32",
    "Minimum type size in bits for breaking large PHI nodes",
    /*hidden=*/1>;

defm AMDGPU_UseMul24Intrin : BoolField<
    "amdgpu-codegenprepare-mul24", "true",
    "Introduce mul24 intrinsics in AMDGPUCodeGenPrepare", /*hidden=*/1>;

defm AMDGPU_ExpandDiv64InIR : BoolField<
    "amdgpu-codegenprepare-expand-div64", "false",
    "Expand 64-bit division in AMDGPUCodeGenPrepare", /*hidden=*/1>;

defm AMDGPU_DisableIDivExpand : BoolField<
    "amdgpu-codegenprepare-disable-idiv-expansion", "false",
    "Prevent expanding integer division in AMDGPUCodeGenPrepare",
    /*hidden=*/1>;

defm AMDGPU_DisableFDivExpand : BoolField<
    "amdgpu-codegenprepare-disable-fdiv-expansion", "false",
    "Prevent expanding floating point division in AMDGPUCodeGenPrepare",
    /*hidden=*/1>;

defm AMDGPU_DumpHSAMetadata : BoolField<
    "amdgpu-dump-hsa-metadata", "false", "Dump AMDGPU HSA Metadata">;

defm AMDGPU_VerifyHSAMetadata : BoolField<
    "amdgpu-verify-hsa-metadata", "false", "Verify AMDGPU HSA Metadata">;

defm AMDGPU_EnableExactSolver : BoolField<
    "amdgpu-igrouplp-exact-solver", "false",
    "Whether to use the exponential time solver to fit the instructions "
    "to the pipeline as closely as possible.", /*hidden=*/1>;

defm AMDGPU_CutoffForExact : ValueField<
    "amdgpu-igrouplp-exact-solver-cutoff", "unsigned", "0",
    "The maximum number of scheduling group conflicts which we attempt "
    "to solve with the exponential time exact solver. Problem sizes "
    "greater than this will be solved by the less accurate greedy "
    "algorithm. Selecting solver by size is superseded by manually "
    "selecting the solver (e.g. by amdgpu-igrouplp-exact-solver",
    /*hidden=*/1>;

defm AMDGPU_MaxBranchesExplored : ValueField<
    "amdgpu-igrouplp-exact-solver-max-branches", "uint64_t", "0",
    "The amount of branches that we are willing to explore with "
    "the exact algorithm before giving up.", /*hidden=*/1>;

defm AMDGPU_UseCostHeur : BoolField<
    "amdgpu-igrouplp-exact-solver-cost-heur", "true",
    "Whether to use the cost heuristic to make choices as we traverse "
    "the search space using the exact solver. Defaulted to on, and if "
    "turned off, we will use the node order -- attempting to put the "
    "later nodes in the later sched groups. Experimentally, results are "
    "mixed, so this should be set on a case-by-case basis.",
    /*hidden=*/1>;

defm AMDGPU_BypassSlowDiv : BoolField<
    "amdgpu-bypass-slow-div", "true",
    "Skip 64-bit divide for dynamic 32-bit values">;

defm AMDGPU_LateWidenConstantLoads : BoolField<
    "amdgpu-late-codegenprepare-widen-constant-loads", "true",
    "Widen sub-dword constant address space loads in "
    "AMDGPULateCodeGenPrepare", /*hidden=*/1>;

defm AMDGPU_EnableNewLegality : BoolField<
    "amdgpu-global-isel-new-legality", "false",
    "Use GlobalISel desired legality, rather than try to use "
    "rules compatible with selection patterns", /*hidden=*/1>;

defm AMDGPU_EnablePreLink : BoolField<
    "amdgpu-prelink", "false", "Enable pre-link mode optimizations",
    /*hidden=*/1>;

defm AMDGPU_EnableOCLManglingMismatchWA : BoolField<
    "amdgpu-enable-ocl-mangling-mismatch-workaround", "true",
    "Enable the workaround for OCL name mangling mismatch.",
    /*hidden=*/1>;

defm AMDGPU_SuperAlignLDSGlobals : BoolField<
    "amdgpu-super-align-lds-globals", "true",
    "Increase alignment of LDS if it is not on align boundary",
    /*hidden=*/1>;

defm AMDGPU_LowerModuleLDSStrategy : EnumField<
    "amdgpu-lower-module-lds-strategy", LDSLoweringKindEnum,
    "LDSLoweringKind::hybrid",
    "Specify lowering strategy for function LDS access:", /*hidden=*/1>;

defm AMDGPU_DistanceCacheEnabled : BoolField<
    "amdgpu-next-use-analysis-distance-cache", "true",
    "Enable live-reg-use distance cache", /*hidden=*/1>;

// std::optional<std::string>: the real fallback used by the caller is a
// runtime, non-literal default, so "specified or not" must survive as
// real tri-state.
defm AMDGPU_DumpNextUseDistanceAsJson : OptionalField<
    "amdgpu-next-use-analysis-dump-distance-as-json", "std::string", "",
    /*hidden=*/1>;

defm AMDGPU_DumpNextUseDistanceDefToUse : BoolField<
    "amdgpu-next-use-analysis-dump-distance-def-to-use", "false", "",
    /*hidden=*/1>;

defm AMDGPU_DumpNextUseDistanceVerbose : BoolField<
    "amdgpu-next-use-analysis-dump-distance-verbose", "false", "",
    /*hidden=*/1>;

defm AMDGPU_ConfigPreset : ValueField<
    "amdgpu-next-use-analysis-config", "std::string", "\"graphics\"",
    "Config preset: 'graphics' or 'compute'", /*hidden=*/1>;

// std::optional<bool>: each of these 4 config-override options defaults to
// the preset's own value rather than a fixed literal, so "specified or not"
// must survive as real tri-state.
defm AMDGPU_ConfigCountPhis : OptionalValueField<
    "amdgpu-next-use-analysis-count-phis", "bool", "true",
    "Count PHI instructions toward distance and block size", /*hidden=*/1>;

defm AMDGPU_ConfigForwardOnly : OptionalValueField<
    "amdgpu-next-use-analysis-forward-only", "bool", "true",
    "Restrict inter-block distances to forward-reachable paths",
    /*hidden=*/1>;

defm AMDGPU_ConfigPreciseUseModeling : OptionalValueField<
    "amdgpu-next-use-analysis-precise-use-modeling", "bool", "true",
    "Model PHI uses via incoming edge block with loop-aware "
    "reachability filtering", /*hidden=*/1>;

defm AMDGPU_ConfigPromoteToPreheader : OptionalValueField<
    "amdgpu-next-use-analysis-use-preheader-model", "bool", "true",
    "Promote loop-entry and inner-loop uses to the loop preheader",
    /*hidden=*/1>;

defm AMDGPU_MemBoundThresh : ValueField<
    "amdgpu-membound-threshold", "unsigned", "50",
    "Function mem bound threshold in %", /*hidden=*/1>;

defm AMDGPU_LimitWaveThresh : ValueField<
    "amdgpu-limit-wave-threshold", "unsigned", "50",
    "Kernel limit wave threshold in %", /*hidden=*/1>;

defm AMDGPU_IAWeight : ValueField<
    "amdgpu-indirect-access-weight", "unsigned", "1000",
    "Indirect access memory instruction weight", /*hidden=*/1>;

defm AMDGPU_LSWeight : ValueField<
    "amdgpu-large-stride-weight", "unsigned", "1000",
    "Large stride memory access weight", /*hidden=*/1>;

defm AMDGPU_LargeStrideThresh : ValueField<
    "amdgpu-large-stride-threshold", "unsigned", "64",
    "Large stride memory access threshold", /*hidden=*/1>;

defm AMDGPU_KernargPreloadCount : ValueField<
    "amdgpu-kernarg-preload-count", "unsigned", "0",
    "How many kernel arguments to preload onto SGPRs">;

defm AMDGPU_EnableKernargPreload : BoolField<
    "amdgpu-kernarg-preload", "true",
    "Enable preload kernel arguments to SGPRs">;

defm AMDGPU_DisablePromoteAllocaToVector : BoolField<
    "disable-promote-alloca-to-vector", "false",
    "Disable promote alloca to vector">;

defm AMDGPU_DisablePromoteAllocaToLDS : BoolField<
    "disable-promote-alloca-to-lds", "false",
    "Disable promote alloca to LDS">;

defm AMDGPU_PromoteAllocaToVectorLimit : ValueField<
    "amdgpu-promote-alloca-to-vector-limit", "unsigned", "0",
    "Maximum byte size to consider promote alloca to vector">;

// std::optional<unsigned>: AMDGPUPromoteAlloca.cpp only overrides its
// internal literal default (32) when the option was not explicitly
// specified on the command line, so "specified or not" must survive.
defm AMDGPU_PromoteAllocaToVectorMaxRegs : OptionalField<
    "amdgpu-promote-alloca-to-vector-max-regs", "unsigned",
    "Maximum vector size (in 32b registers) to use when promoting alloca">;

defm AMDGPU_PromoteAllocaToVectorVGPRRatio : OptionalField<
    "amdgpu-promote-alloca-to-vector-vgpr-ratio", "unsigned",
    "Ratio of VGPRs to budget for promoting alloca to vectors">;

defm AMDGPU_LoopUserWeight : ValueField<
    "promote-alloca-vector-loop-user-weight", "unsigned", "4",
    "The bonus weight of users of allocas within loop when sorting "
    "profitable allocas">;

// std::optional<unsigned>: overridden by a per-function IR attribute only
// when not explicitly specified on the command line.
defm AMDGPU_AssumedStackSizeForExternalCall : OptionalField<
    "amdgpu-assume-external-call-stack-size", "unsigned",
    "Assumed stack use of any external call (in bytes)", /*hidden=*/1>;

// std::optional<unsigned>: overridden by a per-function IR attribute only
// when not explicitly specified on the command line.
defm AMDGPU_AssumedStackSizeForDynamicSizeObjects : OptionalField<
    "amdgpu-assume-dynamic-stack-object-size", "unsigned",
    "Assumed extra stack use if there are any variable sized objects "
    "(in bytes)", /*hidden=*/1>;

defm AMDGPU_AnyAddressSpace : BoolField<
    "amdgpu-any-address-space-out-arguments", "false",
    "Replace pointer out arguments with struct returns for non-private "
    "address space", /*hidden=*/1>;

defm AMDGPU_MaxNumRetRegs : ValueField<
    "amdgpu-max-return-arg-num-regs", "unsigned", "16",
    "Approximately limit number of return registers for replacing "
    "out arguments", /*hidden=*/1>;

defm AMDGPU_DefaultVALUInstsThreshold : ValueField<
    "amdgpu-set-wave-priority-valu-insts-threshold", "unsigned", "100",
    "VALU instruction count threshold for adjusting wave priority",
    /*hidden=*/1>;

defm AMDGPU_AsanInstrumentLDS : BoolField<
    "amdgpu-asan-instrument-lds", "true",
    "Run asan instrumentation on LDS instructions lowered to global memory",
    /*hidden=*/1>;

// std::optional<bool>: AMDGPUTargetMachine::getEnableFunctionCalls(Triple,
// Ctx) only falls back to a non-AMDGCN-triple-specific literal (false) when
// the option was not explicitly specified, so "specified or not" must
// survive. OptionalValueField (not OptionalField) keeps the original bare
// "-amdgpu-function-calls" spelling (equivalent to "=true") working.
defm AMDGPU_EnableFunctionCalls : OptionalValueField<
    "amdgpu-function-calls", "bool", "true",
    "Enable AMDGPU function call support", /*hidden=*/1>;

defm AMDGPU_EnableObjectLinking : BoolField<
    "amdgpu-enable-object-linking", "false",
    "Enable object linking for cross-TU LDS and ABI support",
    /*hidden=*/1>;

defm AMDGPU_EnableLowerModuleLDS : BoolField<
    "amdgpu-enable-lower-module-lds", "true",
    "Enable lower module lds pass", /*hidden=*/1>;

defm AMDGPU_EnableEarlyIfConversion : BoolField<
    "amdgpu-early-ifcvt", "false", "Run early if-conversion", /*hidden=*/1>;

defm AMDGPU_OptExecMaskPreRA : BoolField<
    "amdgpu-opt-exec-mask-pre-ra", "true",
    "Run pre-RA exec mask optimizations", /*hidden=*/1>;

defm AMDGPU_LowerCtorDtor : BoolField<
    "amdgpu-lower-global-ctor-dtor", "true",
    "Lower GPU ctor / dtors to globals on the device.", /*hidden=*/1>;

// std::optional<bool>: overridden by a per-function IR attribute only when
// not explicitly specified on the command line.
defm AMDGPU_EnableLoadStoreVectorizer : OptionalValueField<
    "amdgpu-load-store-vectorizer", "bool", "true",
    "Enable load store vectorizer", /*hidden=*/1>;

defm AMDGPU_ScalarizeGlobal : BoolField<
    "amdgpu-scalarize-global-loads", "true",
    "Enable global load scalarization", /*hidden=*/1>;

defm AMDGPU_InternalizeSymbols : BoolField<
    "amdgpu-internalize-symbols", "false",
    "Enable elimination of non-kernel functions and unused globals",
    /*hidden=*/1>;

defm AMDGPU_EarlyInlineAll : BoolField<
    "amdgpu-early-inline-all", "false", "Inline all functions early",
    /*hidden=*/1>;

defm AMDGPU_RemoveIncompatibleFunctions : BoolField<
    "amdgpu-enable-remove-incompatible-functions", "true",
    "Enable removal of functions when they use features not supported "
    "by the target GPU", /*hidden=*/1>;

// std::optional<bool>: overridden by a per-function IR attribute only when
// not explicitly specified on the command line.
defm AMDGPU_EnableSDWAPeephole : OptionalValueField<
    "amdgpu-sdwa-peephole", "bool", "true", "Enable SDWA peepholer">;

defm AMDGPU_EnableDPPCombine : BoolField<
    "amdgpu-dpp-combine", "true", "Enable DPP combiner">;

defm AMDGPU_EnableAMDGPUAliasAnalysis : BoolField<
    "enable-amdgpu-aa", "true", "Enable AMDGPU Alias Analysis",
    /*hidden=*/1>;

defm AMDGPU_EnableLibCallSimplify : BoolField<
    "amdgpu-simplify-libcall", "true",
    "Enable amdgpu library simplifications", /*hidden=*/1>;

// std::optional<bool>: "unspecified" must be distinguishable from an
// explicit true/false forced value; the tri-state feeds a TargetID
// override that otherwise falls back to per-function/module attributes.
defm AMDGPU_XnackSetting : OptionalValueField<
    "amdgpu-xnack", "bool", "true", "Force amdgpu.xnack value for testing",
    /*hidden=*/1>;

defm AMDGPU_SramEccSetting : OptionalValueField<
    "amdgpu-sramecc", "bool", "true", "Force amdgpu.sramecc for testing",
    /*hidden=*/1>;

defm AMDGPU_EnableLowerKernelArguments : BoolField<
    "amdgpu-ir-lower-kernel-arguments", "true",
    "Lower kernel argument loads in IR pass", /*hidden=*/1>;

defm AMDGPU_EnableRegReassign : BoolField<
    "amdgpu-reassign-regs", "true",
    "Enable register reassign optimizations on gfx10+", /*hidden=*/1>;

defm AMDGPU_OptVGPRLiveRange : BoolField<
    "amdgpu-opt-vgpr-liverange", "true",
    "Enable VGPR liverange optimizations for if-else structure",
    /*hidden=*/1>;

defm AMDGPU_AtomicOptimizerStrategy : EnumField<
    "amdgpu-atomic-optimizer-strategy", AMDGPUScanOptionsEnum,
    "AMDGPUScanOptions::Iterative",
    "Select DPP or Iterative strategy for scan">;

defm AMDGPU_EnableSIModeRegisterPass : BoolField<
    "amdgpu-mode-register", "true", "Enable mode register pass",
    /*hidden=*/1>;

// std::optional<bool>: overridden by a per-function IR attribute only when
// not explicitly specified on the command line.
defm AMDGPU_EnableInsertDelayAlu : OptionalValueField<
    "amdgpu-enable-delay-alu", "bool", "true", "Enable s_delay_alu insertion",
    /*hidden=*/1>;

defm AMDGPU_EnableVOPD : OptionalValueField<
    "amdgpu-enable-vopd", "bool", "true",
    "Enable VOPD, dual issue of VALU in wave32", /*hidden=*/1>;

defm AMDGPU_EnableDCEInRA : BoolField<
    "amdgpu-dce-in-ra", "true", "Enable machine DCE inside regalloc",
    /*hidden=*/1>;

// std::optional<bool>: overridden by a per-function IR attribute only when
// not explicitly specified on the command line.
defm AMDGPU_EnableSetWavePriority : OptionalValueField<
    "amdgpu-set-wave-priority", "bool", "true", "Adjust wave priority",
    /*hidden=*/1>;

defm AMDGPU_EnableScalarIRPasses : OptionalValueField<
    "amdgpu-scalar-ir-passes", "bool", "true", "Enable scalar IR passes",
    /*hidden=*/1>;

defm AMDGPU_EnableLowerExecSync : BoolField<
    "amdgpu-enable-lower-exec-sync", "true",
    "Enable lowering of execution synchronization.", /*hidden=*/1>;

defm AMDGPU_EnableSwLowerLDS : BoolField<
    "amdgpu-enable-sw-lower-lds", "true",
    "Enable lowering of lds to global memory pass and asan instrument "
    "resulting IR.", /*hidden=*/1>;

defm AMDGPU_EnablePreRAOptimizations : OptionalValueField<
    "amdgpu-enable-pre-ra-optimizations", "bool", "true",
    "Enable Pre-RA optimizations pass", /*hidden=*/1>;

defm AMDGPU_EnablePromoteKernelArguments : BoolField<
    "amdgpu-enable-promote-kernel-arguments", "true",
    "Enable promotion of flat kernel pointer arguments to global",
    /*hidden=*/1>;

defm AMDGPU_EnableImageIntrinsicOptimizer : OptionalValueField<
    "amdgpu-enable-image-intrinsic-optimizer", "bool", "true",
    "Enable image intrinsic optimizer pass", /*hidden=*/1>;

defm AMDGPU_EnableLoopPrefetch : OptionalValueField<
    "amdgpu-loop-prefetch", "bool", "true",
    "Enable loop data prefetch on AMDGPU", /*hidden=*/1>;

defm AMDGPU_SchedStrategy : ValueField<
    "amdgpu-sched-strategy", "std::string", "\"\"",
    "Select custom AMDGPU scheduling strategy.", /*hidden=*/1>;

defm AMDGPU_EnableRewritePartialRegUses : BoolField<
    "amdgpu-enable-rewrite-partial-reg-uses", "true",
    "Enable rewrite partial reg uses pass", /*hidden=*/1>;

defm AMDGPU_EnableHipStdPar : BoolField<
    "amdgpu-enable-hipstdpar", "false",
    "Enable HIP Standard Parallelism Offload support", /*hidden=*/1>;

defm AMDGPU_EnableAMDGPUAttributor : BoolField<
    "amdgpu-attributor-enable", "true", "Enable AMDGPUAttributorPass",
    /*hidden=*/1>;

defm AMDGPU_NewRegBankSelect : BoolField<
    "new-reg-bank-select", "false",
    "Run amdgpu-regbankselect and amdgpu-regbanklegalize instead of "
    "regbankselect", /*hidden=*/1>;

defm AMDGPU_HasClosedWorldAssumption : BoolField<
    "amdgpu-link-time-closed-world", "false",
    "Whether has closed-world assumption at link time", /*hidden=*/1>;

defm AMDGPU_EnableUniformIntrinsicCombine : BoolField<
    "amdgpu-enable-uniform-intrinsic-combine", "true",
    "Enable/Disable the Uniform Intrinsic Combine Pass", /*hidden=*/1>;

defm AMDGPU_UnrollThresholdPrivate : ValueField<
    "amdgpu-unroll-threshold-private", "unsigned", "2700",
    "Unroll threshold for AMDGPU if private memory used in a loop",
    /*hidden=*/1>;

defm AMDGPU_UnrollThresholdLocal : ValueField<
    "amdgpu-unroll-threshold-local", "unsigned", "1000",
    "Unroll threshold for AMDGPU if local memory used in a loop",
    /*hidden=*/1>;

defm AMDGPU_UnrollThresholdIf : ValueField<
    "amdgpu-unroll-threshold-if", "unsigned", "200",
    "Unroll threshold increment for AMDGPU for each if statement "
    "inside loop", /*hidden=*/1>;

defm AMDGPU_UnrollRuntimeLocal : BoolField<
    "amdgpu-unroll-runtime-local", "true",
    "Allow runtime unroll for AMDGPU if local memory used in a loop",
    /*hidden=*/1>;

defm AMDGPU_UnrollMaxBlockToAnalyze : ValueField<
    "amdgpu-unroll-max-block-to-analyze", "unsigned", "32",
    "Inner loop block size threshold to analyze in unroll for AMDGPU",
    /*hidden=*/1>;

defm AMDGPU_ArgAllocaCost : ValueField<
    "amdgpu-inline-arg-alloca-cost", "unsigned", "4000",
    "Cost of alloca argument", /*hidden=*/1>;

defm AMDGPU_ArgAllocaCutoff : ValueField<
    "amdgpu-inline-arg-alloca-cutoff", "unsigned", "256",
    "Maximum alloca size to use for inline cost", /*hidden=*/1>;

defm AMDGPU_InlineMaxBB : ValueField<
    "amdgpu-inline-max-bb", "unsigned", "1100",
    "Maximum number of BBs allowed in a function after inlining "
    "(compile time constraint)", /*hidden=*/1>;

defm AMDGPU_MemcpyLoopUnroll : ValueField<
    "amdgpu-memcpy-loop-unroll", "unsigned", "16",
    "Unroll factor (affecting 4x32-bit operations) to use for memory "
    "operations when lowering statically-sized memcpy, memmove, or "
    "memset as a loop", /*hidden=*/1>;

defm AMDGPU_CullSGPRHazardsOnFunctionBoundary : OptionalValueField<
    "amdgpu-sgpr-hazard-boundary-cull", "bool", "true",
    "Cull hazards on function boundaries", /*hidden=*/1>;

defm AMDGPU_CullSGPRHazardsAtMemWait : OptionalValueField<
    "amdgpu-sgpr-hazard-mem-wait-cull", "bool", "true",
    "Cull hazards on memory waits", /*hidden=*/1>;

defm AMDGPU_CullSGPRHazardsMemWaitThreshold : OptionalField<
    "amdgpu-sgpr-hazard-mem-wait-cull-threshold", "unsigned",
    "Number of tracked SGPRs before initiating hazard cull on memory wait",
    /*hidden=*/1>;

defm AMDGPU_MFMAPaddingRatio : ValueField<
    "amdgpu-mfma-padding-ratio", "unsigned", "0",
    "Fill a percentage of the latency between neighboring MFMA with "
    "s_nops.", /*hidden=*/1>;

defm AMDGPU_NopPadding : ValueField<
    "amdgpu-snop-padding", "unsigned", "0",
    "Insert a s_nop x before every instruction", /*hidden=*/1>;

defm AMDGPU_EnableWMMAVnopHoisting : BoolField<
    "amdgpu-wmma-vnop-hoisting", "true",
    "Hoist WMMA hazard V_NOPs from loops to preheaders", /*hidden=*/1>;

defm AMDGPU_LongBranchFactor : ValueField<
    "amdgpu-long-branch-factor", "double", "1.0",
    "Factor to apply to what qualifies as a long branch to reserve a "
    "pair of scalar registers. If this value is 0 the long branch "
    "registers are never reserved. As this value grows the greater "
    "chance the branch distance will fall within the threshold and the "
    "registers will be marked to be reserved. We lean towards always "
    "reserving a register for long jumps", /*hidden=*/1>;

defm AMDGPU_UseDownwardTracker : BoolField<
    "amdgpu-print-rp-downward", "false",
    "Use GCNDownwardRPTracker for GCNRegPressurePrinter pass",
    /*hidden=*/1>;

defm AMDGPU_DisableUnclusterHighRP : BoolField<
    "amdgpu-disable-unclustered-high-rp-reschedule", "false",
    "Disable unclustered high register pressure reduction scheduling "
    "stage.", /*hidden=*/1>;

defm AMDGPU_DisableClusteredLowOccupancy : BoolField<
    "amdgpu-disable-clustered-low-occupancy-reschedule", "false",
    "Disable clustered low occupancy rescheduling for ILP scheduling "
    "stage.", /*hidden=*/1>;

defm AMDGPU_ScheduleMetricBias : ValueField<
    "amdgpu-schedule-metric-bias", "unsigned", "10",
    "Sets the bias which adds weight to occupancy vs latency. Set it "
    "to 100 to chase the occupancy only.", /*hidden=*/1>;

defm AMDGPU_RelaxedOcc : BoolField<
    "amdgpu-schedule-relaxed-occupancy", "false",
    "Relax occupancy targets for kernels which are memory bound "
    "(amdgpu-membound-threshold), or Wave Limited "
    "(amdgpu-limit-wave-threshold).", /*hidden=*/1>;

defm AMDGPU_GCNTrackers : OptionalValueField<
    "amdgpu-use-amdgpu-trackers", "bool", "true",
    "Use the AMDGPU specific RPTrackers during scheduling", /*hidden=*/1>;

defm AMDGPU_PendingQueueLimit : ValueField<
    "amdgpu-scheduler-pending-queue-limit", "unsigned", "256",
    "Max (Available+Pending) size to inspect pending queue (0 disables)",
    /*hidden=*/1>;

defm AMDGPU_DisableRewriteMFMAFormSchedStage : BoolField<
    "amdgpu-disable-rewrite-mfma-form-sched-stage", "true",
    "Disable rewrite mfma rewrite scheduling stage", /*hidden=*/1>;

defm AMDGPU_PrintMaxRPRegUsageBeforeScheduler : BoolField<
    "amdgpu-print-max-reg-pressure-regusage-before-scheduler", "false",
    "Print a list of live registers along with their def/uses at the "
    "point of maximum register pressure before scheduling.",
    /*hidden=*/1>;

defm AMDGPU_PrintMaxRPRegUsageAfterScheduler : BoolField<
    "amdgpu-print-max-reg-pressure-regusage-after-scheduler", "false",
    "Print a list of live registers along with their def/uses at the "
    "point of maximum register pressure after scheduling.",
    /*hidden=*/1>;

defm AMDGPU_VGPRThresholdPercent : ValueField<
    "amdgpu-vgpr-threshold-percent", "unsigned", "0",
    "Percent of VGPR limits that we should use as RP threshold during "
    "scheduling. We have two limits relevant to scheduling: Critical "
    "(avoid decreasing occupancy), Excess (avoid spilling). This flag "
    "scales both limits back by an equal percent: (0 = use default "
    "calculation, 1-100 = use percentage), default: 0",
    /*hidden=*/1, /*metaVar=*/"", /*maxValue=*/100>;

defm AMDGPU_EnableVGPRIndexMode : BoolField<
    "amdgpu-vgpr-index-mode", "false",
    "Use GPR indexing mode instead of movrel for vector indexing">;

defm AMDGPU_UseAA : BoolField<
    "amdgpu-use-aa-in-codegen", "true",
    "Enable the use of AA during codegen.">;

defm AMDGPU_NSAThreshold : OptionalField<
    "amdgpu-nsa-threshold", "unsigned",
    "Number of addresses from which to enable MIMG NSA.", /*hidden=*/1>;

defm AMDGPU_ForceGenericVersion : ValueField<
    "amdgpu-force-generic-version", "unsigned", "0",
    "Force a specific generic_v<N> flag to be added. For testing "
    "purposes only.", /*hidden=*/1>;

defm AMDGPU_EnableR600StructurizeCFG : BoolField<
    "r600-ir-structurize", "true", "Use StructurizeCFG IR pass">;

defm AMDGPU_EnableR600IfConvert : BoolField<
    "r600-if-convert", "true", "Use if conversion pass", /*hidden=*/1>;

defm AMDGPU_EnableM0Merge : BoolField<
    "amdgpu-enable-merge-m0", "true", "Merge and hoist M0 initializations">;

defm AMDGPU_MaxClause : ValueField<
    "amdgpu-max-memory-clause", "unsigned", "15",
    "Maximum length of a memory clause, instructions", /*hidden=*/1>;

defm AMDGPU_EnableSpillVGPRToAGPR : BoolField<
    "amdgpu-spill-vgpr-to-agpr", "true",
    "Enable spilling VGPRs to AGPRs", /*hidden=*/1>;

// std::optional<unsigned>: the real fallback is computed from the
// subtarget's hardware clause length, a runtime, non-literal value.
defm AMDGPU_HardClauseLengthLimit : OptionalField<
    "amdgpu-hard-clause-length-limit", "unsigned",
    "Maximum number of memory instructions to place in the same hard "
    "clause", /*hidden=*/1>;

defm AMDGPU_ForceEmitZeroFlag : BoolField<
    "amdgpu-waitcnt-forcezero", "false",
    "Force all waitcnt instrs to be emitted as s_waitcnt vmcnt(0) "
    "expcnt(0) lgkmcnt(0)", /*hidden=*/1>;

defm AMDGPU_ForceEmitZeroLoadFlag : BoolField<
    "amdgpu-waitcnt-load-forcezero", "false",
    "Force all waitcnt load counters to wait until 0", /*hidden=*/1>;

defm AMDGPU_ExpertSchedulingModeFlag : OptionalValueField<
    "amdgpu-expert-scheduling-mode", "bool", "true",
    "Enable expert scheduling mode 2 for all functions (GFX12+ only)",
    /*hidden=*/1>;

defm AMDGPU_BranchOffsetBits : ValueField<
    "amdgpu-s-branch-bits", "unsigned", "16",
    "Restrict range of branch instructions (DEBUG)", /*hidden=*/1>;

defm AMDGPU_Fix16BitCopies : BoolField<
    "amdgpu-fix-16-bit-physreg-copies", "true",
    "Fix copies between 32 and 16 bit registers by extending to "
    "32 bit", /*hidden=*/1>;

defm AMDGPU_DisableLoopAlignment : BoolField<
    "amdgpu-disable-loop-alignment", "false",
    "Do not align and prefetch loops">;

defm AMDGPU_UseDivergentRegisterIndexing : BoolField<
    "amdgpu-use-divergent-register-indexing", "false",
    "Use indirect register addressing for divergent indexes",
    /*hidden=*/1>;

defm AMDGPU_RemoveRedundantEndcf : BoolField<
    "amdgpu-remove-redundant-endcf", "true", "", /*hidden=*/1>;

defm AMDGPU_MaxNumVGPRsForWwmAllocation : ValueField<
    "amdgpu-num-vgprs-for-wwm-alloc", "unsigned", "10",
    "Max num VGPRs for whole-wave register allocation.", /*hidden=*/1>;

defm AMDGPU_MFMAVGPRForm : BoolField<
    "amdgpu-mfma-vgpr-form", "true",
    "Whether to force use VGPR for Opc and Dest of MFMA. If "
    "unspecified, default to compiler heuristics", /*hidden=*/1>;

defm AMDGPU_SkipCacheInvalidations : BoolField<
    "amdgcn-skip-cache-invalidations", "false",
    "Use this to skip inserting cache invalidating instructions.",
    /*hidden=*/1>;

defm AMDGPU_EnablePreallocateSGPRSpillVGPRs : BoolField<
    "amdgpu-prealloc-sgpr-spill-vgprs", "false", "", /*hidden=*/1>;

defm AMDGPU_EnableSpillSGPRToVGPR : BoolField<
    "amdgpu-spill-sgpr-to-vgpr", "true",
    "Enable spilling SGPRs to VGPRs", /*hidden=*/1>;

defm AMDGPU_EnableSpillCFISavedRegs : BoolField<
    "amdgpu-spill-cfi-saved-regs", "false",
    "Enable spilling the registers required for CFI emission",
    /*hidden=*/1>;

defm AMDGPU_StressVGPRLimit : OptionalField<
    "amdgpu-stress-vgpr", "unsigned",
    "Limit VGPRs to N registers by reserving the rest", /*hidden=*/1>;

defm AMDGPU_StressAGPRLimit : OptionalField<
    "amdgpu-stress-agpr", "unsigned",
    "Limit AGPRs to N registers by reserving the rest", /*hidden=*/1>;

defm AMDGPU_StressSGPRLimit : OptionalField<
    "amdgpu-stress-sgpr", "unsigned",
    "Limit SGPRs to N registers by reserving the rest", /*hidden=*/1>;

// sgpr-regalloc, vgpr-regalloc and wwm-regalloc are NOT declared here: their
// valid values are the contents of file-local registries in
// AMDGPUTargetMachine.cpp, so their descriptors remain hand-written there.

defm AMDGPU_SGPRRegAllocNPM : EnumField<
    "sgpr-regalloc-npm", AMDGPURegAllocTypeEnum, "RegAllocType::Default",
    "SGPR register allocator (new pass manager)", /*hidden=*/1>;

defm AMDGPU_VGPRRegAllocNPM : EnumField<
    "vgpr-regalloc-npm", AMDGPURegAllocTypeEnum, "RegAllocType::Default",
    "VGPR register allocator (new pass manager)", /*hidden=*/1>;

defm AMDGPU_WWMRegAllocNPM : EnumField<
    "wwm-regalloc-npm", AMDGPURegAllocTypeEnum, "RegAllocType::Default",
    "WWM register allocator (new pass manager)", /*hidden=*/1>;

defm AMDGPU_DefaultAMDHSACodeObjectVersion : ValueField<
    "amdhsa-code-object-version", "unsigned", "6",
    "Set default AMDHSA Code Object Version (module flag or asm "
    "directive still take priority if present)", /*hidden=*/1>;

// --- AMDGPUTargetMachine.cpp ---

defm AMDGPU_EnableMachinePipeliner : BoolField<
    "amdgpu-enable-pipeliner", "false",
    "Enable Machine Pipeliner for AMDGCN", /*hidden=*/1>;

// --- AMDGPUCoExecSchedStrategy.cpp ---

defm AMDGPU_BlockCarriedLatency : EnumField<
    "amdgpu-block-carried-latency", CarriedLatencyEnum, "CarriedLatency::Off",
    "Estimate block-carried latency and include it in the effective "
    "candidate stall cost.", /*hidden=*/1>;
