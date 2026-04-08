//===-- OffloadBase.td - Offload dialect base definitions --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the Offload dialect, its attributes, and base classes used by
// OffloadOps.td and OffloadTypes.td.
//
// The `offload` dialect preserves the unified host+device structure of
// HIP/CUDA single-source programs through CIR-level IR, enabling cross-
// boundary analyses before the traditional host/device split.  After
// analysis, a SplitSingleSource pass produces standard gpu.module /
// gpu.func output consumed by existing GPU backends.
//
//===----------------------------------------------------------------------===//

#ifndef OFFLOAD_BASE
#define OFFLOAD_BASE

include "mlir/IR/AttrTypeBase.td"
include "mlir/IR/EnumAttr.td"
include "mlir/IR/OpBase.td"

//===----------------------------------------------------------------------===//
// Offload Dialect
//===----------------------------------------------------------------------===//

def Offload_Dialect : Dialect {
  let name = "offload";
  let cppNamespace = "::mlir::offload";
  let summary = "Single-source GPU offload dialect for HIP/CUDA programs";
  let description = [{
    The `offload` dialect represents unified host+device GPU programs before
    the host/device split.  It sits above the `gpu` dialect in the lowering
    stack: `offload.func` / `offload.kernel_launch` lower to `gpu.func` /
    `gpu.module` via the `SplitSingleSource` pass, leaving all existing GPU
    backends unchanged.

    Key design points:
    - `offload.func` carries an `exec_space` attribute (host/device/global/
      host_device) and lives flat in `builtin.module` alongside host `cir.func`
      ops, preserving single-source structure.
    - `offload.kernel_launch` is the typed, ABI-verified `<<<>>>` operator.
      The verifier statically checks that argument types match the target
      `offload.func` parameter types — a capability impossible with the
      traditional split compilation model.
    - Device index/intrinsic ops (`gpu.thread_id`, `gpu.block_id`, etc.) are
      reused *directly* inside `offload.func` bodies; nothing is duplicated.
    - Two opaque handle types (`!offload.stream`, `!offload.event`) model
      CUDA/HIP runtime handles without loss of semantics.
  }];

  let useDefaultAttributePrinterParser = 1;
  let useDefaultTypePrinterParser = 1;

  // GPU dialect provides thread/block index ops used inside offload.func.
  let dependentDialects = ["::mlir::gpu::GPUDialect"];
}

//===----------------------------------------------------------------------===//
// Offload base class helpers
//===----------------------------------------------------------------------===//

class Offload_Attr<string name, string attrMnemonic,
                   list<Trait> traits = []>
    : AttrDef<Offload_Dialect, name, traits> {
  let mnemonic = attrMnemonic;
}

class Offload_I32Enum<string name, string description,
                      list<I32EnumAttrCase> cases>
    : I32EnumAttr<name, description, cases> {
  let genSpecializedAttr = 0;
  let cppNamespace = "::mlir::offload";
}

class Offload_I32EnumAttr<string mnemonic, Offload_I32Enum enumInfo>
    : EnumAttr<Offload_Dialect, enumInfo, mnemonic> {
  let assemblyFormat = "`<` $value `>`";
}

//===----------------------------------------------------------------------===//
// ExecSpace enum  —  maps CUDA/HIP function qualifiers
//===----------------------------------------------------------------------===//

def OFFLOAD_ExecSpace_Host : I32EnumAttrCase<"host", 0, "host">;
def OFFLOAD_ExecSpace_Device : I32EnumAttrCase<"device", 1, "device">;
def OFFLOAD_ExecSpace_Global : I32EnumAttrCase<"global", 2, "global">;
def OFFLOAD_ExecSpace_HostDevice
    : I32EnumAttrCase<"host_device", 3, "host_device">;

def OFFLOAD_ExecSpaceEnum
    : Offload_I32Enum<"ExecSpace", "GPU execution space qualifier",
                      [OFFLOAD_ExecSpace_Host, OFFLOAD_ExecSpace_Device,
                       OFFLOAD_ExecSpace_Global,
                       OFFLOAD_ExecSpace_HostDevice]>;

def OFFLOAD_ExecSpaceAttr
    : Offload_I32EnumAttr<"exec_space", OFFLOAD_ExecSpaceEnum> {
  let summary = "Execution space for an offload function";
}

//===----------------------------------------------------------------------===//
// MemSpace enum  —  maps CUDA/HIP variable qualifiers to address spaces
//===----------------------------------------------------------------------===//

def OFFLOAD_MemSpace_Generic : I32EnumAttrCase<"generic", 0, "generic">;
def OFFLOAD_MemSpace_Shared : I32EnumAttrCase<"shared", 1, "shared">;
def OFFLOAD_MemSpace_Constant : I32EnumAttrCase<"constant", 2, "constant">;
def OFFLOAD_MemSpace_Device : I32EnumAttrCase<"device", 3, "device">;
def OFFLOAD_MemSpace_Managed : I32EnumAttrCase<"managed", 4, "managed">;
def OFFLOAD_MemSpace_Local : I32EnumAttrCase<"local", 5, "local">;

def OFFLOAD_MemSpaceEnum
    : Offload_I32Enum<"MemSpace", "GPU memory space qualifier",
                      [OFFLOAD_MemSpace_Generic, OFFLOAD_MemSpace_Shared,
                       OFFLOAD_MemSpace_Constant, OFFLOAD_MemSpace_Device,
                       OFFLOAD_MemSpace_Managed, OFFLOAD_MemSpace_Local]>;

def OFFLOAD_MemSpaceAttr
    : Offload_I32EnumAttr<"mem_space", OFFLOAD_MemSpaceEnum> {
  let summary = "Memory space for an offload global variable";
}

//===----------------------------------------------------------------------===//
// LaunchBoundsAttr  —  maps __launch_bounds__(maxThreads [, minBlocks])
//===----------------------------------------------------------------------===//

def OFFLOAD_LaunchBoundsAttr
    : Offload_Attr<"LaunchBounds", "launch_bounds"> {
  let summary = "Launch bounds hint for a kernel function";
  let description = [{
    Corresponds to `__launch_bounds__(maxThreadsPerBlock [, minBlocksPerSM])`
    in CUDA/HIP source.  Used by the backend to tune register allocation.
  }];
  let parameters = (ins
    "int64_t":$maxThreadsPerBlock,
    OptionalParameter<"std::optional<int64_t>">:$minBlocksPerSM
  );
  let assemblyFormat = [{
    `<` $maxThreadsPerBlock (`,` $minBlocksPerSM^)? `>`
  }];
}

//===----------------------------------------------------------------------===//
// ReqdWorkgroupSizeAttr  —  maps reqd_work_group_size(x, y, z)
//===----------------------------------------------------------------------===//

def OFFLOAD_ReqdWorkgroupSizeAttr
    : Offload_Attr<"ReqdWorkgroupSize", "reqd_work_group_size"> {
  let summary = "Required workgroup (block) size hint for a kernel function";
  let parameters = (ins
    "int64_t":$x,
    "int64_t":$y,
    "int64_t":$z
  );
  let assemblyFormat = "`<` $x `,` $y `,` $z `>`";
}

//===----------------------------------------------------------------------===//
// TargetAttr  —  dialect-level runtime + architecture metadata
//===----------------------------------------------------------------------===//

def OFFLOAD_TargetAttr : Offload_Attr<"Target", "target"> {
  let summary = "Target runtime and architecture descriptor for offload code";
  let description = [{
    Attached to the module as `offload.target`.  Identifies the runtime
    ("cuda" or "hip") and the set of target architectures (e.g. ["gfx90a"]
    or ["sm_80", "sm_90"]).
  }];
  let parameters = (ins
    "::mlir::StringAttr":$runtime,
    "::mlir::ArrayAttr":$architectures
  );
  let assemblyFormat = [{
    `<` `runtime` `=` $runtime `,` `architectures` `=` $architectures `>`
  }];
}

#endif // OFFLOAD_BASE
