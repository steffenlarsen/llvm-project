// RUN: mlir-opt --split-input-file \
// RUN:   --offload-split-single-source \
// RUN:   '--offload-lower-shared-globals=gpu-module-name=offload_device_module' \
// RUN:   %s | FileCheck %s

// Tests for OffloadLowerSharedGlobalsPass.
//
// offload.global_var with mem_space = shared should be lowered to
// llvm.mlir.global internal with addr_space = 3 inside the gpu.module.
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
// Non-shared variables should NOT appear in gpu.module as llvm.mlir.global.
//===----------------------------------------------------------------------===//

// CHECK-LABEL: gpu.module @offload_device_module
// CHECK-NOT:   llvm.mlir.global

module attributes {gpu.container_module} {
  offload.global_var @dev_var : i32
      mem_space = #offload.mem_space<device>

  offload.func @kernel(%arg0 : index) exec_space = #offload.exec_space<global> {
    offload.return
  }
}
