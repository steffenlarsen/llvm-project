// RUN: mlir-opt --split-input-file %s | FileCheck %s
// Verify the printed output can be parsed (round-trip).
// RUN: mlir-opt --split-input-file %s | mlir-opt --split-input-file | FileCheck %s
// Verify the generic form can be parsed.
// RUN: mlir-opt --split-input-file -mlir-print-op-generic %s | mlir-opt --split-input-file | FileCheck %s

// Round-trip and attribute coverage tests for the offload dialect.
// No errors expected — all constructs here should parse and print cleanly.

//===----------------------------------------------------------------------===//
// ExecSpaceAttr  (#offload.exec_space<...>)
//===----------------------------------------------------------------------===//

// CHECK-LABEL: func.func @exec_space_host
func.func @exec_space_host() {
  // CHECK: #offload.exec_space<host>
  %0 = "test.op"() { es = #offload.exec_space<host> } : () -> i1
  return
}

// -----

// CHECK-LABEL: func.func @exec_space_device
func.func @exec_space_device() {
  // CHECK: #offload.exec_space<device>
  %0 = "test.op"() { es = #offload.exec_space<device> } : () -> i1
  return
}

// -----

// CHECK-LABEL: func.func @exec_space_global
func.func @exec_space_global() {
  // CHECK: #offload.exec_space<global>
  %0 = "test.op"() { es = #offload.exec_space<global> } : () -> i1
  return
}

// -----

// CHECK-LABEL: func.func @exec_space_host_device
func.func @exec_space_host_device() {
  // CHECK: #offload.exec_space<host_device>
  %0 = "test.op"() { es = #offload.exec_space<host_device> } : () -> i1
  return
}

// -----

//===----------------------------------------------------------------------===//
// MemSpaceAttr  (#offload.mem_space<...>)
//===----------------------------------------------------------------------===//

// CHECK-LABEL: func.func @mem_spaces
func.func @mem_spaces() {
  %0 = "test.op"() {
    // CHECK-DAG: #offload.mem_space<generic>
    a = #offload.mem_space<generic>,
    // CHECK-DAG: #offload.mem_space<shared>
    b = #offload.mem_space<shared>,
    // CHECK-DAG: #offload.mem_space<constant>
    c = #offload.mem_space<constant>,
    // CHECK-DAG: #offload.mem_space<device>
    d = #offload.mem_space<device>,
    // CHECK-DAG: #offload.mem_space<managed>
    e = #offload.mem_space<managed>,
    // CHECK-DAG: #offload.mem_space<local>
    f = #offload.mem_space<local>
  } : () -> i1
  return
}

// -----

//===----------------------------------------------------------------------===//
// LaunchBoundsAttr  (#offload.launch_bounds<...>)
//===----------------------------------------------------------------------===//

// CHECK-LABEL: func.func @launch_bounds_no_minblocks
func.func @launch_bounds_no_minblocks() {
  // CHECK: #offload.launch_bounds<256>
  %0 = "test.op"() { lb = #offload.launch_bounds<256> } : () -> i1
  return
}

// -----

// CHECK-LABEL: func.func @launch_bounds_with_minblocks
func.func @launch_bounds_with_minblocks() {
  // CHECK: #offload.launch_bounds<1024, 4>
  %0 = "test.op"() { lb = #offload.launch_bounds<1024, 4> } : () -> i1
  return
}

// -----

//===----------------------------------------------------------------------===//
// ReqdWorkgroupSizeAttr  (#offload.reqd_work_group_size<x,y,z>)
//===----------------------------------------------------------------------===//

// CHECK-LABEL: func.func @reqd_work_group_size_1d
func.func @reqd_work_group_size_1d() {
  // CHECK: #offload.reqd_work_group_size<64, 1, 1>
  %0 = "test.op"() { rw = #offload.reqd_work_group_size<64, 1, 1> } : () -> i1
  return
}

// -----

// CHECK-LABEL: func.func @reqd_work_group_size_3d
func.func @reqd_work_group_size_3d() {
  // CHECK: #offload.reqd_work_group_size<16, 8, 4>
  %0 = "test.op"() { rw = #offload.reqd_work_group_size<16, 8, 4> } : () -> i1
  return
}

// -----

//===----------------------------------------------------------------------===//
// TargetAttr  (#offload.target<...>)
//===----------------------------------------------------------------------===//

// CHECK: module attributes
// CHECK-SAME: offload.target = #offload.target<runtime = "hip", architectures = ["gfx90a", "gfx942"]>
module attributes {
  offload.target = #offload.target<runtime = "hip", architectures = ["gfx90a", "gfx942"]>
} {
  func.func @dummy() { return }
}

// -----

// CHECK: module attributes
// CHECK-SAME: offload.target = #offload.target<runtime = "cuda", architectures = ["sm_80"]>
module attributes {
  offload.target = #offload.target<runtime = "cuda", architectures = ["sm_80"]>
} {
  func.func @dummy() { return }
}

// -----

//===----------------------------------------------------------------------===//
// StreamType and EventType  (!offload.stream, !offload.event)
//===----------------------------------------------------------------------===//

// CHECK-LABEL: func.func @stream_type
// CHECK-SAME: !offload.stream
func.func @stream_type(%s: !offload.stream) -> !offload.stream {
  return %s : !offload.stream
}

// -----

// CHECK-LABEL: func.func @event_type
// CHECK-SAME: !offload.event
func.func @event_type(%e: !offload.event) -> !offload.event {
  return %e : !offload.event
}

// -----

//===----------------------------------------------------------------------===//
// offload.func — all exec_space variants
//===----------------------------------------------------------------------===//

// CHECK-LABEL: offload.func @host_void
// CHECK-SAME: exec_space = #offload.exec_space<host>
offload.func @host_void() exec_space = #offload.exec_space<host> {
  offload.return
}

// -----

// CHECK-LABEL: offload.func @host_with_args
// CHECK-SAME: (%{{.*}}: f32, %{{.*}}: i32) -> f32
// CHECK-SAME: exec_space = #offload.exec_space<host>
offload.func @host_with_args(%x: f32, %n: i32) -> f32
    exec_space = #offload.exec_space<host> {
  offload.return %x : f32
}

// -----

// CHECK-LABEL: offload.func @device_helper
// CHECK-SAME: exec_space = #offload.exec_space<device>
offload.func @device_helper(%x: f32) -> f32
    exec_space = #offload.exec_space<device> {
  offload.return %x : f32
}

// -----

// CHECK-LABEL: offload.func @simple_kernel
// CHECK-SAME: exec_space = #offload.exec_space<global>
// CHECK-NOT:  launch_bounds
offload.func @simple_kernel(%a: memref<f32>, %n: i32)
    exec_space = #offload.exec_space<global> {
  offload.return
}

// -----

// CHECK-LABEL: offload.func @kernel_with_launch_bounds
// CHECK-SAME: exec_space = #offload.exec_space<global>
// CHECK-SAME: launch_bounds = #offload.launch_bounds<256>
offload.func @kernel_with_launch_bounds(%a: memref<f32>, %n: i32)
    exec_space = #offload.exec_space<global>
    launch_bounds = #offload.launch_bounds<256> {
  offload.return
}

// -----

// CHECK-LABEL: offload.func @kernel_with_minblocks
// CHECK-SAME: launch_bounds = #offload.launch_bounds<512, 2>
offload.func @kernel_with_minblocks(%a: memref<f32>)
    exec_space = #offload.exec_space<global>
    launch_bounds = #offload.launch_bounds<512, 2> {
  offload.return
}

// -----

// CHECK-LABEL: offload.func @kernel_with_reqd_wg_size
// CHECK-SAME: reqd_work_group_size = #offload.reqd_work_group_size<64, 1, 1>
offload.func @kernel_with_reqd_wg_size(%a: memref<f32>)
    exec_space = #offload.exec_space<global>
    reqd_work_group_size = #offload.reqd_work_group_size<64, 1, 1> {
  offload.return
}

// -----

// CHECK-LABEL: offload.func @host_device_math
// CHECK-SAME: exec_space = #offload.exec_space<host_device>
offload.func @host_device_math(%x: f32, %y: f32) -> f32
    exec_space = #offload.exec_space<host_device> {
  %z = arith.addf %x, %y : f32
  offload.return %z : f32
}

// -----

//===----------------------------------------------------------------------===//
// offload.return — various arities
//===----------------------------------------------------------------------===//

// CHECK-LABEL: offload.func @return_void
offload.func @return_void() exec_space = #offload.exec_space<host> {
  // CHECK: offload.return
  offload.return
}

// -----

// CHECK-LABEL: offload.func @return_one_val
offload.func @return_one_val(%x: i32) -> i32
    exec_space = #offload.exec_space<host> {
  // CHECK: offload.return %{{.*}} : i32
  offload.return %x : i32
}

// -----

// CHECK-LABEL: offload.func @return_multi_val
offload.func @return_multi_val(%x: f32, %n: i32) -> (f32, i32)
    exec_space = #offload.exec_space<host> {
  // CHECK: offload.return %{{.*}}, %{{.*}} : f32, i32
  offload.return %x, %n : f32, i32
}

// -----

//===----------------------------------------------------------------------===//
// offload.kernel_launch — various arities and types
//===----------------------------------------------------------------------===//

offload.func @kernel_no_args() exec_space = #offload.exec_space<global> {
  offload.return
}

// CHECK-LABEL: func.func @launch_no_args
func.func @launch_no_args(%gx: index, %bsz: index) {
  %one = arith.constant 1 : index
  // CHECK: offload.kernel_launch @kernel_no_args
  // CHECK-SAME: grid = (%{{[^ ,)]+}}, %{{[^ ,)]+}}, %{{[^ ,)]+}})
  // CHECK-SAME: block = (%{{[^ ,)]+}}, %{{[^ ,)]+}}, %{{[^ ,)]+}})
  offload.kernel_launch @kernel_no_args
      grid  = (%gx, %one, %one)
      block = (%bsz, %one, %one)
  return
}

// -----

offload.func @kernel_mixed_args(%a: memref<f32>, %n: i32, %s: f32)
    exec_space = #offload.exec_space<global> {
  offload.return
}

// CHECK-LABEL: func.func @launch_mixed_args
func.func @launch_mixed_args(%da: memref<f32>, %n: i32, %s: f32,
                              %gx: index, %bsz: index) {
  %one = arith.constant 1 : index
  // CHECK: offload.kernel_launch @kernel_mixed_args
  // CHECK-SAME: args = (%{{[^ ,)]+}}, %{{[^ ,)]+}}, %{{[^ ,)]+}} : memref<f32>, i32, f32)
  offload.kernel_launch @kernel_mixed_args
      grid  = (%gx, %one, %one)
      block = (%bsz, %one, %one)
      args  = (%da, %n, %s : memref<f32>, i32, f32)
  return
}

// -----

//===----------------------------------------------------------------------===//
// offload.global_var — all memory spaces
//===----------------------------------------------------------------------===//

// CHECK: offload.global_var @gv_generic : f32 mem_space = <generic>
offload.global_var @gv_generic : f32 mem_space = #offload.mem_space<generic>

// -----

// CHECK: offload.global_var @gv_shared : memref<256xf32> mem_space = <shared>
offload.global_var @gv_shared : memref<256xf32> mem_space = #offload.mem_space<shared>

// -----

// CHECK: offload.global_var @gv_constant : f32 mem_space = <constant>
offload.global_var @gv_constant : f32 mem_space = #offload.mem_space<constant>

// -----

// CHECK: offload.global_var @gv_device : i32 mem_space = <device>
offload.global_var @gv_device : i32 mem_space = #offload.mem_space<device>

// -----

// CHECK: offload.global_var @gv_managed : f64 mem_space = <managed>
offload.global_var @gv_managed : f64 mem_space = #offload.mem_space<managed>

// -----

// CHECK: offload.global_var @gv_extern_init : f32 mem_space = <device> extern_init
offload.global_var @gv_extern_init : f32
    mem_space = #offload.mem_space<device>
    extern_init

// -----

//===----------------------------------------------------------------------===//
// offload.shared_mem_alloc — inside device and global functions
//===----------------------------------------------------------------------===//

// CHECK-LABEL: offload.func @kernel_dynamic_shmem
offload.func @kernel_dynamic_shmem(%nbytes: index)
    exec_space = #offload.exec_space<global> {
  // CHECK: offload.shared_mem_alloc %{{.*}} -> memref<i8>
  %ptr = offload.shared_mem_alloc %nbytes -> memref<i8>
  offload.return
}

// -----

// CHECK-LABEL: offload.func @device_dynamic_shmem
offload.func @device_dynamic_shmem(%nbytes: index)
    exec_space = #offload.exec_space<device> {
  // CHECK: offload.shared_mem_alloc %{{.*}} -> memref<i8>
  %ptr = offload.shared_mem_alloc %nbytes -> memref<i8>
  offload.return
}

// -----

//===----------------------------------------------------------------------===//
// Host-side runtime operations
//===----------------------------------------------------------------------===//

// CHECK-LABEL: func.func @stream_create_and_destroy
func.func @stream_create_and_destroy() {
  // CHECK: %[[S:.*]] = offload.stream_create : !offload.stream
  %s = offload.stream_create : !offload.stream
  // CHECK: offload.stream_destroy %[[S]] : !offload.stream
  offload.stream_destroy %s : !offload.stream
  return
}

// -----

// CHECK-LABEL: func.func @stream_sync
func.func @stream_sync(%s: !offload.stream) {
  // CHECK: offload.stream_sync %{{.*}} : !offload.stream
  offload.stream_sync %s : !offload.stream
  return
}

// -----

// CHECK-LABEL: func.func @device_sync
func.func @device_sync() {
  // CHECK: offload.device_sync
  offload.device_sync
  return
}

// -----

//===----------------------------------------------------------------------===//
// offload.memcpy_to_symbol
//===----------------------------------------------------------------------===//

offload.global_var @lookup_table : memref<256xf32>
    mem_space = #offload.mem_space<constant>

// CHECK-LABEL: func.func @init_lookup_table
func.func @init_lookup_table(%src: memref<256xf32>, %n: index) {
  // CHECK: offload.memcpy_to_symbol @lookup_table
  // CHECK-SAME: src = %{{.*}} : memref<256xf32>
  // CHECK-SAME: count = %{{.*}}
  offload.memcpy_to_symbol @lookup_table
      src = %src : memref<256xf32>
      count = %n
  return
}

// -----

//===----------------------------------------------------------------------===//
// Realistic single-source HIP vector-add module
//===----------------------------------------------------------------------===//

// CHECK: module attributes
// CHECK-SAME: offload.target = #offload.target<runtime = "hip"
module attributes {
  offload.target = #offload.target<runtime = "hip", architectures = ["gfx90a"]>
} {

  // CHECK-LABEL: offload.func @vecAdd
  // CHECK-SAME:  exec_space = #offload.exec_space<global>
  // CHECK-SAME:  launch_bounds = #offload.launch_bounds<256>
  offload.func @vecAdd(%a: memref<f32>, %b: memref<f32>,
                       %c: memref<f32>, %n: i32)
      exec_space = #offload.exec_space<global>
      launch_bounds = #offload.launch_bounds<256> {
    %tid  = gpu.thread_id x
    %bid  = gpu.block_id  x
    %bdim = gpu.block_dim x
    offload.return
  }

  // CHECK-LABEL: func.func @launchVecAdd
  func.func @launchVecAdd(%da: memref<f32>, %db: memref<f32>,
                          %dc: memref<f32>, %n: i32) {
    %c256 = arith.constant 256 : index
    %c1   = arith.constant 1   : index
    %ni   = arith.index_cast %n : i32 to index
    %gx   = arith.ceildivsi %ni, %c256 : index
    // CHECK: offload.kernel_launch @vecAdd
    offload.kernel_launch @vecAdd
        grid  = (%gx,  %c1, %c1)
        block = (%c256, %c1, %c1)
        args  = (%da, %db, %dc, %n : memref<f32>, memref<f32>, memref<f32>, i32)
    return
  }
}
