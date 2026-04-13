//===-- OffloadOps.td - Offload dialect operations ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines all operations for the offload dialect.
//
// Operations fall into three groups:
//   1. offload.func / offload.return  — unified host+device function body
//   2. offload.kernel_launch          — typed, ABI-verified <<<>>> expression
//   3. offload.global_var / offload.shared_mem_alloc — device-side memory
//   4. Host-side runtime ops (stream, event, sync, memcpy)
//
//===----------------------------------------------------------------------===//

#ifndef OFFLOAD_OPS
#define OFFLOAD_OPS

include "mlir/Dialect/Offload/IR/OffloadBase.td"
include "mlir/Dialect/Offload/IR/OffloadTypes.td"
include "mlir/IR/BuiltinAttributeInterfaces.td"
include "mlir/Interfaces/CallInterfaces.td"
include "mlir/Interfaces/ControlFlowInterfaces.td"
include "mlir/Interfaces/FunctionInterfaces.td"
include "mlir/Interfaces/SideEffectInterfaces.td"
include "mlir/IR/SymbolInterfaces.td"

//===----------------------------------------------------------------------===//
// Op base class
//===----------------------------------------------------------------------===//

class Offload_Op<string mnemonic, list<Trait> traits = []>
    : Op<Offload_Dialect, mnemonic, traits>;

//===----------------------------------------------------------------------===//
// offload.func
//
// A single-source function that carries an execution-space qualifier.
// Lives flat inside builtin.module alongside host cir.func / func.func ops.
// After the SplitSingleSource pass, global/device functions become gpu.func
// inside a gpu.module; host/host_device functions remain host-side.
//
// The function body may contain:
//   - gpu.thread_id, gpu.block_id, gpu.block_dim, gpu.grid_dim, gpu.barrier,
//     etc. (reused directly from the gpu dialect, no duplication)
//   - Any cir / arith / scf / memref ops valid in the given exec_space
//
// Verifier enforces:
//   - gpu.* index/intrinsic ops are only legal in device|global exec_space
//   - host-only ops are rejected inside device functions
//===----------------------------------------------------------------------===//

def Offload_FuncOp : Offload_Op<"func", [
    AffineScope,
    AutomaticAllocationScope,
    FunctionOpInterface,
    IsolatedFromAbove,
    Symbol
  ]> {
  let summary = "Unified host+device function with execution-space annotation";
  let description = [{
    Represents a HIP/CUDA function before the host/device compilation split.
    The mandatory `exec_space` attribute conveys the CUDA/HIP qualifier:

    | `exec_space`  | Source qualifier           | Lowered to            |
    |---------------|----------------------------|-----------------------|
    | `host`        | `__host__`                 | host cir.func         |
    | `device`      | `__device__`               | gpu.func (device)     |
    | `global`      | `__global__`               | gpu.func (kernel)     |
    | `host_device` | `__host__ __device__`      | cloned to both sides  |

    Example:
    ```mlir
    offload.func @vecAdd(%a: memref<f32>, %n: i32)
        exec_space = #offload.exec_space<global>
        launch_bounds = #offload.launch_bounds<256> {
      %tid = gpu.thread_id x
      // ... kernel body ...
      offload.return
    }
    ```
  }];

  let arguments = (ins
    TypeAttrOf<FunctionType>:$function_type,
    OFFLOAD_ExecSpaceAttr:$exec_space,
    OptionalAttr<OFFLOAD_LaunchBoundsAttr>:$launch_bounds,
    OptionalAttr<OFFLOAD_ReqdWorkgroupSizeAttr>:$reqd_work_group_size,
    OptionalAttr<DictArrayAttr>:$arg_attrs,
    OptionalAttr<DictArrayAttr>:$res_attrs
  );
  let regions = (region AnyRegion:$body);

  let builders = [
    OpBuilder<(ins
      "StringRef":$name,
      "FunctionType":$type,
      "offload::ExecSpace":$execSpace,
      CArg<"ArrayRef<NamedAttribute>", "{}">:$attrs
    )>
  ];

  let extraClassDeclaration = [{
    //===------------------------------------------------------------------===//
    // FunctionOpInterface implementation
    //===------------------------------------------------------------------===//
    ArrayRef<Type> getArgumentTypes() {
      return getFunctionType().getInputs();
    }
    ArrayRef<Type> getResultTypes() {
      return getFunctionType().getResults();
    }
    Region *getCallableRegion() { return &getBody(); }

    //===------------------------------------------------------------------===//
    // Exec-space predicates
    //===------------------------------------------------------------------===//
    bool isKernel() {
      return getExecSpace() == offload::ExecSpace::global;
    }
    bool isDeviceCode() {
      auto es = getExecSpace();
      return es == offload::ExecSpace::device ||
             es == offload::ExecSpace::global ||
             es == offload::ExecSpace::host_device;
    }
    bool isHostCode() {
      auto es = getExecSpace();
      return es == offload::ExecSpace::host ||
             es == offload::ExecSpace::host_device;
    }
  }];

  let hasCustomAssemblyFormat = 1;
  let hasVerifier = 1;
}

//===----------------------------------------------------------------------===//
// offload.return  — terminator for offload.func
//===----------------------------------------------------------------------===//

def Offload_ReturnOp : Offload_Op<"return", [
    Pure,
    ReturnLike,
    Terminator
  ]> {
  let summary = "Return from an offload function";
  let arguments = (ins Variadic<AnyType>:$operands);
  let assemblyFormat = "($operands^ `:` type($operands))? attr-dict";
  let hasVerifier = 1;
}

//===----------------------------------------------------------------------===//
// offload.kernel_launch  — typed, ABI-verified <<<>>> operator
//
// Statically verifies that:
//   (a) @callee resolves to an offload.func with exec_space = global
//   (b) `args` types match the callee parameter types exactly
//
// This is the key static analysis that the traditional split-compilation
// model cannot provide (the kernel stub and the device function are in
// separate translation units with no shared IR).
//===----------------------------------------------------------------------===//

def Offload_KernelLaunchOp : Offload_Op<"kernel_launch",
    [AttrSizedOperandSegments]> {
  let summary = "Launch an offload kernel (typed ABI-verified <<<>>> operator)";
  let description = [{
    Launches the kernel `$callee` with the given grid/block dimensions and
    arguments.  The verifier checks that `$callee` names an `offload.func`
    with `exec_space = global` visible in the same module, and that the
    types of `$args` match the callee's parameter types exactly.

    Example:
    ```mlir
    offload.kernel_launch @vecAdd
        grid  = (%gx, %one, %one : index, index, index)
        block = (%bsz, %one, %one : index, index, index)
        args  = (%da, %db, %dc, %n : memref<f32>, memref<f32>, memref<f32>, i32)
    ```
  }];

  let arguments = (ins
    FlatSymbolRefAttr:$callee,
    // Grid dimensions (x, y, z)
    Index:$gridX, Index:$gridY, Index:$gridZ,
    // Block dimensions (x, y, z)
    Index:$blockX, Index:$blockY, Index:$blockZ,
    // Dynamic shared memory size in bytes (0 when not specified)
    Optional<Index>:$shared_mem,
    // Optional async stream
    Optional<OFFLOAD_StreamType>:$stream,
    // Kernel arguments (types verified against callee signature)
    Variadic<AnyType>:$args
  );

  let assemblyFormat = [{
    $callee
    `grid`   `=` `(` $gridX `,` $gridY `,` $gridZ `)`
    `block`  `=` `(` $blockX `,` $blockY `,` $blockZ `)`
    (`shmem` `=` $shared_mem^)?
    (`stream` `=` $stream^ `:` type($stream))?
    (`args`   `=` `(` $args^ `:` type($args) `)`)?
    attr-dict
  }];

  let hasVerifier = 1;
}

//===----------------------------------------------------------------------===//
// offload.global_var  — device-side global variable
//
// Models __device__, __constant__, __shared__, and __managed__ globals.
// The SplitSingleSource pass lowers these to LLVM globals in the appropriate
// address space (shared=3, constant=4, device=1, managed=generic).
//===----------------------------------------------------------------------===//

def Offload_GlobalVarOp : Offload_Op<"global_var", [Symbol]> {
  let summary = "Device-side global variable with memory-space annotation";
  let description = [{
    Represents a HIP/CUDA global variable qualified with `__device__`,
    `__constant__`, `__shared__`, or `__managed__`.

    Example:
    ```mlir
    offload.global_var @lut : f32
        mem_space = #offload.mem_space<constant>
    offload.global_var @counter : i32
        mem_space = #offload.mem_space<device>
        extern_init
    ```
  }];

  let arguments = (ins
    SymbolNameAttr:$sym_name,
    TypeAttr:$type,
    OFFLOAD_MemSpaceAttr:$mem_space,
    // Set when the variable is initialised from the host via
    // cudaMemcpyToSymbol / hipMemcpyToSymbol.
    UnitAttr:$extern_init,
    // Compile-time constant initializer (scalar IntegerAttr/FloatAttr or
    // aggregate DenseElementsAttr).  HIP/CUDA Sema restricts device/constant
    // globals to constant-expression initializers, so this is always a static
    // attribute.  Absent for __shared__ variables.
    OptionalAttr<AnyAttr>:$initial_value
  );

  let assemblyFormat = [{
    $sym_name `:` $type
    `mem_space` `=` $mem_space
    (`extern_init` $extern_init^)?
    (`initial_value` `=` $initial_value^)?
    attr-dict
  }];
}

//===----------------------------------------------------------------------===//
// offload.shared_mem_alloc  — dynamic shared memory allocation
//
// Models `extern __shared__ T arr[];` inside a kernel.
// Static shared memory (fixed-size __shared__ arrays) is modelled as an
// offload.global_var with mem_space=shared.
//===----------------------------------------------------------------------===//

def Offload_SharedMemAllocOp : Offload_Op<"shared_mem_alloc", [Pure]> {
  let summary = "Dynamic shared memory allocation inside a kernel";
  let description = [{
    Allocates `$nbytes` bytes of shared memory and returns a pointer to the
    allocation.  Must only appear inside an `offload.func` with
    `exec_space = global` or `exec_space = device`.

    Corresponds to the use of `extern __shared__ char arr[];` in CUDA/HIP.
  }];

  let arguments = (ins Index:$nbytes);
  let results = (outs AnyType:$ptr);

  let assemblyFormat = [{
    $nbytes `->` type($ptr) attr-dict
  }];

  let hasVerifier = 1;
}

//===----------------------------------------------------------------------===//
// Host-side runtime operations
//===----------------------------------------------------------------------===//

//--- offload.stream_create ------------------------------------------------//

def Offload_StreamCreateOp : Offload_Op<"stream_create"> {
  let summary = "Create a new device stream (cudaStreamCreate / hipStreamCreate)";
  let results = (outs OFFLOAD_StreamType:$stream);
  let assemblyFormat = "attr-dict `:` type($stream)";
}

//--- offload.stream_destroy -----------------------------------------------//

def Offload_StreamDestroyOp : Offload_Op<"stream_destroy"> {
  let summary = "Destroy a device stream (cudaStreamDestroy / hipStreamDestroy)";
  let arguments = (ins OFFLOAD_StreamType:$stream);
  let assemblyFormat = "$stream `:` type($stream) attr-dict";
}

//--- offload.device_sync --------------------------------------------------//

def Offload_DeviceSyncOp : Offload_Op<"device_sync"> {
  let summary = "Block host until all device work completes (cudaDeviceSynchronize)";
  let assemblyFormat = "attr-dict";
}

//--- offload.stream_sync --------------------------------------------------//

def Offload_StreamSyncOp : Offload_Op<"stream_sync"> {
  let summary = "Block host until all work in a stream completes (cudaStreamSynchronize)";
  let arguments = (ins OFFLOAD_StreamType:$stream);
  let assemblyFormat = "$stream `:` type($stream) attr-dict";
}

//--- offload.event_create -------------------------------------------------//

def Offload_EventCreateOp : Offload_Op<"event_create"> {
  let summary = "Create a device event (hipEventCreate / cudaEventCreate)";
  let results = (outs OFFLOAD_EventType:$event);
  let assemblyFormat = "attr-dict `:` type($event)";
}

//--- offload.event_destroy ------------------------------------------------//

def Offload_EventDestroyOp : Offload_Op<"event_destroy"> {
  let summary = "Destroy a device event (hipEventDestroy / cudaEventDestroy)";
  let arguments = (ins OFFLOAD_EventType:$event);
  let assemblyFormat = "$event `:` type($event) attr-dict";
}

//--- offload.event_record -------------------------------------------------//

def Offload_EventRecordOp : Offload_Op<"event_record"> {
  let summary = "Record an event in a stream (hipEventRecord / cudaEventRecord)";
  let arguments = (ins OFFLOAD_EventType:$event,
                       Optional<OFFLOAD_StreamType>:$stream);
  let assemblyFormat = [{
    $event `:` type($event)
    (`,` `stream` `=` $stream^ `:` type($stream))?
    attr-dict
  }];
}

//--- offload.event_wait ---------------------------------------------------//

def Offload_EventWaitOp : Offload_Op<"event_wait"> {
  let summary = "Wait for an event (hipEventSynchronize / cudaEventSynchronize)";
  let arguments = (ins OFFLOAD_EventType:$event);
  let assemblyFormat = "$event `:` type($event) attr-dict";
}

//--- offload.stream_wait_event --------------------------------------------//

def Offload_StreamWaitEventOp : Offload_Op<"stream_wait_event"> {
  let summary = "Make a stream wait for a GPU event (hipStreamWaitEvent / cuStreamWaitEvent)";
  let description = [{
    Makes `$stream` wait until `$event` completes before executing further work.
    Corresponds to `hipStreamWaitEvent(stream, event, flags)`.

    `$flags` must be 0 (the only value currently defined by HIP/CUDA).

    Example:
    ```mlir
    offload.stream_wait_event %stream, %event flags = 0
        : !offload.stream, !offload.event
    ```
  }];
  let arguments = (ins OFFLOAD_StreamType:$stream,
                       OFFLOAD_EventType:$event,
                       I32Attr:$flags);
  let assemblyFormat = [{
    $stream `,` $event `flags` `=` $flags
    `:` type($stream) `,` type($event) attr-dict
  }];
}

//--- offload.memcpy_to_symbol ---------------------------------------------//

def Offload_MemcpyToSymbolOp : Offload_Op<"memcpy_to_symbol"> {
  let summary = "Copy host data into a device symbol (cudaMemcpyToSymbol)";
  let description = [{
    Copies `$count` elements from the host pointer `$src` into the device
    global variable `$symbol`.  Corresponds to `cudaMemcpyToSymbol` /
    `hipMemcpyToSymbol`.
  }];

  let arguments = (ins
    FlatSymbolRefAttr:$symbol,
    AnyType:$src,
    Index:$count
  );

  let assemblyFormat = [{
    $symbol `src` `=` $src `:` type($src) `count` `=` $count attr-dict
  }];

  let hasVerifier = 1;
}

//--- offload.memcpy_from_symbol -------------------------------------------//

def Offload_MemcpyFromSymbolOp : Offload_Op<"memcpy_from_symbol"> {
  let summary = "Copy device symbol data to host (cudaMemcpyFromSymbol)";
  let description = [{
    Copies `$count` elements from the device global variable `$symbol` into the
    host pointer `$dst`.  Corresponds to `cudaMemcpyFromSymbol` /
    `hipMemcpyFromSymbol`.
  }];

  let arguments = (ins
    AnyType:$dst,
    FlatSymbolRefAttr:$symbol,
    Index:$count
  );

  let assemblyFormat = [{
    $dst `:` type($dst) `,` $symbol `count` `=` $count attr-dict
  }];
}

//--- offload.malloc -------------------------------------------------------//

def Offload_MallocOp : Offload_Op<"malloc"> {
  let summary = "Allocate memory (hipMalloc / hipHostMalloc / hipMallocManaged)";
  let description = [{
    Allocates `$size` bytes of memory of the given `$alloc_type` and returns a
    pointer to it.

    | `alloc_type` | HIP call           | liboffload           |
    |--------------|--------------------|-----------------------|
    | `device`     | `hipMalloc`        | `olMemAlloc DEVICE`   |
    | `host`       | `hipHostMalloc`    | `olMemAlloc HOST`     |
    | `managed`    | `hipMallocManaged` | `olMemAlloc MANAGED`  |

    Example:
    ```mlir
    %ptr = offload.malloc %size alloc_type = #offload.alloc_type<device> : !llvm.ptr
    %pin = offload.malloc %size alloc_type = #offload.alloc_type<host>   : !llvm.ptr
    ```
  }];
  let arguments = (ins Index:$size,
                       OFFLOAD_AllocTypeAttr:$alloc_type,
                       Optional<OFFLOAD_StreamType>:$stream);
  let results   = (outs AnyType:$ptr);
  let assemblyFormat = [{
    $size `alloc_type` `=` $alloc_type
    (`,` `stream` `=` $stream^ `:` type($stream))? attr-dict `:` type($ptr)
  }];
}

//--- offload.free ---------------------------------------------------------//

def Offload_FreeOp : Offload_Op<"free"> {
  let summary = "Free memory (hipFree / hipHostFree)";
  let description = [{
    Releases memory previously allocated with `offload.malloc`.  The
    `$alloc_type` must match the type used at allocation time.

    | `alloc_type` | HIP call      | liboffload    |
    |--------------|---------------|---------------|
    | `device`     | `hipFree`     | `olMemFree`   |
    | `host`       | `hipHostFree` | `olMemFree`   |
    | `managed`    | `hipFree`     | `olMemFree`   |

    Example:
    ```mlir
    offload.free %ptr alloc_type = #offload.alloc_type<device> : !llvm.ptr
    ```
  }];
  let arguments = (ins AnyType:$ptr,
                       OFFLOAD_AllocTypeAttr:$alloc_type,
                       Optional<OFFLOAD_StreamType>:$stream);
  let assemblyFormat = [{
    $ptr `alloc_type` `=` $alloc_type
    (`,` `stream` `=` $stream^ `:` type($stream))? attr-dict `:` type($ptr)
  }];
}

//--- offload.memcpy -------------------------------------------------------//

def Offload_MemcpyOp : Offload_Op<"memcpy"> {
  let summary = "Copy memory between host and device (hipMemcpy / cudaMemcpy)";
  let description = [{
    Copies `$size` bytes from `$src` to `$dst`.  The `$dst_space` and
    `$src_space` attributes identify the allocation kind of each pointer,
    carrying the same information as `hipMemcpyKind` but in a form that
    both HIP/CUDA and liboffload lowering can consume directly.

    | `dst_space` | `src_space` | HIP kind            | liboffload             |
    |-------------|-------------|---------------------|------------------------|
    | `device`    | `host`      | `HostToDevice` (1)  | dstDev=GPU, srcDev=host|
    | `host`      | `device`    | `DeviceToHost` (2)  | dstDev=host, srcDev=GPU|
    | `device`    | `device`    | `DeviceToDevice` (3)| dstDev=GPU, srcDev=GPU |
    | `host`      | `host`      | `HostToHost` (0)    | dstDev=host, srcDev=host|
    | `managed`   | `*`         | `Default` (4)       | managed pointer path   |

    Example:
    ```mlir
    offload.memcpy %dst, %src, %size
        dst_space = #offload.alloc_type<device>
        src_space = #offload.alloc_type<host>
        : !llvm.ptr, !llvm.ptr
    ```
  }];
  let arguments = (ins AnyType:$dst,
                       AnyType:$src,
                       Index:$size,
                       OFFLOAD_AllocTypeAttr:$dst_space,
                       OFFLOAD_AllocTypeAttr:$src_space,
                       Optional<OFFLOAD_StreamType>:$stream);
  let assemblyFormat = [{
    $dst `,` $src `,` $size
    `dst_space` `=` $dst_space `src_space` `=` $src_space
    (`,` `stream` `=` $stream^ `:` type($stream))?
    attr-dict `:` type($dst) `,` type($src)
  }];
}

//--- offload.memset -------------------------------------------------------//

def Offload_MemsetOp : Offload_Op<"memset"> {
  let summary = "Fill memory with a repeating pattern (hipMemset / cudaMemset / olMemFill)";
  let description = [{
    Fills `$size` bytes at `$ptr` with copies of `$pattern`.  The width of
    `$pattern`'s type encodes the fill stride:

    | `type($pattern)` | HIP call          | liboffload PatternSize |
    |------------------|-------------------|------------------------|
    | `i8`             | `hipMemset`       | 1                      |
    | `i16`            | `hipMemsetD16`    | 2                      |
    | `i32`            | `hipMemsetD32`    | 4                      |

    HIP/CUDA lowering passes `$pattern` directly as the scalar fill value.
    A future liboffload lowering pass stack-allocates `$pattern` and passes
    a pointer with `PatternSize = sizeof(type($pattern))` to `olMemFill`,
    consuming the same op without modification.

    `$size` is always in bytes.  For typed fills (D16/D32) the caller must
    ensure `$size` is a multiple of the pattern width.

    Example:
    ```mlir
    offload.memset %dst, %val, %n_bytes : !llvm.ptr, i8
    ```
  }];
  let arguments = (ins AnyType:$ptr,
                       AnyType:$pattern,
                       Index:$size,
                       Optional<OFFLOAD_StreamType>:$stream);
  let assemblyFormat = [{
    $ptr `,` $pattern `,` $size
    (`,` `stream` `=` $stream^ `:` type($stream))?
    attr-dict `:` type($ptr) `,` type($pattern)
  }];
}

#endif // OFFLOAD_OPS
