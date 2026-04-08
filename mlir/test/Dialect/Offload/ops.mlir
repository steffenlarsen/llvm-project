// RUN: mlir-opt --split-input-file %s | FileCheck %s
// Verify the printed output can be parsed (round-trip).
// RUN: mlir-opt --split-input-file %s | mlir-opt --split-input-file | FileCheck %s

// Detailed op-level tests: checks precise printing order, optional fields,
// uses of gpu.* intrinsics inside device functions, and multi-block bodies.

//===----------------------------------------------------------------------===//
// offload.func — custom assembly: name, signature, exec_space, optional attrs
//===----------------------------------------------------------------------===//

// A void kernel with both optional attrs present.
//
// CHECK-LABEL: offload.func @kernel_full_attrs
// CHECK-SAME:  (%{{.*}}: memref<f32>, %{{.*}}: i32)
// CHECK-SAME:  exec_space = #offload.exec_space<global>
// CHECK-SAME:  launch_bounds = #offload.launch_bounds<256, 4>
// CHECK-SAME:  reqd_work_group_size = #offload.reqd_work_group_size<256, 1, 1>
// CHECK-SAME:  {
offload.func @kernel_full_attrs(%a: memref<f32>, %n: i32)
    exec_space = #offload.exec_space<global>
    launch_bounds = #offload.launch_bounds<256, 4>
    reqd_work_group_size = #offload.reqd_work_group_size<256, 1, 1> {
  offload.return
}

// -----

// Device function — no kernel attributes expected.
//
// CHECK-LABEL: offload.func @dev_helper
// CHECK-SAME:  exec_space = #offload.exec_space<device>
// CHECK-NOT:   launch_bounds
// CHECK-NOT:   reqd_work_group_size
offload.func @dev_helper(%x: f32) -> f32
    exec_space = #offload.exec_space<device> {
  offload.return %x : f32
}

// -----

// host_device function with arithmetic body.
//
// CHECK-LABEL: offload.func @hd_clamp
// CHECK-SAME:  exec_space = #offload.exec_space<host_device>
offload.func @hd_clamp(%x: f32, %lo: f32, %hi: f32) -> f32
    exec_space = #offload.exec_space<host_device> {
  %a = arith.maximumf %x, %lo : f32
  %b = arith.minimumf %a, %hi : f32
  offload.return %b : f32
}

// -----

// Kernel body using gpu.* index intrinsics (the key "reuse, not duplicate" property).
//
// CHECK-LABEL: offload.func @index_intrinsics
// CHECK:         gpu.thread_id x
// CHECK:         gpu.block_id x
// CHECK:         gpu.block_dim x
// CHECK:         gpu.grid_dim x
offload.func @index_intrinsics() exec_space = #offload.exec_space<global> {
  %tid   = gpu.thread_id x
  %bid   = gpu.block_id  x
  %bdim  = gpu.block_dim x
  %gdim  = gpu.grid_dim  x
  offload.return
}

// -----

// Multi-block function body (control flow inside kernel).
//
// CHECK-LABEL: offload.func @multiblock_kernel
// CHECK:         arith.cmpi
// CHECK:         cf.cond_br
offload.func @multiblock_kernel(%n: i32) exec_space = #offload.exec_space<global> {
  %zero = arith.constant 0 : i32
  %cmp  = arith.cmpi sgt, %n, %zero : i32
  cf.cond_br %cmp, ^early, ^skip
^early:
  offload.return
^skip:
  offload.return
}

// -----

//===----------------------------------------------------------------------===//
// offload.kernel_launch — optional stream operand
//===----------------------------------------------------------------------===//

offload.func @stream_kernel(%x: f32) exec_space = #offload.exec_space<global> {
  offload.return
}

// Without stream.
// CHECK-LABEL: func.func @launch_no_stream
// CHECK:         offload.kernel_launch @stream_kernel
// CHECK-NOT:     stream =
func.func @launch_no_stream(%x: f32, %gx: index, %bsz: index) {
  %one = arith.constant 1 : index
  offload.kernel_launch @stream_kernel
      grid  = (%gx, %one, %one)
      block = (%bsz, %one, %one)
      args  = (%x : f32)
  return
}

// -----

offload.func @stream_kernel2(%x: f32) exec_space = #offload.exec_space<global> {
  offload.return
}

// With stream.
// CHECK-LABEL: func.func @launch_with_stream
// CHECK:         offload.kernel_launch @stream_kernel2
// CHECK-SAME:    stream = %{{.*}} : !offload.stream
func.func @launch_with_stream(%x: f32, %s: !offload.stream,
                               %gx: index, %bsz: index) {
  %one = arith.constant 1 : index
  offload.kernel_launch @stream_kernel2
      grid   = (%gx, %one, %one)
      block  = (%bsz, %one, %one)
      stream = %s : !offload.stream
      args   = (%x : f32)
  return
}

// -----

//===----------------------------------------------------------------------===//
// offload.global_var — all fields
//===----------------------------------------------------------------------===//

// Constant array without extern_init.
// CHECK: offload.global_var @lut_arr : memref<256xf32>
// CHECK-SAME: mem_space = <constant>
// CHECK-NOT:  extern_init
offload.global_var @lut_arr : memref<256xf32>
    mem_space = <constant>

// -----

// Device scalar with extern_init.
// CHECK: offload.global_var @device_counter : i64
// CHECK-SAME: mem_space = <device>
// CHECK-SAME: extern_init
offload.global_var @device_counter : i64
    mem_space = <device>
    extern_init

// -----

// Shared-memory array (no extern_init — shared mem is always device-local).
// CHECK: offload.global_var @shared_buf : memref<512xi32>
// CHECK-SAME: mem_space = <shared>
// CHECK-NOT:  extern_init
offload.global_var @shared_buf : memref<512xi32>
    mem_space = <shared>

// -----

//===----------------------------------------------------------------------===//
// offload.shared_mem_alloc — different element types returned
//===----------------------------------------------------------------------===//

// CHECK-LABEL: offload.func @shmem_i8
offload.func @shmem_i8(%n: index) exec_space = #offload.exec_space<global> {
  // CHECK: offload.shared_mem_alloc %{{.*}} -> memref<i8>
  %p = offload.shared_mem_alloc %n -> memref<i8>
  offload.return
}

// -----

// CHECK-LABEL: offload.func @shmem_f32
offload.func @shmem_f32(%n: index) exec_space = #offload.exec_space<global> {
  // CHECK: offload.shared_mem_alloc %{{.*}} -> memref<f32>
  %p = offload.shared_mem_alloc %n -> memref<f32>
  offload.return
}

// -----

//===----------------------------------------------------------------------===//
// Host runtime ops — precise field ordering in printed form
//===----------------------------------------------------------------------===//

// CHECK-LABEL: func.func @all_runtime_ops
func.func @all_runtime_ops() {
  // CHECK-NEXT: %[[S:.*]] = offload.stream_create : !offload.stream
  %s = offload.stream_create : !offload.stream
  // CHECK-NEXT: offload.device_sync
  offload.device_sync
  // CHECK-NEXT: offload.stream_sync %[[S]] : !offload.stream
  offload.stream_sync %s : !offload.stream
  // CHECK-NEXT: offload.stream_destroy %[[S]] : !offload.stream
  offload.stream_destroy %s : !offload.stream
  // CHECK-NEXT: return
  return
}

// -----

//===----------------------------------------------------------------------===//
// offload.memcpy_to_symbol — two back-to-back copies to the same symbol
//===----------------------------------------------------------------------===//

offload.global_var @weights : memref<1024xf32>
    mem_space = <constant>

// CHECK-LABEL: func.func @double_copy
func.func @double_copy(%src1: memref<1024xf32>, %src2: memref<1024xf32>,
                        %n1: index, %n2: index) {
  // CHECK: offload.memcpy_to_symbol @weights src = %{{.*}} : memref<1024xf32> count = %{{.*}}
  offload.memcpy_to_symbol @weights src = %src1 : memref<1024xf32> count = %n1
  // CHECK: offload.memcpy_to_symbol @weights src = %{{.*}} : memref<1024xf32> count = %{{.*}}
  offload.memcpy_to_symbol @weights src = %src2 : memref<1024xf32> count = %n2
  return
}

// -----

//===----------------------------------------------------------------------===//
// Interleaved host and device functions (real single-source IR shape)
//===----------------------------------------------------------------------===//

// The two functions coexist flat in the same module — no nesting required.
//
// CHECK: offload.func @relu_device
// CHECK: offload.func @relu_kernel
// CHECK: func.func @relu_host_wrapper

offload.func @relu_device(%x: f32) -> f32
    exec_space = #offload.exec_space<device> {
  %zero = arith.constant 0.0 : f32
  %r = arith.maximumf %x, %zero : f32
  offload.return %r : f32
}

offload.func @relu_kernel(%buf: memref<f32>, %n: i32)
    exec_space = #offload.exec_space<global>
    launch_bounds = #offload.launch_bounds<128> {
  %tid = gpu.thread_id x
  offload.return
}

func.func @relu_host_wrapper(%buf: memref<f32>, %n: i32) {
  %c128 = arith.constant 128 : index
  %c1   = arith.constant 1   : index
  %ni   = arith.index_cast %n : i32 to index
  %gx   = arith.ceildivsi %ni, %c128 : index
  offload.kernel_launch @relu_kernel
      grid  = (%gx, %c1, %c1)
      block = (%c128, %c1, %c1)
      args  = (%buf, %n : memref<f32>, i32)
  return
}
