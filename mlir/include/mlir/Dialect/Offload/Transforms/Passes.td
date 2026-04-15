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

    The handling of kernels with no observable launch site is controlled by
    `dead-kernel-action`:

    - `"none"`: place all kernels in the primary gpu.module (no secondary module).
    - `"split"` (default): place dead kernels in a secondary gpu.module
      (`deferred-gpu-module-name`), which the HIP runtime only loads when one of
      its kernels is first needed, avoiding unnecessary compile/load cost.
    - `"discard"`: drop dead kernels entirely, along with any device helpers that
      are only transitively reachable from discarded kernels.

    When `dead-kernel-action=split`, device helper functions (`exec_space = device`)
    and the device-side clone of `host_device` functions are replicated into every
    gpu.module that contains a kernel calling them, provided they do not
    transitively reference non-replicable globals (`__device__`, `__constant__`,
    `__managed__`).  Helpers that do reference such globals are placed only in the
    primary module; any dead kernel calling them stays in the primary module too.

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
           "Name of the primary gpu.module to insert shared globals into">,
    Option<"deferredGpuModuleName", "deferred-gpu-module-name", "std::string",
           /*default=*/"\"offload_device_module_deferred\"",
           "Name of the secondary (deferred) gpu.module; shared globals used "
           "exclusively by deferred kernels are placed here">
  ];

  let dependentDialects = [
    "::mlir::gpu::GPUDialect",
    "::mlir::LLVM::LLVMDialect"
  ];
}

#endif // OFFLOAD_TRANSFORMS_PASSES
