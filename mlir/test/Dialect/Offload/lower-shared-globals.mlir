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

// CHECK-LABEL: gpu.module @offload_device_module
// CHECK:         llvm.mlir.global internal @scalar_shared() {addr_space = 3 : i32} : i32
// CHECK-NOT:   offload.global_var @scalar_shared

module attributes {gpu.container_module} {
  offload.global_var @scalar_shared : i32
      mem_space = #offload.mem_space<shared>

  offload.func @kernel(%arg0 : index) exec_space = #offload.exec_space<global> {
    offload.return
  }
}

// -----

//===----------------------------------------------------------------------===//
// Array shared variable: [256 x f32]
//===----------------------------------------------------------------------===//

// CHECK-LABEL: gpu.module @offload_device_module
// CHECK:         llvm.mlir.global internal @smem() {addr_space = 3 : i32} : !llvm.array<256 x f32>
// CHECK-NOT:   offload.global_var @smem

module attributes {gpu.container_module} {
  offload.global_var @smem : !llvm.array<256 x f32>
      mem_space = #offload.mem_space<shared>

  offload.func @kernel(%arg0 : index) exec_space = #offload.exec_space<global> {
    offload.return
  }
}

// -----

//===----------------------------------------------------------------------===//
// Device variable: i32 → addr_space = 1
//===----------------------------------------------------------------------===//

// CHECK-LABEL: gpu.module @offload_device_module
// CHECK:         llvm.mlir.global internal @dev_var() {addr_space = 1 : i32} : i32
// CHECK-NOT:   offload.global_var @dev_var

module attributes {gpu.container_module} {
  offload.global_var @dev_var : i32
      mem_space = #offload.mem_space<device>

  offload.func @kernel(%arg0 : index) exec_space = #offload.exec_space<global> {
    offload.return
  }
}

// -----

//===----------------------------------------------------------------------===//
// Constant variable: [4 x f32] → addr_space = 4, isConstant = true
//===----------------------------------------------------------------------===//

// CHECK-LABEL: gpu.module @offload_device_module
// CHECK:         llvm.mlir.global internal constant @lut() {addr_space = 4 : i32} : !llvm.array<4 x f32>
// CHECK-NOT:   offload.global_var @lut

module attributes {gpu.container_module} {
  offload.global_var @lut : !llvm.array<4 x f32>
      mem_space = #offload.mem_space<constant>

  offload.func @kernel(%arg0 : index) exec_space = #offload.exec_space<global> {
    offload.return
  }
}

// -----

//===----------------------------------------------------------------------===//
// Managed variable: i32 → addr_space = 0 (generic/unified)
//===----------------------------------------------------------------------===//

// CHECK-LABEL: gpu.module @offload_device_module
// CHECK:         llvm.mlir.global internal @managed_var() {addr_space = 0 : i32} : i32
// CHECK-NOT:   offload.global_var @managed_var

module attributes {gpu.container_module} {
  offload.global_var @managed_var : i32
      mem_space = #offload.mem_space<managed>

  offload.func @kernel(%arg0 : index) exec_space = #offload.exec_space<global> {
    offload.return
  }
}
