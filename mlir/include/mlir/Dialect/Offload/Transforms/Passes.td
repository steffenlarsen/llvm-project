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
    "::mlir::func::FuncDialect"
  ];
}

#endif // OFFLOAD_TRANSFORMS_PASSES
