// RUN: mlir-opt -split-input-file -verify-diagnostics %s

// Comprehensive verifier error tests for the offload dialect.
// Each section is separated by a split marker and tests one verifier rule.

//===----------------------------------------------------------------------===//
// offload.func verifier
//===----------------------------------------------------------------------===//

// A kernel (exec_space=global) must have void return type.
// expected-error@+1 {{'offload.func' op kernel function (exec_space=global) must return void}}
offload.func @kernel_with_result(%x: f32) -> f32
    exec_space = #offload.exec_space<global> {
  offload.return %x : f32
}

// -----

// gpu.* ops must not appear inside a host-only offload.func.
offload.func @gpu_op_in_host_func()
    exec_space = #offload.exec_space<host> {
  // expected-error@+1 {{gpu dialect operation is not allowed inside a host-only offload.func (exec_space=host)}}
  %tid = gpu.thread_id x
  offload.return
}

// -----

//===----------------------------------------------------------------------===//
// offload.return verifier
//===----------------------------------------------------------------------===//

// Return arity must match function result types.
offload.func @return_arity_mismatch(%x: f32)
    exec_space = #offload.exec_space<host> {
  // expected-error@+1 {{'offload.return' op has 1 operands, but enclosing function returns 0}}
  offload.return %x : f32
}

// -----

// Return type must match function result types.
offload.func @return_type_mismatch(%x: f32) -> i32
    exec_space = #offload.exec_space<host> {
  // expected-error@+1 {{'offload.return' op type of return operand 0 ('f32') doesn't match function result type ('i32')}}
  offload.return %x : f32
}

// -----

//===----------------------------------------------------------------------===//
// offload.kernel_launch verifier
//===----------------------------------------------------------------------===//

// Callee must exist as an offload.func.
func.func @launch_unknown_callee(%gx: index, %bsz: index) {
  %one = arith.constant 1 : index
  // expected-error@+1 {{'offload.kernel_launch' op callee 'nonexistent' does not name an offload.func in the same module}}
  offload.kernel_launch @nonexistent
      grid  = (%gx, %one, %one)
      block = (%bsz, %one, %one)
  return
}

// -----

// Callee must have exec_space=global (not host).
offload.func @host_fn(%x: f32) exec_space = #offload.exec_space<host> {
  offload.return
}

func.func @launch_non_kernel(%gx: index, %bsz: index, %x: f32) {
  %one = arith.constant 1 : index
  // expected-error@+1 {{'offload.kernel_launch' op callee 'host_fn' must have exec_space = global}}
  offload.kernel_launch @host_fn
      grid  = (%gx, %one, %one)
      block = (%bsz, %one, %one)
      args  = (%x : f32)
  return
}

// -----

// Callee must have exec_space=global (not device).
offload.func @device_fn(%x: f32) exec_space = #offload.exec_space<device> {
  offload.return
}

func.func @launch_device_fn(%gx: index, %bsz: index, %x: f32) {
  %one = arith.constant 1 : index
  // expected-error@+1 {{'offload.kernel_launch' op callee 'device_fn' must have exec_space = global}}
  offload.kernel_launch @device_fn
      grid  = (%gx, %one, %one)
      block = (%bsz, %one, %one)
      args  = (%x : f32)
  return
}

// -----

// Argument count must match the callee's parameter count.
offload.func @kernel_one_arg(%x: f32) exec_space = #offload.exec_space<global> {
  offload.return
}

func.func @launch_wrong_arg_count(%gx: index, %bsz: index, %x: f32, %y: f32) {
  %one = arith.constant 1 : index
  // expected-error@+1 {{'offload.kernel_launch' op has 2 arguments, but callee 'kernel_one_arg' expects 1}}
  offload.kernel_launch @kernel_one_arg
      grid  = (%gx, %one, %one)
      block = (%bsz, %one, %one)
      args  = (%x, %y : f32, f32)
  return
}

// -----

// Argument types must match exactly.
offload.func @kernel_i32(%n: i32) exec_space = #offload.exec_space<global> {
  offload.return
}

func.func @launch_wrong_arg_type(%gx: index, %bsz: index, %x: f32) {
  %one = arith.constant 1 : index
  // expected-error@+1 {{'offload.kernel_launch' op type of argument 0 ('f32') doesn't match callee parameter type ('i32')}}
  offload.kernel_launch @kernel_i32
      grid  = (%gx, %one, %one)
      block = (%bsz, %one, %one)
      args  = (%x : f32)
  return
}

// -----

//===----------------------------------------------------------------------===//
// offload.shared_mem_alloc verifier
//===----------------------------------------------------------------------===//

// shared_mem_alloc must be inside an offload.func.
func.func @shmem_outside_func(%n: index) {
  // expected-error@+1 {{'offload.shared_mem_alloc' op must appear inside an offload.func}}
  %ptr = offload.shared_mem_alloc %n -> memref<i8>
  return
}

// -----

// shared_mem_alloc must be inside a device or global function.
offload.func @host_func_shmem(%n: index) exec_space = #offload.exec_space<host> {
  // expected-error@+1 {{'offload.shared_mem_alloc' op must appear inside an offload.func with exec_space = device or global}}
  %ptr = offload.shared_mem_alloc %n -> memref<i8>
  offload.return
}

// -----

//===----------------------------------------------------------------------===//
// offload.memcpy_to_symbol verifier
//===----------------------------------------------------------------------===//

// Target symbol must be an offload.global_var.
func.func @dummy_fn() { return }

func.func @memcpy_to_func(%src: memref<f32>, %n: index) {
  // expected-error@+1 {{'offload.memcpy_to_symbol' op symbol 'dummy_fn' does not name an offload.global_var in the same module}}
  offload.memcpy_to_symbol @dummy_fn src = %src : memref<f32> count = %n
  return
}

// -----

// Symbol must exist.
func.func @memcpy_to_nonexistent(%src: memref<f32>, %n: index) {
  // expected-error@+1 {{'offload.memcpy_to_symbol' op symbol 'nonexistent_global' does not name an offload.global_var in the same module}}
  offload.memcpy_to_symbol @nonexistent_global src = %src : memref<f32> count = %n
  return
}

// -----

//===----------------------------------------------------------------------===//
// LaunchBoundsAttr verifier (attribute-level)
//===----------------------------------------------------------------------===//

// maxThreadsPerBlock must be positive. (Tested via parse rejection of negative
// values — the integer storage type enforces this at parse time.)
// The attribute parser rejects malformed syntax outright.
// expected-error@+3 {{expected integer value}}
// expected-error@+2 {{failed to parse OFFLOAD_LaunchBoundsAttr parameter 'maxThreadsPerBlock'}}
func.func @bad_launch_bounds_syntax() attributes {
  lb = #offload.launch_bounds<>
} { return }

// -----

//===----------------------------------------------------------------------===//
// ReqdWorkgroupSizeAttr — parse rejects wrong number of dimensions
//===----------------------------------------------------------------------===//

// expected-error@+2 {{expected ','}}
func.func @bad_reqd_wg_size() attributes {
  rw = #offload.reqd_work_group_size<64>
} { return }
