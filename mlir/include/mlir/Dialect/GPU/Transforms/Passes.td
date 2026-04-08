//===-- Passes.td - GPU pass definition file ---------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_DIALECT_GPU_PASSES
#define MLIR_DIALECT_GPU_PASSES

include "mlir/Pass/PassBase.td"

def GpuLaunchSinkIndexComputationsPass
    : Pass<"gpu-launch-sink-index-computations"> {
  let summary = "Sink index computations into gpu.launch body";
  let dependentDialects = ["mlir::gpu::GPUDialect"];
}

def GpuKernelOutliningPass : Pass<"gpu-kernel-outlining", "ModuleOp"> {
  let summary = "Outline gpu.launch bodies to kernel functions";
  let dependentDialects = ["mlir::DLTIDialect", "cf::ControlFlowDialect"];
  let options = [Option<"dataLayoutStr", "data-layout-str", "std::string",
                        /*default=*/"",
                        "String description of the data layout">];
}

def GpuAsyncRegionPass : Pass<"gpu-async-region", "func::FuncOp"> {
  let summary = "Make GPU ops async";
  let dependentDialects = ["async::AsyncDialect"];
}

def GpuMapParallelLoopsPass
    : Pass<"gpu-map-parallel-loops", "mlir::func::FuncOp"> {
  let summary = "Greedily maps loops to GPU hardware dimensions.";
  let description = [{
    Maps the parallel loops found in the given function to workgroups. The first
    loop encountered will be mapped to the global workgroup and the second loop
    encountered to the local workgroup. Within each mapping, the first three
    dimensions are mapped to x/y/z hardware ids and all following dimensions are
    mapped to sequential loops.

    Ordering of the loop mapping against the different dimensions is controlled
    by the `mapping-policy` option.
    Two policies are supported:
       1. `outermost-first` (default): the outermost loop maps to X, then Y
          and finally Z.
       2. `innermost-first`: the innermost loop maps to X, then Y and finally Z.
  }];
  let dependentDialects = ["mlir::gpu::GPUDialect"];
  let options = [Option<"mappingPolicyStr", "mapping-policy", "std::string",
                        /*default=*/"\"outermost-first\"",
                        "Policy outlining how to assign loops to GPU dimensions."
                        "Supported values are `outermost-first` and `innermost-first`.">];
}

def GpuEliminateBarriers
    : Pass<"gpu-eliminate-barriers", "mlir::func::FuncOp"> {
  let summary = "Erase unnecessary barriers";
  let description = [{
    Barrier elimination pass. If a barrier does not enforce any conflicting
    pair of memory effects, including a pair that is enforced by another
    barrier, it is unnecessary and can be removed. Adapted from
    "High-Performance GPU-to-CPU Transpilation and Optimization via High-Level
    Parallel Constructs" by Moses, Ivanov, Domke, Endo, Doerfert, and Zinenko in
    PPoPP 2023 and implementation in Polygeist.
  }];
  let dependentDialects = [
    "mlir::gpu::GPUDialect",
    "mlir::memref::MemRefDialect",
    "mlir::scf::SCFDialect"
  ];
}

def GpuDecomposeMemrefsPass : Pass<"gpu-decompose-memrefs"> {
  let summary = "Decomposes memref index computation into explicit ops.";
  let description = [{
    This pass decomposes memref index computation into explicit computations on
    sizes/strides, obtained from `memref.extract_memref_metadata` which it tries
    to place outside of `gpu.launch` body. Memrefs are then reconstructed using
    `memref.reinterpret_cast`.
    This is needed for as some targets (SPIR-V) lower memrefs to bare pointers
    and sizes/strides for dynamically-sized memrefs are not available inside
    `gpu.launch`.
  }];
  let dependentDialects = [
    "mlir::gpu::GPUDialect", "mlir::memref::MemRefDialect",
    "mlir::affine::AffineDialect"
  ];
}

def GpuModuleToBinaryPass
    : Pass<"gpu-module-to-binary", ""> {
  let summary = "Transforms a GPU module into a GPU binary.";
  let description = [{
    This pass searches for all nested GPU modules and serializes the module
    using the target attributes attached to the module, producing a GPU binary
    with an object for every target.

    The `format` argument can have the following values:
    1. `offloading`, `llvm`: produces an offloading representation.
    2. `assembly`, `isa`: produces assembly code.
    3. `binary`, `bin`: produces binaries.
    4. `fatbinary`, `fatbin`: produces fatbinaries.
  }];
  let options = [
    Option<"toolkitPath", "toolkit", "std::string", [{""}],
           "Toolkit path.">,
    ListOption<"linkFiles", "l", "std::string",
           "Extra files to link to.">,
    Option<"cmdOptions", "opts", "std::string", [{""}],
           "Command line options to pass to the tools.">,
    Option<"compilationTarget", "format", "std::string", [{"fatbin"}],
           "The target representation of the compilation process.">,
    Option<"elfSection", "section", "std::string", [{""}],
           "ELF section where binary is to be located.">
  ];
}

def GpuNVVMAttachTarget: Pass<"nvvm-attach-target", ""> {
  let summary = "Attaches an NVVM target attribute to a GPU Module.";
  let description = [{
    This pass searches for all GPU Modules in the immediate regions and attaches
    an NVVM target if the module matches the name specified by the `module` argument.

    Example:
    ```
    // File: in.mlir:
    gpu.module @nvvm_module_1 {...}
    gpu.module @nvvm_module_2 {...}
    gpu.module @rocdl_module_1 {...}
    // mlir-opt --nvvm-attach-target="module=nvvm.* chip=sm_90" in.mlir
    gpu.module @nvvm_module_1 [#nvvm.target<chip = "sm_90">] {...}
    gpu.module @nvvm_module_2 [#nvvm.target<chip = "sm_90">] {...}
    gpu.module @rocdl_module_1 {...}
    ```
  }];
  let options = [
    Option<"moduleMatcher", "module", "std::string",
           /*default=*/ [{""}],
           "Regex used to identify the modules to attach the target to.">,
    Option<"triple", "triple", "std::string",
           /*default=*/ "\"nvptx64-nvidia-cuda\"",
           "Target triple.">,
    Option<"chip", "chip", "std::string",
           /*default=*/"\"sm_75\"",
           "Target chip.">,
    Option<"features", "features", "std::string",
           /*default=*/"\"\"",
           "Target features.">,
    Option<"optLevel", "O", "unsigned",
           /*default=*/"2",
           "Optimization level.">,
    Option<"fastFlag", "fast", "bool",
           /*default=*/"false",
           "Enable fast math mode.">,
    Option<"ftzFlag", "ftz", "bool",
           /*default=*/"false",
           "Enable flush to zero for denormals.">,
    Option<"compilerDiagnosticsFlag", "collect-compiler-diagnostics", "bool",
           /*default=*/"false",
           "Enable collection of compiler diagnostics.">,
    ListOption<"linkLibs", "l", "std::string",
           "Extra bitcode libraries paths to link to.">,
    Option<"cmdOptions", "ptxas-cmd-options", "std::string",
           /*default=*/ [{""}],
           "Command line options passed to downstream compiler">,
    Option<"verifyTarget", "verify-target-arch", "bool",
           /*default=*/"true",
           "Enable verification of the target architecture">,
  ];
}

def GpuROCDLAttachTarget: Pass<"rocdl-attach-target", ""> {
  let summary = "Attaches a ROCDL target attribute to a GPU Module.";
  let description = [{
    This pass searches for all GPU Modules in the immediate regions and attaches
    a ROCDL target if the module matches the name specified by the `module` argument.

    Example:
    ```
    // File: in.mlir:
    gpu.module @nvvm_module_1 {...}
    gpu.module @nvvm_module_2 {...}
    gpu.module @rocdl_module_1 {...}
    // mlir-opt --nvvm-attach-target="module=rocdl.* chip=gfx90a" in.mlir
    gpu.module @nvvm_module_1 {...}
    gpu.module @nvvm_module_2 {...}
    gpu.module @rocdl_module_1 [#rocdl.target<chip = "gfx90a">] {...}
    ```
  }];
  let options = [
    Option<"moduleMatcher", "module", "std::string",
           /*default=*/ [{""}],
           "Regex used to identify the modules to attach the target to.">,
    Option<"triple", "triple", "std::string",
           /*default=*/ "\"amdgcn-amd-amdhsa\"",
           "Target triple.">,
    Option<"chip", "chip", "std::string",
           /*default=*/"\"gfx900\"",
           "Target chip.">,
    Option<"features", "features", "std::string",
           /*default=*/"\"\"",
           "Target features.">,
    Option<"abiVersion", "abi", "std::string",
           /*default=*/"\"600\"",
           "ABI version.">,
    Option<"optLevel", "O", "unsigned",
           /*default=*/"2",
           "Optimization level.">,
    Option<"wave64Flag", "wave64", "bool",
           /*default=*/"true",
           "Use Wave64 mode.">,
    Option<"fastFlag", "fast", "bool",
           /*default=*/"false",
           "Enable fast relaxed math opt.">,
    Option<"dazFlag", "daz", "bool",
           /*default=*/"false",
           "Enable denormals are zero opt.">,
    Option<"finiteOnlyFlag", "finite-only", "bool",
           /*default=*/"false",
           "Enable finite only opt.">,
    Option<"unsafeMathFlag", "unsafe-math", "bool",
           /*default=*/"false",
           "Enable unsafe math opt.">,
    Option<"correctSqrtFlag", "correct-sqrt", "bool",
           /*default=*/"true",
           "Enable correct rounded sqrt.">,
    ListOption<"linkLibs", "l", "std::string",
           "Extra bitcode libraries paths to link to.">,
  ];
}

def GpuSPIRVAttachTarget: Pass<"spirv-attach-target", ""> {
  let summary = "Attaches an SPIR-V target attribute to a GPU Module.";
  let description = [{
    This pass searches for all GPU Modules in the immediate regions and attaches
    an SPIR-V target if the module matches the name specified by the `module` argument.

    Example:
    ```
    // Given the following file: in1.mlir:
    gpu.module @nvvm_module_1 {...}
    gpu.module @spirv_module_1 {...}
    // With
    // mlir-opt --spirv-attach-target="module=spirv.* ver=v1.0 caps=Kernel" in1.mlir
    // it will generate,
    gpu.module @nvvm_module_1 {...}
    gpu.module @spirv_module_1 [#spirv.target<#spirv.vce<v1.0, [Kernel], []>, #spirv.resource_limits<>>] {...}
    ```
  }];
  let options = [
    Option<"moduleMatcher", "module", "std::string",
           /*default=*/ [{""}],
           "Regex used to identify the modules to attach the target to.">,
    Option<"spirvVersion", "ver", "std::string",
           /*default=*/ "\"v1.0\"",
           "SPIR-V Version.">,
    ListOption<"spirvCapabilities", "caps", "std::string",
           "List of supported SPIR-V Capabilities">,
    ListOption<"spirvExtensions", "exts", "std::string",
           "List of supported SPIR-V Extensions">,
    Option<"clientApi", "client_api", "std::string",
           /*default=*/ "\"Unknown\"",
           "Client API">,
    Option<"deviceVendor", "vendor", "std::string",
           /*default=*/ "\"Unknown\"",
           "Device Vendor">,
    Option<"deviceType", "device_type", "std::string",
           /*default=*/ "\"Unknown\"",
           "Device Type">,
    Option<"deviceId", "device_id", "uint32_t",
           /*default=*/ "",
           "Device ID">,
  ];
}

def GpuXeVMAttachTarget : Pass<"xevm-attach-target", ""> {
  let summary = "Attaches a XeVM target attribute to a GPU Module.";
  let description = [{
    This pass searches for all GPU Modules in the immediate regions and attaches
    a XeVM target if the module matches the name specified by the `module` argument.

    Example:
    ```
    // File: in.mlir:
    gpu.module @nvvm_module_1 {...}
    gpu.module @rocdl_module_2 {...}
    gpu.module @xevm_module_3 {...}
    // mlir-opt --xevm-attach-target="module=xevm.* chip=pvc" in.mlir
    gpu.module @nvvm_module_1 {...}
    gpu.module @rocdl_module_2 {...}
    gpu.module @xevm_module_3 [#xevm.target<chip = "pvc">] {...}
    ```
  }];
  let options =
      [Option<"moduleMatcher", "module", "std::string",
              /*default=*/[{""}],
              "Regex used to identify the modules to attach the target to.">,
       Option<"triple", "triple", "std::string",
              /*default=*/"\"spirv64-unknown-unknown\"", "Target triple.">,
       Option<"chip", "chip", "std::string",
              /*default=*/"\"bmg\"", "Target chip.">,
       Option<"optLevel", "O", "unsigned",
              /*default=*/"2", "Optimization level.">,
       ListOption<"linkLibs", "l", "std::string",
                  "Extra bitcode libraries paths to link to.">,
       Option<"cmdOptions", "cmd-options", "std::string",
              /*default=*/[{""}],
              "Command line options passed to downstream compiler">];
}

//===----------------------------------------------------------------------===//
// GpuSplitSingleSourcePass
//
// Splits a unified single-source gpu.module (produced by CIRGen) into primary
// and deferred gpu.modules based on launch-site visibility.
//===----------------------------------------------------------------------===//

def GpuSplitSingleSourcePass
    : Pass<"gpu-split-single-source", "::mlir::ModuleOp"> {
  let summary = "Split a single-source gpu.module into primary and deferred parts";
  let description = [{
    Classifies `gpu.func kernel` operations by launch-site visibility:

    | Kernel                        | Output module                    |
    |-------------------------------|----------------------------------|
    | Has a `gpu.launch_func` site  | Primary gpu.module               |
    | No observable launch site     | Deferred gpu.module (or dropped) |

    Device helper `gpu.func` ops (non-kernel) are cloned into every gpu.module
    that contains a kernel calling them, unless they transitively reference a
    non-replicable device global (`offload.global_var` with
    `mem_space = device/constant/managed`), in which case they are placed in
    the primary module only.

    The handling of kernels with no observable launch site is controlled by
    `dead-kernel-action`:

    - `"none"`: place all kernels in the primary gpu.module (no secondary module).
    - `"split"` (default): place dead kernels in a secondary gpu.module
      (`deferred-gpu-module-name`), which the HIP runtime only loads when one of
      its kernels is first needed, avoiding unnecessary compile/load cost.
    - `"discard"`: drop dead kernels entirely, along with any device helpers that
      are only transitively reachable from discarded kernels.

    The secondary gpu.module is omitted entirely when all kernels are referenced,
    or when `dead-kernel-action` is `"none"` or `"discard"`.

    Module names default to `"offload_device_module"` and
    `"offload_device_module_deferred"` but can be overridden via the
    `gpu-module-name` and `deferred-gpu-module-name` options.
  }];

  let options = [
    Option<"gpuModuleName", "gpu-module-name", "std::string",
           /*default=*/"\"offload_device_module\"",
           "Name of the primary gpu.module (kernels with visible launch sites)">,
    Option<"deferredGpuModuleName", "deferred-gpu-module-name", "std::string",
           /*default=*/"\"offload_device_module_deferred\"",
           "Name of the secondary gpu.module for kernels with no visible launch site">,
    Option<"deadKernelAction", "dead-kernel-action", "std::string",
           /*default=*/"\"split\"",
           "How to handle kernels with no observable launch site. "
           "\"none\": place all kernels in the primary gpu.module (no secondary module). "
           "\"split\" (default): place dead kernels in a secondary gpu.module. "
           "\"discard\": drop dead kernels and any device helpers only reachable "
           "from discarded kernels.">
  ];

  let dependentDialects = [
    "::mlir::func::FuncDialect",
    "::mlir::gpu::GPUDialect"
  ];
}

//===----------------------------------------------------------------------===//
// GpuTightenLaunchBoundsPass
//
// For each gpu.func kernel, inspects all gpu.launch_func ops that reference it
// and attempts to fold blockX * blockY * blockZ to a compile-time constant.
// When the static block total fits a smaller bound from {warpSize, 256, 512},
// a specialised clone is created with tighter known_block_size and
// rocdl.flat_work_group_size attrs, and the launch op is redirected to it.
//
// Run this pass before GpuSplitSingleSourcePass.
//===----------------------------------------------------------------------===//

def GpuTightenLaunchBoundsPass
    : Pass<"gpu-tighten-launch-bounds", "::mlir::ModuleOp"> {
  let summary = "Tighten kernel launch bounds when block size is statically known";
  let description = [{
    For each `gpu.func` with the `gpu.kernel` attribute, collects all
    `gpu.launch_func` call sites and attempts to fold the product
    `blockX * blockY * blockZ` to a compile-time constant.

    If the static block total fits within a smaller candidate bound from
    {warpSize, 256, 512}, a specialised clone is created with a name of the
    form `@kernel$maxN` (using `$` — valid in MLIR bare identifiers but not in
    standard C++ identifiers, ensuring no collision with user symbols) and
    tighter `known_block_size` and `rocdl.flat_work_group_size` attributes.
    Those launch ops are redirected to the clone.  The original kernel is always
    preserved: it may be launched by name at runtime via the HIP API
    (`hipModuleGetFunction`) in ways the IR cannot observe.

    The warp size is inferred from the `offload.target` attribute on the module.
    When no target attribute is present, only the fixed candidates {256, 512}
    are considered.

    Run this pass before `GpuSplitSingleSource`.
  }];

  let options = [
    Option<"enabled", "enabled", "bool", /*default=*/"true",
           "When false, the pass is a no-op.">,
    Option<"wavesPerEuEnabled", "waves-per-eu-enabled", "bool",
           /*default=*/"true",
           "Emit rocdl.waves_per_eu based on static occupancy analysis.">,
    Option<"aggressiveRegisterMin", "aggressive-register-min", "bool",
           /*default=*/"false",
           "Lower waves-per-eu min for compute-bound kernels.">,
    Option<"aggressiveMinFraction", "aggressive-min-fraction", "float",
           /*default=*/"0.5",
           "Fraction of K_ceiling used as min when aggressive mode is on.">
  ];
}

def GpuPropagatePointerFactsPass
    : Pass<"gpu-propagate-pointer-facts", "::mlir::ModuleOp"> {
  let summary = "Propagate pointer alignment and noalias from host allocations to kernel parameters";
  let description = [{
    For each `gpu.func` with the `gpu.kernel` attribute, traces pointer
    operands of `gpu.launch_func` call sites back to their host allocation
    roots (hipMalloc, etc.) to infer:

    1. Pointer alignment: hipMalloc returns >= 256-byte-aligned pointers.
    2. noalias: distinct allocation roots imply non-aliasing.
    3. readonly/writeonly: from scanning uses of the block argument.

    Facts are attached to a cloned kernel (or to existing `$maxN` clones from
    TightenLaunchBounds).  The original kernel is always preserved for runtime
    API callers.

    Run this pass after `GpuTightenLaunchBounds` and before
    `GpuSplitSingleSource`.
  }];

  let options = [
    Option<"enabled", "enabled", "bool", /*default=*/"true",
           "When false, the pass is a no-op.">,
    Option<"inferAlign", "infer-align", "bool", /*default=*/"true",
           "Infer and attach pointer alignment from allocation roots.">,
    Option<"inferNoalias", "infer-noalias", "bool", /*default=*/"true",
           "Infer and attach noalias when pointer roots are provably distinct.">,
    Option<"minAlignBytes", "min-align-bytes", "int", /*default=*/"16",
           "Minimum alignment in bytes to bother attaching.">
  ];
}

//===----------------------------------------------------------------------===//
// GpuSpecializeScalarArgsPass
//===----------------------------------------------------------------------===//

def GpuSpecializeScalarArgsPass
    : Pass<"gpu-specialize-scalar-args", "::mlir::ModuleOp"> {
  let summary = "Specialize scalar kernel args that are constant at all launch sites";
  let description = [{
    For each `gpu.func` with the `gpu.kernel` attribute, inspects all
    `gpu.launch_func` call sites and resolves each non-pointer scalar operand
    to a compile-time constant.  When ALL sites agree on the same value for a
    parameter, a specialised clone is created where that block argument is
    replaced (RAUW) with the constant.  The original kernel is always preserved.

    Only values arriving through the launch-argument channel are specialised.
    Constants already visible in the kernel body (`#define`, `constexpr`) are
    not touched — the backend already constant-folds them.

    Run this pass after `GpuPropagatePointerFacts` and before
    `GpuSplitSingleSource`.
  }];

  let options = [
    Option<"enabled", "enabled", "bool", /*default=*/"true",
           "When false, the pass is a no-op.">,
    Option<"specializePerSiteset", "specialize-per-siteset", "bool",
           /*default=*/"false",
           "When disjoint groups of sites share distinct constant sets, "
           "emit one clone per group.">,
    Option<"minUses", "min-uses", "int", /*default=*/"1",
           "Only specialize params with at least this many benefiting uses.">,
    Option<"specializeFloats", "specialize-floats", "bool", /*default=*/"true",
           "Also specialize float/double arguments.">
  ];
}

//===----------------------------------------------------------------------===//
// GpuMultiversionDivisibilityPass
//===----------------------------------------------------------------------===//

def GpuMultiversionDivisibilityPass
    : Pass<"gpu-multiversion-divisibility", "::mlir::ModuleOp"> {
  let summary = "Multiversion kernels with grid-stride loops for divisible trip counts";
  let description = [{
    When a scalar runtime argument (typically a problem size `n`) is used as a
    grid-stride loop bound in a kernel, emits a host-side dispatch: a fast
    clone valid under `n % VF == 0` alongside the generic original, with a
    host-side conditional choosing which to launch.

    The fast clone has an `llvm.assume` encoding the divisibility fact so the
    LLVM vectorizer can eliminate remainder/tail handling.

    Run this pass after `GpuSpecializeScalarArgs` and before
    `GpuSplitSingleSource`.
  }];

  let options = [
    Option<"enabled", "enabled", "bool", /*default=*/"true",
           "When false, the pass is a no-op.">,
    Option<"vectorFactor", "vector-factor", "int", /*default=*/"4",
           "Divisibility factor for the fast clone.">,
    Option<"cascadeFactors", "cascade-factors", "bool", /*default=*/"false",
           "Emit cascading variants for multiple factors.">,
    Option<"maxVariantsPerKernel", "max-variants-per-kernel", "int",
           /*default=*/"1",
           "Maximum number of divisibility variants per kernel.">
  ];
}

//===----------------------------------------------------------------------===//
// GpuPropagateBlockShapePass
//===----------------------------------------------------------------------===//

def GpuPropagateBlockShapePass
    : Pass<"gpu-propagate-block-shape", "::mlir::ModuleOp"> {
  let summary = "Replace gpu.block_dim reads with constants from known_block_size";
  let description = [{
    For each gpu.func kernel clone that has a `known_block_size` attribute
    (set by TightenLaunchBounds), replaces `gpu.block_dim` ops in the body
    with `arith.constant` ops carrying the corresponding dimension value.

    This enables downstream canonicalization to fold block-relative guards,
    prove fixed-size shared-tile bounds, and unroll blockDim-derived loops.

    Run this pass after `GpuTightenLaunchBounds` and before
    `GpuSplitSingleSource`.
  }];

  let options = [
    Option<"enabled", "enabled", "bool", /*default=*/"true",
           "When false, the pass is a no-op.">
  ];
}

#endif // MLIR_DIALECT_GPU_PASSES
