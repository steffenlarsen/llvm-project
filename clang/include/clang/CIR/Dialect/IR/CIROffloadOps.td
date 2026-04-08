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

    Example:
    ```
    cir.offload.module @offload_device_module {
      cir.offload.func @kernel(%arg0: !cir.ptr<!s32i>) kernel {
        ...
        cir.return
      }
    }
    ```
  }];

  let arguments = (ins
    SymbolNameAttr:$sym_name,
    OptionalAttr<StrAttr>:$sym_visibility
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
    OptionalAttr<DictArrayAttr>:$res_attrs
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

    An optional `stream` operand and optional `shmem` (dynamic shared memory
    size) operand are supported.

    Example:
    ```
    cir.offload.kernel_launch @offload_device_module::@my_kernel
      grid(%gx, %gy, %gz : !u32i)
      block(%bx, %by, %bz : !u32i)
      args(%a, %b : !s32i, !cir.ptr<!s32i>)
    ```
  }];

  let arguments = (ins
    SymbolRefAttr:$kernel,
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

#endif // CLANG_CIR_DIALECT_IR_CIROFFLOADOPS_TD
