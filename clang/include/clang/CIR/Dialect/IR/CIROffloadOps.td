//===-- CIROffloadOps.td - CIR offload ops ------------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// CIR ops for GPU/accelerator offloading. These replace direct usage of the
// MLIR GPU dialect in CIR codegen, using CIR types natively. They are lowered
// to GPU dialect ops by a dedicated CIR Offload → GPU lowering pass.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_CIR_DIALECT_IR_CIROFFLOADOPS_TD
#define CLANG_CIR_DIALECT_IR_CIROFFLOADOPS_TD

include "clang/CIR/Dialect/IR/CIRDialect.td"
include "clang/CIR/Dialect/IR/CIREnumAttr.td"
include "clang/CIR/Dialect/IR/CIRTypes.td"

include "mlir/IR/SymbolInterfaces.td"

//===----------------------------------------------------------------------===//
// Module and function structure
//===----------------------------------------------------------------------===//

def CIR_OffloadModuleOp : CIR_Op<"offload.module", [
  SymbolTable, Symbol, NoTerminator
]> {
  let summary = "Container for device-side code";
  let description = [{
    The `cir.offload.module` op is a container for device functions and globals,
    analogous to `gpu.module` but using CIR types natively.

    Unlike `gpu.module`, this op is NOT `IsolatedFromAbove` — CIR ops inside
    can freely reference symbols in the enclosing module. The isolation
    boundary is introduced when lowering to `gpu.module`.

    The optional `target` attribute carries the GPU architecture string
    (e.g., `"gfx90a"`, `"gfx1030"`, `"sm_80"`), enabling per-arch
    decisions in optimization passes without parsing the module name.

    Example:
    ```
    cir.offload.module @offload_device_module {target = "gfx90a"} {
      cir.offload.func @kernel(%arg0: !cir.ptr<!s32i>) kernel {
        ...
        cir.return
      }
    }
    ```
  }];

  let arguments = (ins
    SymbolNameAttr:$sym_name,
    OptionalAttr<StrAttr>:$sym_visibility,
    OptionalAttr<StrAttr>:$target
  );

  let regions = (region AnyRegion:$body);

  let assemblyFormat = [{
    $sym_name attr-dict-with-keyword $body
  }];

  let hasLLVMLowering = false;
}

def CIR_OffloadFuncOp : CIR_Op<"offload.func", [
  AutomaticAllocationScope, CallableOpInterface, FunctionOpInterface,
  IsolatedFromAbove, Symbol
]> {
  let summary = "Device function or kernel";
  let description = [{
    The `cir.offload.func` op defines a device-side function (`__device__`) or
    kernel (`__global__`). It uses `cir::FuncType` and accepts `cir.return` as
    its terminator — no separate `cir.offload.return` is needed.

    Kernels are marked with the `kernel` unit attribute.

    Example:
    ```
    cir.offload.func @my_kernel(%arg0: !s32i, %arg1: !cir.ptr<!s32i>) kernel {
      ...
      cir.return
    }
    ```
  }];

  let arguments = (ins
    SymbolNameAttr:$sym_name,
    TypeAttrOf<CIR_FuncType>:$function_type,
    UnitAttr:$kernel,
    OptionalAttr<StrAttr>:$sym_visibility,
    OptionalAttr<DictArrayAttr>:$arg_attrs,
    OptionalAttr<DictArrayAttr>:$res_attrs,
    // Carried over from the `cir.func` this was formed from.  Device code
    // leans on `__forceinline__` (always_inline) heavily, and dropping it
    // leaves the inliner to make a cost decision the programmer already made.
    OptionalAttr<CIR_InlineKind>:$inline_kind
  );

  let regions = (region AnyRegion:$body);

  let skipDefaultBuilders = 1;

  let builders = [OpBuilder<(ins
    "llvm::StringRef":$sym_name,
    "cir::FuncType":$type,
    CArg<"bool", "false">:$isKernel)
  >];

  let extraClassDeclaration = [{
    /// Whether this function is a kernel (__global__).
    bool isKernel() { return getKernelAttr() != nullptr; }

    //===------------------------------------------------------------------===//
    // CallableOpInterface
    //===------------------------------------------------------------------===//
    mlir::Region *getCallableRegion() { return isExternal() ? nullptr : &getBody(); }

    //===------------------------------------------------------------------===//
    // FunctionOpInterface
    //===------------------------------------------------------------------===//
    /// Returns the argument types of this function.
    llvm::ArrayRef<mlir::Type> getArgumentTypes() {
      return getFunctionType().getInputs();
    }
    /// Returns the result types of this function.
    llvm::ArrayRef<mlir::Type> getResultTypes() {
      return getFunctionType().getReturnTypes();
    }

    /// Whether this is an external (declaration-only) function.
    bool isExternal() { return getBody().empty(); }
  }];

  let hasCustomAssemblyFormat = 1;

  let hasLLVMLowering = false;
}

//===----------------------------------------------------------------------===//
// Kernel launch
//===----------------------------------------------------------------------===//

def CIR_OffloadKernelLaunchOp : CIR_Op<"offload.kernel_launch", [
  AttrSizedOperandSegments
]> {
  let summary = "Launch a kernel function";
  let description = [{
    The `cir.offload.kernel_launch` op launches a device kernel. Grid and block
    dimensions use CIR integer types directly. Kernel arguments are CIR-typed.

    The `kernels` attribute is an array of symbol refs, one per target module.
    For single-arch builds, the array has one entry. For multi-arch builds,
    each entry references the kernel in a per-arch offload module. The fat
    binary dispatches to the correct code object at runtime.

    An optional `stream` operand and optional `shmem` (dynamic shared memory
    size) operand are supported.

    Example:
    ```
    cir.offload.kernel_launch [@offload_device_module_gfx90a::@my_kernel]
      grid(%gx, %gy, %gz : !u32i)
      block(%bx, %by, %bz : !u32i)
      args(%a, %b : !s32i, !cir.ptr<!s32i>)
    ```
  }];

  let arguments = (ins
    ArrayAttr:$kernels,
    CIR_IntType:$gridSizeX,
    CIR_IntType:$gridSizeY,
    CIR_IntType:$gridSizeZ,
    CIR_IntType:$blockSizeX,
    CIR_IntType:$blockSizeY,
    CIR_IntType:$blockSizeZ,
    Optional<CIR_IntType>:$dynamicSharedMemorySize,
    Variadic<AnyType>:$kernelOperands,
    Optional<CIR_PointerType>:$stream
  );

  let extraClassDeclaration = [{
    /// Return the kernel leaf name (function name) from the first entry.
    mlir::StringRef getKernelLeafName() {
      auto ref = mlir::cast<mlir::SymbolRefAttr>(getKernels()[0]);
      return ref.getLeafReference().getValue();
    }
    /// Return the first kernel symbol ref (for single-arch compatibility).
    mlir::SymbolRefAttr getKernelAttr() {
      return mlir::cast<mlir::SymbolRefAttr>(getKernels()[0]);
    }
    /// Replace all kernel refs with a single new ref (used by passes that
    /// redirect launches to cloned kernels).
    void setKernelAttr(mlir::SymbolRefAttr newRef) {
      setKernelsAttr(mlir::ArrayAttr::get(getContext(), {newRef}));
    }
  }];

  let hasCustomAssemblyFormat = 1;

  let hasLLVMLowering = false;
}

//===----------------------------------------------------------------------===//
// Dimension queries
//===----------------------------------------------------------------------===//

class CIR_OffloadDimOp<string mnemonic>
    : CIR_Op<mnemonic, [Pure]> {
  let arguments = (ins CIR_OffloadDimensionAttr:$dimension);
  let results = (outs CIR_IntType:$result);
  let assemblyFormat = "$dimension attr-dict `:` type($result)";
  let hasLLVMLowering = false;
}

def CIR_OffloadThreadIdOp : CIR_OffloadDimOp<"offload.thread_id"> {
  let summary = "Thread index within the block";
  let description = [{
    Returns the index of the current thread within the block along the
    given dimension.

    ```
    %tid = cir.offload.thread_id x : !u32i
    ```
  }];
}

def CIR_OffloadBlockIdOp : CIR_OffloadDimOp<"offload.block_id"> {
  let summary = "Block index within the grid";
  let description = [{
    Returns the index of the current block within the grid along the
    given dimension.

    ```
    %bid = cir.offload.block_id x : !u32i
    ```
  }];
}

def CIR_OffloadBlockDimOp : CIR_OffloadDimOp<"offload.block_dim"> {
  let summary = "Block dimensions";
  let description = [{
    Returns the size of the current block along the given dimension.

    ```
    %bdim = cir.offload.block_dim x : !u32i
    ```
  }];
}

def CIR_OffloadGridDimOp : CIR_OffloadDimOp<"offload.grid_dim"> {
  let summary = "Grid dimensions";
  let description = [{
    Returns the size of the grid along the given dimension.

    ```
    %gdim = cir.offload.grid_dim x : !u32i
    ```
  }];
}

//===----------------------------------------------------------------------===//
// Synchronization
//===----------------------------------------------------------------------===//

def CIR_OffloadBarrierOp : CIR_Op<"offload.barrier"> {
  let summary = "Workgroup barrier";
  let description = [{
    Synchronizes all work items in the workgroup. All prior memory accesses
    are made visible to all work items.

    ```
    cir.offload.barrier
    ```
  }];

  let assemblyFormat = "attr-dict";

  let hasLLVMLowering = false;
}

//===----------------------------------------------------------------------===//
// Wave / subgroup operations
//===----------------------------------------------------------------------===//

def CIR_OffloadSubgroupReduceOp : CIR_Op<"offload.subgroup_reduce", [
  Pure, SameOperandsAndResultType
]> {
  let summary = "Subgroup-level reduction";
  let description = [{
    Reduces a value across all lanes of a subgroup (wave/warp). The operand
    and result use CIR integer types directly — no type bridging is needed.

    ```
    %r = cir.offload.subgroup_reduce add %val : !u32i
    ```
  }];

  let arguments = (ins
    CIR_AnyType:$value,
    CIR_OffloadReduceKindAttr:$op,
    UnitAttr:$uniform
  );
  let results = (outs CIR_AnyType:$result);

  let assemblyFormat = [{
    $op $value (`uniform` $uniform^)? attr-dict `:` type($result)
  }];

  let hasLLVMLowering = false;
}

def CIR_OffloadSubgroupBroadcastOp : CIR_Op<"offload.subgroup_broadcast", [
  Pure
]> {
  let summary = "Subgroup-level broadcast";
  let description = [{
    Broadcasts a value from one lane to all lanes of a subgroup.

    With `first_active_lane`, the value from the first active lane is broadcast.
    With `specific_lane`, the value from the specified lane index is broadcast.

    ```
    %r = cir.offload.subgroup_broadcast %val, first_active_lane : !u32i
    %r = cir.offload.subgroup_broadcast %val, specific_lane %lane : !u32i
    ```
  }];

  let arguments = (ins
    CIR_AnyType:$src,
    Optional<CIR_IntType>:$lane,
    CIR_OffloadBroadcastKindAttr:$broadcast_kind
  );
  let results = (outs CIR_AnyType:$result);

  let hasCustomAssemblyFormat = 1;

  let hasLLVMLowering = false;
}

def CIR_OffloadBallotOp : CIR_Op<"offload.ballot", [Pure]> {
  let summary = "Subgroup ballot";
  let description = [{
    Collects a predicate value from each lane in the subgroup. The result is
    an integer where bit N is set iff lane N's predicate is true.

    The predicate is a CIR bool. The result is a CIR integer whose width
    matches the target's wavefront size (typically 32 or 64).

    ```
    %r = cir.offload.ballot %pred : !u64i
    ```
  }];

  let arguments = (ins CIR_BoolType:$predicate);
  let results = (outs CIR_IntType:$result);

  let assemblyFormat = "$predicate attr-dict `:` type($result)";

  let hasLLVMLowering = false;
}

//===----------------------------------------------------------------------===//
// Device-side memory
//===----------------------------------------------------------------------===//

def CIR_OffloadDynamicSharedMemoryOp
    : CIR_Op<"offload.dynamic_shared_memory", [Pure]> {
  let summary = "Access dynamic shared memory (workgroup/LDS)";
  let description = [{
    Returns a pointer to the start of the dynamic shared memory region
    allocated at kernel launch. The pointer type carries the workgroup
    address space and the declared element type.

    ```
    %p = cir.offload.dynamic_shared_memory : !cir.ptr<!s32i>
    ```
  }];

  let arguments = (ins);
  let results = (outs CIR_PointerType:$result);

  let assemblyFormat = "attr-dict `:` type($result)";

  let hasLLVMLowering = false;
}

//===----------------------------------------------------------------------===//
// Host-side constant memory transfer
//===----------------------------------------------------------------------===//

def CIR_OffloadMemcpyToSymbolOp
    : CIR_Op<"offload.memcpy_to_symbol"> {
  let summary = "Copy host scalar values to device constant memory";
  let description = [{
    Transfers scalar values from host memory to device constant memory
    globals. This op is inserted before a loop by Phase 1 of constant
    arg promotion (while structured loops are visible) and lowered to
    hipMemcpyToSymbol calls by Phase 2 (after merge).

    The `kernel` attribute identifies which kernel's constant args are
    being set. The `arg_indices` attribute lists which kernel operand
    indices are promoted. The `values` operand list carries the scalar
    values to transfer.

    ```
    cir.offload.memcpy_to_symbol
        kernel = @offload_device_module::@myKernel
        arg_indices = [4, 5, 6]
        values(%dx, %dy, %dz : !cir.double, !cir.double, !cir.double)
    ```
  }];

  let arguments = (ins
    SymbolRefAttr:$kernel,
    DenseI32ArrayAttr:$arg_indices,
    Variadic<AnyType>:$values
  );

  let assemblyFormat = [{
    `kernel` `=` $kernel
    `arg_indices` `=` $arg_indices
    `values` `(` $values `:` type($values) `)`
    attr-dict
  }];

  let hasLLVMLowering = false;
}

//===----------------------------------------------------------------------===//
// Host-side memory management
//===----------------------------------------------------------------------===//

def CIR_OffloadMallocOp : CIR_Op<"offload.malloc"> {
  let summary = "Allocate device memory";
  let description = [{
    Allocates device, managed, or pinned host memory. Wraps hipMalloc,
    hipMallocManaged, and hipHostMalloc with a structured kind attribute.

    ```
    %err = cir.offload.malloc %out_ptr, %size {kind = device}
               : (!cir.ptr<!cir.ptr<!void>>, !u64i) -> !s32i
    ```
  }];

  let arguments = (ins
    CIR_PointerType:$out_ptr,
    CIR_IntType:$size,
    CIR_OffloadAllocKindAttr:$kind
  );
  let results = (outs CIR_IntType:$err);

  let assemblyFormat = [{
    $out_ptr `,` $size `{` `kind` `=` $kind `}`
    attr-dict `:` `(` type($out_ptr) `,` type($size) `)` `->` type($err)
  }];

  let hasLLVMLowering = false;
}

def CIR_OffloadFreeOp : CIR_Op<"offload.free"> {
  let summary = "Free device memory";
  let description = [{
    Frees device or pinned host memory. Wraps hipFree and hipHostFree.

    ```
    %err = cir.offload.free %ptr : (!cir.ptr<!void>) -> !s32i
    ```
  }];

  let arguments = (ins CIR_PointerType:$ptr);
  let results = (outs CIR_IntType:$err);

  let assemblyFormat = [{
    $ptr attr-dict `:` `(` type($ptr) `)` `->` type($err)
  }];

  let hasLLVMLowering = false;
}

def CIR_OffloadMemcpyOp : CIR_Op<"offload.memcpy"> {
  let summary = "Copy memory between host and device";
  let description = [{
    Copies data between host and device memory. Wraps hipMemcpy and
    hipMemcpyAsync. The direction is a runtime integer matching the
    hipMemcpyKind enum (0=H2H, 1=H2D, 2=D2H, 3=D2D, 4=Default).
    If a stream operand is present, the copy is asynchronous.

    ```
    %err = cir.offload.memcpy %dst, %src, %size, %dir
               : (!cir.ptr<!void>, !cir.ptr<!void>, !u64i, !s32i) -> !s32i
    ```
  }];

  let arguments = (ins
    CIR_PointerType:$dst,
    CIR_PointerType:$src,
    CIR_IntType:$size,
    CIR_IntType:$direction,
    Optional<CIR_PointerType>:$stream
  );
  let results = (outs CIR_IntType:$err);

  let assemblyFormat = [{
    $dst `,` $src `,` $size `,` $direction
    (`stream` `(` $stream^ `:` type($stream) `)`)?
    attr-dict `:` `(` type($dst) `,` type($src) `,` type($size) `,`
    type($direction) `)` `->` type($err)
  }];

  let hasLLVMLowering = false;
}

def CIR_OffloadMemsetOp : CIR_Op<"offload.memset"> {
  let summary = "Set device memory to a value";
  let description = [{
    Sets device memory to a byte value. Wraps hipMemset.

    ```
    %err = cir.offload.memset %dst, %val, %size
               : (!cir.ptr<!void>, !s32i, !u64i) -> !s32i
    ```
  }];

  let arguments = (ins
    CIR_PointerType:$dst,
    CIR_IntType:$value,
    CIR_IntType:$size,
    Optional<CIR_PointerType>:$stream
  );
  let results = (outs CIR_IntType:$err);

  let assemblyFormat = [{
    $dst `,` $value `,` $size
    (`stream` `(` $stream^ `:` type($stream) `)`)?
    attr-dict `:` `(` type($dst) `,` type($value) `,` type($size) `)` `->` type($err)
  }];

  let hasLLVMLowering = false;
}

def CIR_OffloadDeviceSyncOp : CIR_Op<"offload.device_sync"> {
  let summary = "Synchronize with all device operations";
  let description = [{
    Waits for all previously launched device operations to complete.
    Wraps hipDeviceSynchronize / cudaDeviceSynchronize.

    ```
    %err = cir.offload.device_sync : !s32i
    ```
  }];

  let arguments = (ins);
  let results = (outs CIR_IntType:$err);

  let assemblyFormat = "attr-dict `:` type($err)";

  let hasLLVMLowering = false;
}

def CIR_OffloadStreamSyncOp : CIR_Op<"offload.stream_sync"> {
  let summary = "Synchronize with a specific stream";
  let description = [{
    Waits for all operations on the given stream to complete.
    Wraps hipStreamSynchronize / cudaStreamSynchronize.

    ```
    %err = cir.offload.stream_sync %stream : (!cir.ptr<!void>) -> !s32i
    ```
  }];

  let arguments = (ins CIR_PointerType:$stream);
  let results = (outs CIR_IntType:$err);

  let assemblyFormat = [{
    $stream attr-dict `:` `(` type($stream) `)` `->` type($err)
  }];

  let hasLLVMLowering = false;
}

#endif // CLANG_CIR_DIALECT_IR_CIROFFLOADOPS_TD
