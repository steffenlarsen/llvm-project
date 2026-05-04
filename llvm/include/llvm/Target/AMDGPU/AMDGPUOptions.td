//===- AMDGPUOptions.td - CLV2 option defs for AMDGPU -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def AMDGPUOptsReg : Registry<"llvm::amdgpu_opts"> { let OutOfLineGetters = 1; }

//===----------------------------------------------------------------------===//
// Enum types
//===----------------------------------------------------------------------===//

// --- LDSLoweringKind ---

def LDSLoweringModule : EnumValue<"module", ""> {
  let CppName = "module";
}

def LDSLoweringTable : EnumValue<"table", ""> {
  let CppName = "table";
}

def LDSLoweringKernel : EnumValue<"kernel", ""> {
  let CppName = "kernel";
}

def LDSLoweringHybrid : EnumValue<"hybrid", ""> {
  let CppName = "hybrid";
}

def LDSLoweringKind : EnumType<"LDSLoweringKind",
    [LDSLoweringModule, LDSLoweringTable, LDSLoweringKernel,
     LDSLoweringHybrid]>;

// --- AMDGPUScanOptions ---

def AMDGPUScanDPP : EnumValue<"DPP", ""> {
  let CppName = "DPP";
}

def AMDGPUScanIterative : EnumValue<"Iterative", ""> {
  let CppName = "Iterative";
}

def AMDGPUScanNone : EnumValue<"None", ""> {
  let CppName = "None";
}

def AMDGPUScanOptions : EnumType<"AMDGPUScanOptions",
    [AMDGPUScanDPP, AMDGPUScanIterative, AMDGPUScanNone]>;

// --- AMDGPURegAllocType ---
// Original C++ enum: { Unset=0, Default=1, Basic=2, Fast=3, Greedy=4, PBQP=5 }
// Unset is not exposed on the CLI but must be present to keep numeric values.

def AMDGPURegAllocUnset : EnumValue<"__unset", ""> {
  let CppName = "Unset";
  let Value = 0;
}

def AMDGPURegAllocDefault : EnumValue<"default", ""> {
  let CppName = "Default";
  let Value = 1;
}

def AMDGPURegAllocBasic : EnumValue<"basic", ""> {
  let CppName = "Basic";
  let Value = 2;
}

def AMDGPURegAllocFast : EnumValue<"fast", ""> {
  let CppName = "Fast";
  let Value = 3;
}

def AMDGPURegAllocGreedy : EnumValue<"greedy", ""> {
  let CppName = "Greedy";
  let Value = 4;
}

def AMDGPURegAllocPBQP : EnumValue<"pbqp", ""> {
  let CppName = "PBQP";
  let Value = 5;
}

def AMDGPURegAllocType : EnumType<"AMDGPURegAllocType",
    [AMDGPURegAllocUnset, AMDGPURegAllocDefault, AMDGPURegAllocBasic,
     AMDGPURegAllocFast, AMDGPURegAllocGreedy, AMDGPURegAllocPBQP]>;

//===----------------------------------------------------------------------===//
// List options
//===----------------------------------------------------------------------===//

def AMDGPU_UseNative : ListOption<
    "amdgpu-use-native",
    "",
    "std::string"> {
  let Registry = AMDGPUOptsReg;
  let Hidden = 1;
  let CommaSeparated = 1;
  let ValueOptional = 1;
}

//===----------------------------------------------------------------------===//
// Scalar options
//===----------------------------------------------------------------------===//

def AMDGPU_StressCalls : Option<
    "amdgpu-stress-function-calls",
    "Force all functions to be noinline",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_IndirectCallSpecializationThreshold : Option<
    "amdgpu-indirect-call-specialization-threshold",
    "A threshold controls whether an indirect call will be specialized",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "3";
}

def AMDGPU_BarrierSignalWaitLatency : Option<
    "amdgpu-barrier-signal-wait-latency",
    "Synthetic latency between S_BARRIER_SIGNAL and S_BARRIER_WAIT "
    "to encourage scheduling independent work between them",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "16";
  let Hidden = 1;
}

def AMDGPU_WidenConstantLoads : Option<
    "amdgpu-codegenprepare-widen-constant-loads",
    "Widen sub-dword constant address space loads in AMDGPUCodeGenPrepare",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let ReallyHidden = 1;
}

def AMDGPU_BreakLargePHIs : Option<
    "amdgpu-codegenprepare-break-large-phis",
    "Break large PHI nodes for DAGISel",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let ReallyHidden = 1;
}

def AMDGPU_ForceBreakLargePHIs : Option<
    "amdgpu-codegenprepare-force-break-large-phis",
    "For testing purposes, always break large PHIs even if it "
    "isn't profitable.",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let ReallyHidden = 1;
}

def AMDGPU_BreakLargePHIsThreshold : Option<
    "amdgpu-codegenprepare-break-large-phis-threshold",
    "Minimum type size in bits for breaking large PHI nodes",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "32";
  let ReallyHidden = 1;
}

def AMDGPU_UseMul24Intrin : Option<
    "amdgpu-codegenprepare-mul24",
    "Introduce mul24 intrinsics in AMDGPUCodeGenPrepare",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let ReallyHidden = 1;
}

def AMDGPU_ExpandDiv64InIR : Option<
    "amdgpu-codegenprepare-expand-div64",
    "Expand 64-bit division in AMDGPUCodeGenPrepare",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let ReallyHidden = 1;
}

def AMDGPU_DisableIDivExpand : Option<
    "amdgpu-codegenprepare-disable-idiv-expansion",
    "Prevent expanding integer division in AMDGPUCodeGenPrepare",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_DisableFDivExpand : Option<
    "amdgpu-codegenprepare-disable-fdiv-expansion",
    "Prevent expanding floating point division in AMDGPUCodeGenPrepare",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_DumpHSAMetadata : Option<
    "amdgpu-dump-hsa-metadata",
    "Dump AMDGPU HSA Metadata",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
}

def AMDGPU_VerifyHSAMetadata : Option<
    "amdgpu-verify-hsa-metadata",
    "Verify AMDGPU HSA Metadata",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
}

def AMDGPU_EnableExactSolver : Option<
    "amdgpu-igrouplp-exact-solver",
    "Whether to use the exponential time solver to fit the instructions "
    "to the pipeline as closely as possible.",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_CutoffForExact : Option<
    "amdgpu-igrouplp-exact-solver-cutoff",
    "The maximum number of scheduling group conflicts which we attempt "
    "to solve with the exponential time exact solver. Problem sizes "
    "greater than this will be solved by the less accurate greedy "
    "algorithm. Selecting solver by size is superseded by manually "
    "selecting the solver (e.g. by amdgpu-igrouplp-exact-solver",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_MaxBranchesExplored : Option<
    "amdgpu-igrouplp-exact-solver-max-branches",
    "The amount of branches that we are willing to explore with "
    "the exact algorithm before giving up.",
    "uint64_t"> {
  let Registry = AMDGPUOptsReg;
  let Default = "uint64_t{0}";
  let Hidden = 1;
}

def AMDGPU_UseCostHeur : Option<
    "amdgpu-igrouplp-exact-solver-cost-heur",
    "Whether to use the cost heuristic to make choices as we traverse "
    "the search space using the exact solver. Defaulted to on, and if "
    "turned off, we will use the node order -- attempting to put the "
    "later nodes in the later sched groups. Experimentally, results are "
    "mixed, so this should be set on a case-by-case basis.",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_BypassSlowDiv : Option<
    "amdgpu-bypass-slow-div",
    "Skip 64-bit divide for dynamic 32-bit values",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
}

def AMDGPU_LateWidenConstantLoads : Option<
    "amdgpu-late-codegenprepare-widen-constant-loads",
    "Widen sub-dword constant address space loads in "
    "AMDGPULateCodeGenPrepare",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let ReallyHidden = 1;
}

def AMDGPU_EnableNewLegality : Option<
    "amdgpu-global-isel-new-legality",
    "Use GlobalISel desired legality, rather than try to use "
    "rules compatible with selection patterns",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let ReallyHidden = 1;
}

def AMDGPU_EnablePreLink : Option<
    "amdgpu-prelink",
    "Enable pre-link mode optimizations",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_EnableOCLManglingMismatchWA : Option<
    "amdgpu-enable-ocl-mangling-mismatch-workaround",
    "Enable the workaround for OCL name mangling mismatch.",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let ReallyHidden = 1;
}

def AMDGPU_SuperAlignLDSGlobals : Option<
    "amdgpu-super-align-lds-globals",
    "Increase alignment of LDS if it is not on align boundary",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- Enum option: LDSLoweringKind ---

def AMDGPU_LowerModuleLDSStrategy : Option<
    "amdgpu-lower-module-lds-strategy",
    "Specify lowering strategy for function LDS access:",
    "LDSLoweringKind"> {
  let Registry = AMDGPUOptsReg;
  let Default = "hybrid";
  let Hidden = 1;
}

def AMDGPU_DistanceCacheEnabled : Option<
    "amdgpu-next-use-analysis-distance-cache",
    "Enable live-reg-use distance cache",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_DumpNextUseDistanceAsJson : Option<
    "amdgpu-next-use-analysis-dump-distance-as-json",
    "",
    "std::string"> {
  let Registry = AMDGPUOptsReg;
  let Hidden = 1;
}

def AMDGPU_DumpNextUseDistanceDefToUse : Option<
    "amdgpu-next-use-analysis-dump-distance-def-to-use",
    "",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_DumpNextUseDistanceVerbose : Option<
    "amdgpu-next-use-analysis-dump-distance-verbose",
    "",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_ConfigPreset : Option<
    "amdgpu-next-use-analysis-config",
    "Config preset: 'graphics' or 'compute'",
    "std::string"> {
  let Registry = AMDGPUOptsReg;
  let Default = "\"graphics\"";
  let Hidden = 1;
}

def AMDGPU_ConfigCountPhis : Option<
    "amdgpu-next-use-analysis-count-phis",
    "Count PHI instructions toward distance and block size",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Hidden = 1;
}

def AMDGPU_ConfigForwardOnly : Option<
    "amdgpu-next-use-analysis-forward-only",
    "Restrict inter-block distances to forward-reachable paths",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Hidden = 1;
}

def AMDGPU_ConfigPreciseUseModeling : Option<
    "amdgpu-next-use-analysis-precise-use-modeling",
    "Model PHI uses via incoming edge block with loop-aware "
    "reachability filtering",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Hidden = 1;
}

def AMDGPU_ConfigPromoteToPreheader : Option<
    "amdgpu-next-use-analysis-use-preheader-model",
    "Promote loop-entry and inner-loop uses to the loop preheader",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Hidden = 1;
}

def AMDGPU_MemBoundThresh : Option<
    "amdgpu-membound-threshold",
    "Function mem bound threshold in %",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "50";
  let Hidden = 1;
}

def AMDGPU_LimitWaveThresh : Option<
    "amdgpu-limit-wave-threshold",
    "Kernel limit wave threshold in %",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "50";
  let Hidden = 1;
}

def AMDGPU_IAWeight : Option<
    "amdgpu-indirect-access-weight",
    "Indirect access memory instruction weight",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1000";
  let Hidden = 1;
}

def AMDGPU_LSWeight : Option<
    "amdgpu-large-stride-weight",
    "Large stride memory access weight",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1000";
  let Hidden = 1;
}

def AMDGPU_LargeStrideThresh : Option<
    "amdgpu-large-stride-threshold",
    "Large stride memory access threshold",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "64";
  let Hidden = 1;
}

def AMDGPU_KernargPreloadCount : Option<
    "amdgpu-kernarg-preload-count",
    "How many kernel arguments to preload onto SGPRs",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
}

def AMDGPU_EnableKernargPreload : Option<
    "amdgpu-kernarg-preload",
    "Enable preload kernel arguments to SGPRs",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
}

def AMDGPU_DisablePromoteAllocaToVector : Option<
    "disable-promote-alloca-to-vector",
    "Disable promote alloca to vector",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
}

def AMDGPU_DisablePromoteAllocaToLDS : Option<
    "disable-promote-alloca-to-lds",
    "Disable promote alloca to LDS",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
}

def AMDGPU_PromoteAllocaToVectorLimit : Option<
    "amdgpu-promote-alloca-to-vector-limit",
    "Maximum byte size to consider promote alloca to vector",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
}

def AMDGPU_PromoteAllocaToVectorMaxRegs : Option<
    "amdgpu-promote-alloca-to-vector-max-regs",
    "Maximum vector size (in 32b registers) to use when promoting alloca",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "32";
}

def AMDGPU_PromoteAllocaToVectorVGPRRatio : Option<
    "amdgpu-promote-alloca-to-vector-vgpr-ratio",
    "Ratio of VGPRs to budget for promoting alloca to vectors",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "4";
}

def AMDGPU_LoopUserWeight : Option<
    "promote-alloca-vector-loop-user-weight",
    "The bonus weight of users of allocas within loop when sorting "
    "profitable allocas",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "4";
}

def AMDGPU_AssumedStackSizeForExternalCall : Option<
    "amdgpu-assume-external-call-stack-size",
    "Assumed stack use of any external call (in bytes)",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "16384";
  let Hidden = 1;
}

def AMDGPU_AssumedStackSizeForDynamicSizeObjects : Option<
    "amdgpu-assume-dynamic-stack-object-size",
    "Assumed extra stack use if there are any variable sized objects "
    "(in bytes)",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "4096";
  let Hidden = 1;
}

def AMDGPU_AnyAddressSpace : Option<
    "amdgpu-any-address-space-out-arguments",
    "Replace pointer out arguments with struct returns for non-private "
    "address space",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_MaxNumRetRegs : Option<
    "amdgpu-max-return-arg-num-regs",
    "Approximately limit number of return registers for replacing "
    "out arguments",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "16";
  let Hidden = 1;
}

def AMDGPU_DefaultVALUInstsThreshold : Option<
    "amdgpu-set-wave-priority-valu-insts-threshold",
    "VALU instruction count threshold for adjusting wave priority",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "100";
  let Hidden = 1;
}

def AMDGPU_AsanInstrumentLDS : Option<
    "amdgpu-asan-instrument-lds",
    "Run asan instrumentation on LDS instructions lowered to global memory",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_EnableFunctionCalls : Option<
    "amdgpu-function-calls",
    "Enable AMDGPU function call support",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_EnableObjectLinking : Option<
    "amdgpu-enable-object-linking",
    "Enable object linking for cross-TU LDS and ABI support",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_EnableLowerModuleLDS : Option<
    "amdgpu-enable-lower-module-lds",
    "Enable lower module lds pass",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_EnableEarlyIfConversion : Option<
    "amdgpu-early-ifcvt",
    "Run early if-conversion",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_OptExecMaskPreRA : Option<
    "amdgpu-opt-exec-mask-pre-ra",
    "Run pre-RA exec mask optimizations",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_LowerCtorDtor : Option<
    "amdgpu-lower-global-ctor-dtor",
    "Lower GPU ctor / dtors to globals on the device.",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_EnableLoadStoreVectorizer : Option<
    "amdgpu-load-store-vectorizer",
    "Enable load store vectorizer",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_ScalarizeGlobal : Option<
    "amdgpu-scalarize-global-loads",
    "Enable global load scalarization",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_InternalizeSymbols : Option<
    "amdgpu-internalize-symbols",
    "Enable elimination of non-kernel functions and unused globals",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_EarlyInlineAll : Option<
    "amdgpu-early-inline-all",
    "Inline all functions early",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_RemoveIncompatibleFunctions : Option<
    "amdgpu-enable-remove-incompatible-functions",
    "Enable removal of functions when they use features not supported "
    "by the target GPU",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_EnableSDWAPeephole : Option<
    "amdgpu-sdwa-peephole",
    "Enable SDWA peepholer",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
}

def AMDGPU_EnableDPPCombine : Option<
    "amdgpu-dpp-combine",
    "Enable DPP combiner",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
}

def AMDGPU_EnableAMDGPUAliasAnalysis : Option<
    "enable-amdgpu-aa",
    "Enable AMDGPU Alias Analysis",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_EnableLibCallSimplify : Option<
    "amdgpu-simplify-libcall",
    "Enable amdgpu library simplifications",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_XnackSetting : Option<
    "amdgpu-xnack",
    "Force amdgpu.xnack value for testing",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let ReallyHidden = 1;
}

def AMDGPU_SramEccSetting : Option<
    "amdgpu-sramecc",
    "Force amdgpu.sramecc for testing",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let ReallyHidden = 1;
}

def AMDGPU_EnableLowerKernelArguments : Option<
    "amdgpu-ir-lower-kernel-arguments",
    "Lower kernel argument loads in IR pass",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_EnableRegReassign : Option<
    "amdgpu-reassign-regs",
    "Enable register reassign optimizations on gfx10+",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_OptVGPRLiveRange : Option<
    "amdgpu-opt-vgpr-liverange",
    "Enable VGPR liverange optimizations for if-else structure",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- Enum option: AMDGPUScanOptions ---

def AMDGPU_AtomicOptimizerStrategy : Option<
    "amdgpu-atomic-optimizer-strategy",
    "Select DPP or Iterative strategy for scan",
    "AMDGPUScanOptions"> {
  let Registry = AMDGPUOptsReg;
  let Default = "Iterative";
}

def AMDGPU_EnableSIModeRegisterPass : Option<
    "amdgpu-mode-register",
    "Enable mode register pass",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_EnableInsertDelayAlu : Option<
    "amdgpu-enable-delay-alu",
    "Enable s_delay_alu insertion",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_EnableVOPD : Option<
    "amdgpu-enable-vopd",
    "Enable VOPD, dual issue of VALU in wave32",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_EnableDCEInRA : Option<
    "amdgpu-dce-in-ra",
    "Enable machine DCE inside regalloc",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_EnableSetWavePriority : Option<
    "amdgpu-set-wave-priority",
    "Adjust wave priority",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_EnableScalarIRPasses : Option<
    "amdgpu-scalar-ir-passes",
    "Enable scalar IR passes",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_EnableLowerExecSync : Option<
    "amdgpu-enable-lower-exec-sync",
    "Enable lowering of execution synchronization.",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_EnableSwLowerLDS : Option<
    "amdgpu-enable-sw-lower-lds",
    "Enable lowering of lds to global memory pass and asan instrument "
    "resulting IR.",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_EnablePreRAOptimizations : Option<
    "amdgpu-enable-pre-ra-optimizations",
    "Enable Pre-RA optimizations pass",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_EnablePromoteKernelArguments : Option<
    "amdgpu-enable-promote-kernel-arguments",
    "Enable promotion of flat kernel pointer arguments to global",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_EnableImageIntrinsicOptimizer : Option<
    "amdgpu-enable-image-intrinsic-optimizer",
    "Enable image intrinsic optimizer pass",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_EnableLoopPrefetch : Option<
    "amdgpu-loop-prefetch",
    "Enable loop data prefetch on AMDGPU",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_SchedStrategy : Option<
    "amdgpu-sched-strategy",
    "Select custom AMDGPU scheduling strategy.",
    "std::string"> {
  let Registry = AMDGPUOptsReg;
  let Default = "\"\"";
  let Hidden = 1;
}

def AMDGPU_EnableRewritePartialRegUses : Option<
    "amdgpu-enable-rewrite-partial-reg-uses",
    "Enable rewrite partial reg uses pass",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_EnableHipStdPar : Option<
    "amdgpu-enable-hipstdpar",
    "Enable HIP Standard Parallelism Offload support",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_EnableAMDGPUAttributor : Option<
    "amdgpu-attributor-enable",
    "Enable AMDGPUAttributorPass",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_NewRegBankSelect : Option<
    "new-reg-bank-select",
    "Run amdgpu-regbankselect and amdgpu-regbanklegalize instead of "
    "regbankselect",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_HasClosedWorldAssumption : Option<
    "amdgpu-link-time-closed-world",
    "Whether has closed-world assumption at link time",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_EnableUniformIntrinsicCombine : Option<
    "amdgpu-enable-uniform-intrinsic-combine",
    "Enable/Disable the Uniform Intrinsic Combine Pass",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_UnrollThresholdPrivate : Option<
    "amdgpu-unroll-threshold-private",
    "Unroll threshold for AMDGPU if private memory used in a loop",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "2700";
  let Hidden = 1;
}

def AMDGPU_UnrollThresholdLocal : Option<
    "amdgpu-unroll-threshold-local",
    "Unroll threshold for AMDGPU if local memory used in a loop",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1000";
  let Hidden = 1;
}

def AMDGPU_UnrollThresholdIf : Option<
    "amdgpu-unroll-threshold-if",
    "Unroll threshold increment for AMDGPU for each if statement "
    "inside loop",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "200";
  let Hidden = 1;
}

def AMDGPU_UnrollRuntimeLocal : Option<
    "amdgpu-unroll-runtime-local",
    "Allow runtime unroll for AMDGPU if local memory used in a loop",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_UnrollMaxBlockToAnalyze : Option<
    "amdgpu-unroll-max-block-to-analyze",
    "Inner loop block size threshold to analyze in unroll for AMDGPU",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "32";
  let Hidden = 1;
}

def AMDGPU_ArgAllocaCost : Option<
    "amdgpu-inline-arg-alloca-cost",
    "Cost of alloca argument",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "4000";
  let Hidden = 1;
}

def AMDGPU_ArgAllocaCutoff : Option<
    "amdgpu-inline-arg-alloca-cutoff",
    "Maximum alloca size to use for inline cost",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "256";
  let Hidden = 1;
}

def AMDGPU_InlineMaxBB : Option<
    "amdgpu-inline-max-bb",
    "Maximum number of BBs allowed in a function after inlining "
    "(compile time constraint)",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1100";
  let Hidden = 1;
}

def AMDGPU_MemcpyLoopUnroll : Option<
    "amdgpu-memcpy-loop-unroll",
    "Unroll factor (affecting 4x32-bit operations) to use for memory "
    "operations when lowering statically-sized memcpy, memmove, or "
    "memset as a loop",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "16";
  let Hidden = 1;
}

def AMDGPU_CullSGPRHazardsOnFunctionBoundary : Option<
    "amdgpu-sgpr-hazard-boundary-cull",
    "Cull hazards on function boundaries",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_CullSGPRHazardsAtMemWait : Option<
    "amdgpu-sgpr-hazard-mem-wait-cull",
    "Cull hazards on memory waits",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_CullSGPRHazardsMemWaitThreshold : Option<
    "amdgpu-sgpr-hazard-mem-wait-cull-threshold",
    "Number of tracked SGPRs before initiating hazard cull on memory wait",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "8";
  let Hidden = 1;
}

def AMDGPU_MFMAPaddingRatio : Option<
    "amdgpu-mfma-padding-ratio",
    "Fill a percentage of the latency between neighboring MFMA with "
    "s_nops.",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_NopPadding : Option<
    "amdgpu-snop-padding",
    "Insert a s_nop x before every instruction",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_EnableWMMAVnopHoisting : Option<
    "amdgpu-wmma-vnop-hoisting",
    "Hoist WMMA hazard V_NOPs from loops to preheaders",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_LongBranchFactor : Option<
    "amdgpu-long-branch-factor",
    "Factor to apply to what qualifies as a long branch to reserve a "
    "pair of scalar registers. If this value is 0 the long branch "
    "registers are never reserved. As this value grows the greater "
    "chance the branch distance will fall within the threshold and the "
    "registers will be marked to be reserved. We lean towards always "
    "reserving a register for long jumps",
    "double"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1.0";
  let Hidden = 1;
}

def AMDGPU_UseDownwardTracker : Option<
    "amdgpu-print-rp-downward",
    "Use GCNDownwardRPTracker for GCNRegPressurePrinter pass",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_DisableUnclusterHighRP : Option<
    "amdgpu-disable-unclustered-high-rp-reschedule",
    "Disable unclustered high register pressure reduction scheduling "
    "stage.",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_DisableClusteredLowOccupancy : Option<
    "amdgpu-disable-clustered-low-occupancy-reschedule",
    "Disable clustered low occupancy rescheduling for ILP scheduling "
    "stage.",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_ScheduleMetricBias : Option<
    "amdgpu-schedule-metric-bias",
    "Sets the bias which adds weight to occupancy vs latency. Set it "
    "to 100 to chase the occupancy only.",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "10";
  let Hidden = 1;
}

def AMDGPU_RelaxedOcc : Option<
    "amdgpu-schedule-relaxed-occupancy",
    "Relax occupancy targets for kernels which are memory bound "
    "(amdgpu-membound-threshold), or Wave Limited "
    "(amdgpu-limit-wave-threshold).",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_GCNTrackers : Option<
    "amdgpu-use-amdgpu-trackers",
    "Use the AMDGPU specific RPTrackers during scheduling",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_PendingQueueLimit : Option<
    "amdgpu-scheduler-pending-queue-limit",
    "Max (Available+Pending) size to inspect pending queue (0 disables)",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "256";
  let Hidden = 1;
}

def AMDGPU_DisableRewriteMFMAFormSchedStage : Option<
    "amdgpu-disable-rewrite-mfma-form-sched-stage",
    "Disable rewrite mfma rewrite scheduling stage",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_PrintMaxRPRegUsageBeforeScheduler : Option<
    "amdgpu-print-max-reg-pressure-regusage-before-scheduler",
    "Print a list of live registers along with their def/uses at the "
    "point of maximum register pressure before scheduling.",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_PrintMaxRPRegUsageAfterScheduler : Option<
    "amdgpu-print-max-reg-pressure-regusage-after-scheduler",
    "Print a list of live registers along with their def/uses at the "
    "point of maximum register pressure after scheduling.",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_VGPRThresholdPercent : Option<
    "amdgpu-vgpr-threshold-percent",
    "Percent of VGPR limits that we should use as RP threshold during "
    "scheduling. We have two limits relevant to scheduling: Critical "
    "(avoid decreasing occupancy), Excess (avoid spilling). This flag "
    "scales both limits back by an equal percent: (0 = use default "
    "calculation, 1-100 = use percentage), default: 0",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
  let Validate = [{
    if (Value > 100)
      return reject("'" + llvm::Twine(Value) +
                    "' value must be in the range [0, 100]!");
    return true;
  }];
}

def AMDGPU_EnableVGPRIndexMode : Option<
    "amdgpu-vgpr-index-mode",
    "Use GPR indexing mode instead of movrel for vector indexing",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
}

def AMDGPU_UseAA : Option<
    "amdgpu-use-aa-in-codegen",
    "Enable the use of AA during codegen.",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
}

def AMDGPU_NSAThreshold : Option<
    "amdgpu-nsa-threshold",
    "Number of addresses from which to enable MIMG NSA.",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def AMDGPU_ForceGenericVersion : Option<
    "amdgpu-force-generic-version",
    "Force a specific generic_v<N> flag to be added. For testing "
    "purposes only.",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let ReallyHidden = 1;
}

def AMDGPU_EnableR600StructurizeCFG : Option<
    "r600-ir-structurize",
    "Use StructurizeCFG IR pass",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
}

def AMDGPU_EnableR600IfConvert : Option<
    "r600-if-convert",
    "Use if conversion pass",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let ReallyHidden = 1;
}

def AMDGPU_EnableM0Merge : Option<
    "amdgpu-enable-merge-m0",
    "Merge and hoist M0 initializations",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
}

def AMDGPU_MaxClause : Option<
    "amdgpu-max-memory-clause",
    "Maximum length of a memory clause, instructions",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "15";
  let Hidden = 1;
}

def AMDGPU_EnableSpillVGPRToAGPR : Option<
    "amdgpu-spill-vgpr-to-agpr",
    "Enable spilling VGPRs to AGPRs",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let ReallyHidden = 1;
}

def AMDGPU_HardClauseLengthLimit : Option<
    "amdgpu-hard-clause-length-limit",
    "Maximum number of memory instructions to place in the same hard "
    "clause",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Hidden = 1;
}

def AMDGPU_ForceEmitZeroFlag : Option<
    "amdgpu-waitcnt-forcezero",
    "Force all waitcnt instrs to be emitted as s_waitcnt vmcnt(0) "
    "expcnt(0) lgkmcnt(0)",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_ForceEmitZeroLoadFlag : Option<
    "amdgpu-waitcnt-load-forcezero",
    "Force all waitcnt load counters to wait until 0",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_ExpertSchedulingModeFlag : Option<
    "amdgpu-expert-scheduling-mode",
    "Enable expert scheduling mode 2 for all functions (GFX12+ only)",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_BranchOffsetBits : Option<
    "amdgpu-s-branch-bits",
    "Restrict range of branch instructions (DEBUG)",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "16";
  let ReallyHidden = 1;
}

def AMDGPU_Fix16BitCopies : Option<
    "amdgpu-fix-16-bit-physreg-copies",
    "Fix copies between 32 and 16 bit registers by extending to "
    "32 bit",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let ReallyHidden = 1;
}

def AMDGPU_DisableLoopAlignment : Option<
    "amdgpu-disable-loop-alignment",
    "Do not align and prefetch loops",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
}

def AMDGPU_UseDivergentRegisterIndexing : Option<
    "amdgpu-use-divergent-register-indexing",
    "Use indirect register addressing for divergent indexes",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_RemoveRedundantEndcf : Option<
    "amdgpu-remove-redundant-endcf",
    "",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let ReallyHidden = 1;
}

def AMDGPU_MaxNumVGPRsForWwmAllocation : Option<
    "amdgpu-num-vgprs-for-wwm-alloc",
    "Max num VGPRs for whole-wave register allocation.",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "10";
  let ReallyHidden = 1;
}

def AMDGPU_MFMAVGPRForm : Option<
    "amdgpu-mfma-vgpr-form",
    "Whether to force use VGPR for Opc and Dest of MFMA. If "
    "unspecified, default to compiler heuristics",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AMDGPU_SkipCacheInvalidations : Option<
    "amdgcn-skip-cache-invalidations",
    "Use this to skip inserting cache invalidating instructions.",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_EnablePreallocateSGPRSpillVGPRs : Option<
    "amdgpu-prealloc-sgpr-spill-vgprs",
    "",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_EnableSpillSGPRToVGPR : Option<
    "amdgpu-spill-sgpr-to-vgpr",
    "Enable spilling SGPRs to VGPRs",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "1";
  let ReallyHidden = 1;
}

def AMDGPU_EnableSpillCFISavedRegs : Option<
    "amdgpu-spill-cfi-saved-regs",
    "Enable spilling the registers required for CFI emission",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let ReallyHidden = 1;
}

def AMDGPU_StressVGPRLimit : Option<
    "amdgpu-stress-vgpr",
    "Limit VGPRs to N registers by reserving the rest",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_StressAGPRLimit : Option<
    "amdgpu-stress-agpr",
    "Limit AGPRs to N registers by reserving the rest",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AMDGPU_StressSGPRLimit : Option<
    "amdgpu-stress-sgpr",
    "Limit SGPRs to N registers by reserving the rest",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "0";
  let Hidden = 1;
}




// sgpr-regalloc, vgpr-regalloc and wwm-regalloc are NOT declared here: their
// valid values are the contents of file-local registries in
// AMDGPUTargetMachine.cpp, so their descriptors are hand-written there in order
// to carry a Validate body.  See the self-containment note in CLV2Options.td.

// --- Enum options: AMDGPURegAllocType ---

def AMDGPU_SGPRRegAllocNPM : Option<
    "sgpr-regalloc-npm",
    "SGPR register allocator (new pass manager)",
    "AMDGPURegAllocType"> {
  let Registry = AMDGPUOptsReg;
  let Default = "default";
  let Hidden = 1;
}

def AMDGPU_VGPRRegAllocNPM : Option<
    "vgpr-regalloc-npm",
    "VGPR register allocator (new pass manager)",
    "AMDGPURegAllocType"> {
  let Registry = AMDGPUOptsReg;
  let Default = "default";
  let Hidden = 1;
}

def AMDGPU_WWMRegAllocNPM : Option<
    "wwm-regalloc-npm",
    "WWM register allocator (new pass manager)",
    "AMDGPURegAllocType"> {
  let Registry = AMDGPUOptsReg;
  let Default = "default";
  let Hidden = 1;
}

def AMDGPU_DefaultAMDHSACodeObjectVersion : Option<
    "amdhsa-code-object-version",
    "Set default AMDHSA Code Object Version (module flag or asm "
    "directive still take priority if present)",
    "unsigned"> {
  let Registry = AMDGPUOptsReg;
  let Default = "6";
  let Hidden = 1;
}

// Apply function declaration is in AMDGPUOptionsOptInfos.h

// --- AMDGPUTargetMachine.cpp ---

def AMDGPU_EnableMachinePipeliner : Option<
    "amdgpu-enable-pipeliner",
    "Enable Machine Pipeliner for AMDGCN",
    "bool"> {
  let Registry = AMDGPUOptsReg;
  let Default = "false";
  let Hidden = 1;
}
