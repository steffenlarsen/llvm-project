//===- FlangOptions.td - CLV2 option defs for Flang ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def FlangOptsReg : Registry<"llvm::flang_opts"> {
  let ContextTypes = [];
}

// --- Optimizer/Passes/CommandLineOpts.cpp ---

def FLANG_DynamicHeapArray : Option<
    "fdynamic-heap-array",
    "place all array allocations of dynamic size on the heap",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def FLANG_StackArraySize : Option<
    "fstack-array-size",
    "place all array allocations more than <size> elements on the heap",
    "std::size_t"> {
  let Registry = FlangOptsReg;
  let Default = "std::numeric_limits<std::size_t>::max()";
  let Hidden = 1;
}

def FLANG_IgnoreMissingTypeDesc : Option<
    "ignore-missing-type-desc",
    "ignore failures to find derived type descriptors when "
    "translating FIR to LLVM",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def FLANG_SkipExternalRttiDef : Option<
    "skip-external-rtti-definition",
    "do not define rtti static objects for types belonging to "
    "other compilation units",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def FLANG_DisableCfgConversion : Option<
    "disable-cfg-conversion", "disable FIR to CFG pass",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def FLANG_EnableAllocationPlacement : Option<
    "enable-allocation-placement",
    "enable unified array allocation placement (experimental; replaces "
    "stack-arrays and memory-allocation-opt) pass",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def FLANG_AllocationPlacementSmallArraySize : Option<
    "allocation-placement-small-array-size",
    "constant-size arrays up to <size> bytes are placed on the stack by the "
    "allocation-placement pass",
    "std::size_t"> {
  let Registry = FlangOptsReg;
  let Default = "64";
  let Hidden = 1;
}

def FLANG_AllocationPlacementStackLimit : Option<
    "allocation-placement-stack-limit",
    "per-function budget in bytes for small arrays placed on the stack by the "
    "allocation-placement pass",
    "std::size_t"> {
  let Registry = FlangOptsReg;
  let Default = "4ull * 1024 * 1024";
  let Hidden = 1;
}

def FLANG_DisableMemoryAllocOpt : Option<
    "disable-memory-allocation-opt",
    "disable memory allocation optimization pass",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def FLANG_DisableFirAliasTags : Option<
    "disable-fir-alias-tags", "disable fir alias analysis pass",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def FLANG_UseOldAliasTags : Option<
    "use-old-alias-tags",
    "Use a single TBAA tree for all functions and do not use "
    "the FIR alias tags pass",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def FLANG_EnableFirLICM : Option<
    "enable-fir-licm", "enable FIR loop invariant code motion pass",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def FLANG_DisableCodeGenRewrite : Option<
    "disable-codegen-rewrite", "disable rewrite FIR for codegen pass",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def FLANG_DisableTargetRewrite : Option<
    "disable-target-rewrite", "disable rewrite FIR for target pass",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def FLANG_DisableDebugInfo : Option<
    "disable-debug-info", "disable Add debug info pass",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def FLANG_DisableFirToLlvmIr : Option<
    "disable-fir-to-llvmir", "disable FIR to LLVM-IR dialect pass",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def FLANG_DisableLlvm : Option<
    "disable-llvm", "disable conversion to LLVM pass",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def FLANG_DisableBoxedProcRewrite : Option<
    "disable-boxed-procedure-rewrite",
    "disable rewrite boxed procedures pass",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def FLANG_EnableSafeTrampoline : Option<
    "enable-safe-trampoline",
    "enable W^X compliant runtime trampoline pool pass",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def FLANG_DisableExternalNameInterop : Option<
    "disable-external-name-interop",
    "disable convert names with external convention pass",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def FLANG_EnableConstArgGlobal : Option<
    "enable-constant-argument-globalisation",
    "enable the local constant argument to global constant conversion pass",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def FLANG_DisableCompilerGenNames : Option<
    "disable-compiler-generated-names",
    "disable replace special symbols in compiler generated names pass",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// --- Optimizer/Passes/Pipelines.cpp ---

def FLANG_ForceNoAlias : Option<
    "force-no-alias", "",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def FLANG_DisableArgFakeUse : Option<
    "disable-argument-fake-use", "",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// --- Optimizer/Transforms/LoopInvariantCodeMotion.cpp ---

def FLANG_DisableFlangLICM : Option<
    "disable-flang-licm",
    "Disable Flang's loop invariant code motion",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// --- Optimizer/Transforms/AddAliasTags.cpp ---

def FLANG_DummyArgTBAA : Option<
    "dummy-arg-tbaa", "Add TBAA tags to dummy arguments",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def FLANG_GlobalsTBAA : Option<
    "globals-tbaa", "Add TBAA tags to global variables",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def FLANG_DirectTBAA : Option<
    "direct-tbaa", "Add TBAA tags to direct variables",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def FLANG_LocalAllocTBAA : Option<
    "local-alloc-tbaa", "Add TBAA tags to local allocations.",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def FLANG_LocalAllocTBAAThreshold : Option<
    "local-alloc-tbaa-threshold",
    "If present, stops generating TBAA tags for accesses of "
    "local allocations after N accesses in a module",
    "unsigned"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let ReallyHidden = 1;
}

// --- Optimizer/Transforms/StackArrays.cpp ---

def FLANG_StackArraysMaxAllocs : Option<
    "stack-arrays-max-allocs",
    "The maximum number of heap allocations to consider in one "
    "function before skipping (to save compilation time). Set "
    "to 0 for no limit.",
    "std::size_t"> {
  let Registry = FlangOptsReg;
  let Default = "1000";
  let Hidden = 1;
}

def FLANG_StackArraysLifetime : Option<
    "stack-arrays-lifetime",
    "Add lifetime markers to generated constant size allocas",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// --- Optimizer/Transforms/FIRToMemRef.cpp ---

def FLANG_EnableFirConvertOpts : Option<
    "enable-fir-convert-opts",
    "enable emilinating redundant fir.convert in FIR-to-MemRef",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// --- Optimizer/Dialect/FIRType.cpp ---

def FLANG_EnableFirTypeAliases : Option<
    "enable-fir-type-aliases",
    "Enable pretty-printing of FIR types using type aliases",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// --- Optimizer/Support/InternalNames.cpp ---

def FLANG_MainEntryName : Option<
    "main-entry-name",
    "override the name of the default PROGRAM entry (may be "
    "helpful for using other runtimes)",
    "std::string"> {
  let Registry = FlangOptsReg;
}

// --- Optimizer/Dialect/Inliner.cpp ---

def FLANG_InlineAll : Option<
    "inline-all", "aggressively inline everything",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
}

// --- Optimizer/HLFIR/Transforms/InlineHLFIRAssign.cpp ---

def FLANG_InlineHLFIRAllocExprAssign : Option<
    "inline-hlfir-allocatable-expr-assign",
    "Enable inlining of allocatable assignments when RHS is an "
    "hlfir.expr (e.g., from hlfir.elemental)",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
}

// --- Optimizer/HLFIR/Transforms/InlineHLFIRCopyIn.cpp ---

def FLANG_NoInlineHLFIRCopy : Option<
    "no-inline-hlfir-copy",
    "Do not inline hlfir.copy operations",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
}

// --- Optimizer/HLFIR/IR/HLFIROps.cpp ---

def FLANG_StrictIntrinsicVerifier : Option<
    "strict-intrinsic-verifier",
    "use stricter verifier for HLFIR intrinsic operations",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
}

// --- Optimizer/HLFIR/Transforms/LowerHLFIROrderedAssignments.cpp ---

def FLANG_DbgOrderAssignSchedOnly : Option<
    "flang-dbg-order-assignment-schedule-only",
    "Only run ordered assignment scheduling with no codegen",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
}

// --- Optimizer/HLFIR/Transforms/SimplifyHLFIRIntrinsics.cpp ---

def FLANG_InlineMatmulAsElemental : Option<
    "flang-inline-matmul-as-elemental",
    "Expand hlfir.matmul as elemental operation",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
}

// --- Optimizer/Analysis/AliasAnalysis.cpp ---

def FLANG_UnsafeCrayPointers : Option<
    "unsafe-cray-pointers",
    "Support Cray POINTERs that ALIAS with non-TARGET data",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
}

// --- Optimizer/OpenACC/Support/FIROpenACCTypeInterfaces.cpp ---

def FLANG_OpenACCUseReductionCombineAll : Option<
    "openacc-use-reduction-combine-all",
    "Whether to generate acc.reduction_combine for all types "
    "and operators",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
}

// --- Optimizer/Builder/FIRBuilder.cpp ---

def FLANG_LengthToHashStrLit : Option<
    "length-to-hash-string-literal",
    "string literals that exceed this length will use a hash value as "
    "their symbol name",
    "std::size_t"> {
  let Registry = FlangOptsReg;
  let Default = "32";
}

// --- Optimizer/Builder/IntrinsicCall.cpp ---

def FLANG_OutlineIntrinsics : Option<
    "outline-intrinsics",
    "Lower all intrinsic procedure implementation in their own functions",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
}

def FLANG_ForceMlirComplex : Option<
    "force-mlir-complex",
    "Force using MLIR complex operations instead of libm complex operations",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
}

def FlangMathRuntimeVersionFast : EnumValue<"fast",
    "use fast runtime behavior"> {
  let CppName = "Fast";
}

def FlangMathRuntimeVersionRelaxed : EnumValue<"relaxed",
    "use relaxed runtime behavior"> {
  let CppName = "Relaxed";
}

def FlangMathRuntimeVersionPrecise : EnumValue<"precise",
    "use precise runtime behavior"> {
  let CppName = "Precise";
}

def FlangMathRuntimeVersionEnum : EnumType<"FlangMathRuntimeVersion",
    [FlangMathRuntimeVersionFast, FlangMathRuntimeVersionRelaxed,
     FlangMathRuntimeVersionPrecise]>;

def FLANG_MathRuntime : Option<
    "math-runtime",
    "Select math operations' runtime behavior:",
    "FlangMathRuntimeVersion"> {
  let Registry = FlangOptsReg;
  let Default = "fast";
}

// --- Optimizer/CodeGen/TBAABuilder.cpp ---

def FLANG_DisableTBAA : Option<
    "disable-tbaa",
    "disable attaching TBAA tags to memory accessing operations "
    "to override default Flang behavior",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
}

def FLANG_PerFunctionTBAATrees : Option<
    "per-function-tbaa-trees",
    "Give each function an independent TBAA tree (default)",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def FLANG_TBAAAttachTagMax : Option<
    "tbaa-attach-tag-max", "",
    "unsigned"> {
  let Registry = FlangOptsReg;
  let Default = "std::numeric_limits<unsigned>::max()";
}

// --- Optimizer/CodeGen/CodeGen.cpp ---

def FLANG_FirLogicalNativeOps : Option<
    "fir-logical-native-ops",
    "Use bitwise operations on the storage type for logical operations "
    "instead of normalizing to i1. Requires that all logical values "
    "are in their canonical representation.",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
}

// --- Optimizer/Dialect/FIROps.cpp ---

def FLANG_StrictFirVolatileVerifier : Option<
    "strict-fir-volatile-verifier",
    "use stricter verifier for FIR operations with volatile types",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
}

// --- Optimizer/Dialect/Support/KindMapping.cpp ---

def FLANG_KindMapping : Option<
    "kind-mapping", "kind mapping string to set kind precision",
    "std::string"> {
  let Registry = FlangOptsReg;
  let ValueDesc = "kind-mapping-string";
}

def FLANG_DefaultKinds : Option<
    "default-kinds", "string to set default kind values",
    "std::string"> {
  let Registry = FlangOptsReg;
  let ValueDesc = "default-kind-string";
}

// --- Lower/Allocatable.cpp ---

def FLANG_UseAllocRuntime : Option<
    "use-alloc-runtime", "Lower allocations to fortran runtime calls",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
}

// --- Lower/ConvertCall.cpp ---

def FLANG_UseHlfirIntrinsicOps : Option<
    "use-hlfir-intrinsic-ops",
    "Lower via HLFIR transformational intrinsic operations such "
    "as hlfir.sum",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "1";
}

// --- Lower/ConvertVariable.cpp ---

def FLANG_AllowAssumedRank : Option<
    "allow-assumed-rank", "Enable assumed rank lowering",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "1";
}

// --- Lower/Support/ReductionProcessor.cpp ---

def FLANG_ForceByrefReduction : Option<
    "force-byref-reduction",
    "Pass all reduction arguments by reference",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// --- Lower/Support/PrivateReductionUtils.cpp ---

def FLANG_EnableGpuHeapAlloc : Option<
    "enable-gpu-heap-alloc",
    "Allow the use of heap allocation for dynamically sized arrays on GPU",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
}

// --- Lower/OpenMP/Utils.cpp ---

def FLANG_OpenMPTreatIndexAsSection : Option<
    "openmp-treat-index-as-section",
    "In the OpenMP data clauses treat `a(N)` as `a(N:N)`.",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "1";
}

// --- Lower/OpenMP/Atomic.cpp ---

def FLANG_DebugDumpAtomicAnalysis : Option<
    "fdebug-dump-atomic-analysis", "",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
}

// --- Lower/OpenACC.cpp ---

def FLANG_OpenACCGenDefaultBounds : Option<
    "openacc-generate-default-bounds",
    "Whether to generate default bounds for arrays.",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
}

def FLANG_OpenACCStrideIncludeLowerExtent : Option<
    "openacc-stride-include-lower-extent",
    "Whether to include the lower dimensions extents in the stride.",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "1";
}

def FLANG_OpenACCDoLoopToAccLoop : Option<
    "openacc-do-loop-to-acc-loop",
    "Whether to lower do loops as `acc.loop` operations.",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "1";
}

def FLANG_OpenACCDoLoopInAccRoutine : Option<
    "openacc-do-loop-to-acc-loop-in-acc-routine",
    "Whether to lower do loops as `acc.loop` operations when "
    "inside an acc.routine.",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "1";
}

def FLANG_EmitIndependentAsUnstructured : Option<
    "emit-independent-loops-as-unstructured",
    "Emit independent loops as unstructured loops",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "1";
}

def FLANG_OpenACCRemapSymbols : Option<
    "openacc-remap-symbols",
    "Whether to remap symbols that appears in data clauses.",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "1";
}

// --- Lower/PFTBuilder.cpp ---

def FLANG_NoStructuredFir : Option<
    "no-structured-fir", "disable generation of structured FIR",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def FLANG_WrapUnstructuredInExecRegion : Option<
    "wrap-unstructured-constructs-in-execute-region",
    "Wrap unstructured constructs in execute_region ops",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
}

def FLANG_LowerDoWhileToScfWhile : Option<
    "lower-do-while-to-scf-while",
    "lower structured DO WHILE loops to scf.while",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// --- Lower/Bridge.cpp ---

def FLANG_AlwaysExecuteLoopBody : Option<
    "always-execute-loop-body",
    "force the body of a loop to execute at least once",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
}

// --- Support/Flags.cpp ---

def FLANG_EnableDelayedPrivatization : Option<
    "enable-delayed-privatization",
    "Emit private/local variables as clauses/specifiers on MLIR ops.",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "1";
}

def FLANG_EnableDelayedPrivatizationStaging : Option<
    "enable-delayed-privatization-staging",
    "For partially supported constructs, emit private/local "
    "variables as clauses/specifiers on MLIR ops.",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
}

// --- HLFIR/Transforms/ConvertToFIR.cpp ---

def FLANG_UseFortranAssignOnly : Option<
    "use-fortran-assign-only",
    "Do not use _FortranAAssignSimple. Only _FortranAAssign",
    "bool"> {
  let Registry = FlangOptsReg;
  let Default = "0";
}
