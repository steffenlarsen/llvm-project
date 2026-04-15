// RUN: mlir-opt --split-input-file \
// RUN:   --offload-split-single-source \
// RUN:   '--offload-lower-shared-globals=gpu-module-name=offload_device_module' \
// RUN:   %s | FileCheck %s

// Tests for OffloadLowerSharedGlobalsPass.
//
// offload.global_var with device-side mem_spaces should be lowered to
// llvm.mlir.global inside the gpu.module with the correct addr_space:
//   shared   → addr_space = 3
//   device   → addr_space = 1
//   constant → addr_space = 4 (isConstant = true)
//   managed  → addr_space = 0 (generic/unified, accessible from host+device)
// The offload.global_var should be erased from the parent module.

//===----------------------------------------------------------------------===//
// Scalar shared variable: i32
//===----------------------------------------------------------------------===//

// CHECK-LABEL: gpu.module @offload_device_module {
// CHECK:         llvm.mlir.global internal @scalar_shared() {addr_space = 3 : i32} : i32
// CHECK-NOT:   offload.global_var @scalar_shared

module attributes {gpu.container_module} {
  offload.global_var @scalar_shared : i32
      mem_space = #offload.mem_space<shared>

  offload.func @kernel(%arg0 : index) exec_space = #offload.exec_space<global> {
    offload.return
  }

  func.func @launch(%gx: index, %bsz: index) {
    %one = arith.constant 1 : index
    offload.kernel_launch @kernel
        grid  = (%gx, %one, %one)
        block = (%bsz, %one, %one)
        args  = (%gx : index)
    return
  }
}

// -----

//===----------------------------------------------------------------------===//
// Array shared variable: [256 x f32]
//===----------------------------------------------------------------------===//

// CHECK-LABEL: gpu.module @offload_device_module {
// CHECK:         llvm.mlir.global internal @smem() {addr_space = 3 : i32} : !llvm.array<256 x f32>
// CHECK-NOT:   offload.global_var @smem

module attributes {gpu.container_module} {
  offload.global_var @smem : !llvm.array<256 x f32>
      mem_space = #offload.mem_space<shared>

  offload.func @kernel(%arg0 : index) exec_space = #offload.exec_space<global> {
    offload.return
  }

  func.func @launch(%gx: index, %bsz: index) {
    %one = arith.constant 1 : index
    offload.kernel_launch @kernel
        grid  = (%gx, %one, %one)
        block = (%bsz, %one, %one)
        args  = (%gx : index)
    return
  }
}

// -----

//===----------------------------------------------------------------------===//
// Device variable: i32 → addr_space = 1
//===----------------------------------------------------------------------===//

// CHECK-LABEL: gpu.module @offload_device_module {
// CHECK:         llvm.mlir.global internal @dev_var() {addr_space = 1 : i32} : i32
// CHECK-NOT:   offload.global_var @dev_var

module attributes {gpu.container_module} {
  offload.global_var @dev_var : i32
      mem_space = #offload.mem_space<device>

  offload.func @kernel(%arg0 : index) exec_space = #offload.exec_space<global> {
    offload.return
  }

  func.func @launch(%gx: index, %bsz: index) {
    %one = arith.constant 1 : index
    offload.kernel_launch @kernel
        grid  = (%gx, %one, %one)
        block = (%bsz, %one, %one)
        args  = (%gx : index)
    return
  }
}

// -----

//===----------------------------------------------------------------------===//
// Constant variable: [4 x f32] → addr_space = 4, isConstant = true
//===----------------------------------------------------------------------===//

// CHECK-LABEL: gpu.module @offload_device_module {
// CHECK:         llvm.mlir.global internal constant @lut() {addr_space = 4 : i32} : !llvm.array<4 x f32>
// CHECK-NOT:   offload.global_var @lut

module attributes {gpu.container_module} {
  offload.global_var @lut : !llvm.array<4 x f32>
      mem_space = #offload.mem_space<constant>

  offload.func @kernel(%arg0 : index) exec_space = #offload.exec_space<global> {
    offload.return
  }

  func.func @launch(%gx: index, %bsz: index) {
    %one = arith.constant 1 : index
    offload.kernel_launch @kernel
        grid  = (%gx, %one, %one)
        block = (%bsz, %one, %one)
        args  = (%gx : index)
    return
  }
}

// -----

//===----------------------------------------------------------------------===//
// Managed variable: i32 → addr_space = 0 (generic/unified)
//===----------------------------------------------------------------------===//

// CHECK-LABEL: gpu.module @offload_device_module {
// CHECK:         llvm.mlir.global internal @managed_var() {addr_space = 0 : i32} : i32
// CHECK-NOT:   offload.global_var @managed_var

module attributes {gpu.container_module} {
  offload.global_var @managed_var : i32
      mem_space = #offload.mem_space<managed>

  offload.func @kernel(%arg0 : index) exec_space = #offload.exec_space<global> {
    offload.return
  }

  func.func @launch(%gx: index, %bsz: index) {
    %one = arith.constant 1 : index
    offload.kernel_launch @kernel
        grid  = (%gx, %one, %one)
        block = (%bsz, %one, %one)
        args  = (%gx : index)
    return
  }
}

// -----

//===----------------------------------------------------------------------===//
// Two-module split: non-replicable (__device__) global goes to primary only
//===----------------------------------------------------------------------===//

// A __device__ global is non-replicable — it always ends up in the primary
// module regardless of which kernels reference it.
//
// CHECK: gpu.module @offload_device_module
// CHECK:   llvm.mlir.global internal @shared_counter() {addr_space = 1 : i32} : i32
// CHECK-NOT: offload.global_var @shared_counter

module attributes {gpu.container_module} {
  offload.global_var @shared_counter : i32
      mem_space = #offload.mem_space<device>

  offload.func @launchedWithGlobal(%n: i32) exec_space = #offload.exec_space<global> {
    offload.return
  }

  offload.func @unusedWithGlobal(%n: i32) exec_space = #offload.exec_space<global> {
    offload.return
  }

  func.func @doLaunch(%n: i32, %gx: index, %bsz: index) {
    %one = arith.constant 1 : index
    offload.kernel_launch @launchedWithGlobal
        grid  = (%gx, %one, %one)
        block = (%bsz, %one, %one)
        args  = (%n : i32)
    return
  }
}

// -----

//===----------------------------------------------------------------------===//
// Two-module split: __shared__ global referenced by no function falls back to
// primary module
//===----------------------------------------------------------------------===//

// When no gpu.func in either module references a shared global, LowerSharedGlobals
// falls back to placing it in the primary module to avoid silently dropping it.
//
// CHECK: gpu.module @offload_device_module
// CHECK:   llvm.mlir.global internal @smem_unused() {addr_space = 3 : i32} : !llvm.array<64 x f32>
// CHECK-NOT: offload.global_var @smem_unused

module attributes {gpu.container_module} {
  offload.global_var @smem_unused : !llvm.array<64 x f32>
      mem_space = #offload.mem_space<shared>

  offload.func @kernelNoSmem(%n: i32) exec_space = #offload.exec_space<global> {
    offload.return
  }

  func.func @launchNoSmem(%n: i32, %gx: index, %bsz: index) {
    %one = arith.constant 1 : index
    offload.kernel_launch @kernelNoSmem
        grid  = (%gx, %one, %one)
        block = (%bsz, %one, %one)
        args  = (%n : i32)
    return
  }
}
