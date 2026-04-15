//===-- Passes.td - Offload dialect pass definitions -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef OFFLOAD_TRANSFORMS_PASSES
#define OFFLOAD_TRANSFORMS_PASSES

include "mlir/Pass/PassBase.td"

//===----------------------------------------------------------------------===//
// SplitSingleSourcePass
//
// Splits a unified host+device offload module into two standard outputs:
//   - A host-side module where offload.func (host/host_device) survive as
//     regular func.func, and offload.kernel_launch becomes gpu.launch_func.
//   - A gpu.module containing all global/device offload.func ops lowered to
//     gpu.func (with gpu.kernel attr for global functions).
//
// After this pass all offload.* ops are gone.  The host module goes through
// the normal CIR → LLVM pipeline; the gpu.module goes through the existing
// GPU → AMDGPU / NVVM pipeline unchanged.
//===----------------------------------------------------------------------===//

def OffloadSplitSingleSourcePass
    : Pass<"offload-split-single-source", "::mlir::ModuleOp"> {
  let summary = "Split a single-source offload module into host and device parts";
  let description = [{
    Classifies `offload.func` operations by `exec_space`:

    | exec_space    | Output                                       |
    |---------------|----------------------------------------------|
    | `global`      | `gpu.func` with `gpu.kernel` attr in gpu.module |
    | `device`      | `gpu.func` (non-kernel) in gpu.module        |
    | `host_device` | Cloned to both gpu.func and host func.func   |
    | `host`        | Lowered to func.func (host side only)        |

    `offload.kernel_launch` ops are replaced by `gpu.launch_func` ops that
    reference the newly-created gpu.module by name.

    The gpu.module name defaults to `"offload_device_module"` but can be
    overridden via the `gpu-module-name` option.
  }];

  let options = [
    Option<"gpuModuleName", "gpu-module-name", "std::string",
           /*default=*/"\"offload_device_module\"",
           "Name of the generated gpu.module">
  ];

  let dependentDialects = [
    "::mlir::func::FuncDialect",
    "::mlir::gpu::GPUDialect"
  ];
}

//===----------------------------------------------------------------------===//
// LowerHostRuntimePass
//
// Lowers host-side offload runtime operations to direct runtime function calls:
//   offload.stream_create      → func.call @hipStreamCreate / @cudaStreamCreate
//   offload.stream_destroy     → func.call @hipStreamDestroy
//   offload.stream_sync        → func.call @hipStreamSynchronize
//   offload.device_sync        → func.call @hipDeviceSynchronize
//   offload.memcpy_to_symbol   → func.call @hipMemcpyToSymbol
//
// This pass should run *after* SplitSingleSourcePass, on the host-side module.
//===----------------------------------------------------------------------===//

def OffloadLowerHostRuntimePass
    : Pass<"offload-lower-host-runtime", "::mlir::ModuleOp"> {
  let summary = "Lower offload host runtime ops to direct runtime API calls";
  let description = [{
    Replaces each host-side offload runtime op with a call to the corresponding
    HIP or CUDA runtime function.  The target runtime is selected via the
    `runtime` option ("hip" or "cuda").

    Declarations for the called functions are inserted into the module as
    `func.func` ops with external linkage if they are not already present.
  }];

  let options = [
    Option<"runtime", "runtime", "std::string",
           /*default=*/"\"hip\"",
           "Target runtime: \"hip\" or \"cuda\"">
  ];

  let dependentDialects = [
    "::mlir::func::FuncDialect",
    "::mlir::LLVM::LLVMDialect"
  ];
}

//===----------------------------------------------------------------------===//
// LowerSharedGlobalsPass
//
// Lowers offload.global_var ops with mem_space = shared to llvm.mlir.global
// ops with addr_space = 3 (LDS / workgroup memory) inside the gpu.module
// produced by SplitSingleSourcePass.
//
// Run this pass *after* SplitSingleSourcePass so the gpu.module exists, and
// *before* ConvertCIRInGpuModulePass which requires LLVM dialect in gpu.func
// bodies.
//===----------------------------------------------------------------------===//

def OffloadLowerSharedGlobalsPass
    : Pass<"offload-lower-shared-globals", "::mlir::ModuleOp"> {
  let summary = "Lower offload.global_var(mem_space=shared) to llvm.mlir.global in gpu.module";
  let description = [{
    For each `offload.global_var` with `mem_space = #offload.mem_space<shared>`
    in the module, emits an `llvm.mlir.global internal` with `addr_space = 3`
    (AMDGPU LDS / NVPTX shared memory) into the `gpu.module` sibling, then
    erases the `offload.global_var`.

    The type is converted from CIR to LLVM using the standard `LLVMTypeConverter`.
    Shared memory cannot have initializers in CUDA/HIP; the emitted global is
    zero-initialized (default value).
  }];

  let options = [
    Option<"gpuModuleName", "gpu-module-name", "std::string",
           /*default=*/"\"offload_device_module\"",
           "Name of the gpu.module to insert shared globals into">
  ];

  let dependentDialects = [
    "::mlir::gpu::GPUDialect",
    "::mlir::LLVM::LLVMDialect"
  ];
}

//===----------------------------------------------------------------------===//
// TightenLaunchBoundsPass
//
// Tightens kernel launch_bounds when the block size is statically known at
// every call site.  For each offload.func with exec_space=global, inspects
// all offload.kernel_launch ops that reference it and tries to fold the
// product blockX * blockY * blockZ to a compile-time constant.  If the total
// fits within a smaller candidate bound from {warpSize, 256, 512}, a
// specialised clone of the kernel is created with that tighter bound and the
// launch op is redirected to it.  The original kernel is always preserved.
//
// Run this pass *before* SplitSingleSourcePass.
//===----------------------------------------------------------------------===//

def OffloadTightenLaunchBoundsPass
    : Pass<"offload-tighten-launch-bounds", "::mlir::ModuleOp"> {
  let summary = "Tighten kernel launch_bounds when block size is statically known";
  let description = [{
    For each `offload.func` with `exec_space = global`, collects all
    `offload.kernel_launch` call sites and attempts to fold the product
    `blockX * blockY * blockZ` to a compile-time constant.

    If the static block total fits within a smaller candidate bound from
    {warpSize, 256, 512}, the kernel's `launch_bounds.maxThreadsPerBlock`
    is tightened accordingly, allowing the backend to allocate more
    registers per thread.

    For each candidate bucket that has at least one qualifying launch site,
    a specialised clone is created with a name of the form `@kernel$maxN`
    (using `$` — valid in MLIR bare identifiers but not in standard C++
    identifiers, ensuring no collision with user symbols) and a tighter
    `launch_bounds` attribute. Those launch ops are redirected to the clone.
    The original kernel is always preserved: it may be launched by name at
    runtime via the HIP API (`hipModuleGetFunction`) in ways the IR cannot
    observe, so removing it would be unsound.

    Run this pass before `SplitSingleSource`.
  }];
}

#endif // OFFLOAD_TRANSFORMS_PASSES
