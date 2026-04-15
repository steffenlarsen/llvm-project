// RUN: mlir-opt --split-input-file --offload-split-single-source %s \
// RUN:   | FileCheck %s
// Verify with a custom gpu-module-name option.
// RUN: mlir-opt --split-input-file \
// RUN:   '--offload-split-single-source=gpu-module-name=my_device_mod' %s \
// RUN:   | FileCheck --check-prefix=CHECK-NAMED %s
// Verify dead-kernel-action=none: all kernels land in the primary module.
// RUN: mlir-opt --split-input-file \
// RUN:   '--offload-split-single-source=dead-kernel-action=none' %s \
// RUN:   | FileCheck --check-prefix=CHECK-NONE %s
// Verify dead-kernel-action=discard: unreferenced kernels are dropped.
// RUN: mlir-opt --split-input-file \
// RUN:   '--offload-split-single-source=dead-kernel-action=discard' %s \
// RUN:   | FileCheck --check-prefix=CHECK-DISCARD %s

// Tests for the OffloadSplitSingleSourcePass.
//
// After the pass:
//   exec_space = global      → gpu.func with gpu.kernel attr inside gpu.module
//   exec_space = device      → gpu.func (non-kernel) inside gpu.module
//   exec_space = host_device → gpu.func inside gpu.module AND func.func host side
//   exec_space = host        → func.func (host side only)
//   offload.kernel_launch    → gpu.launch_func referencing the gpu.module

//===----------------------------------------------------------------------===//
// Minimal: single __global__ kernel + host launcher
//===----------------------------------------------------------------------===//

// CHECK:      module attributes {gpu.container_module}
//
// Host side: @launchAdd becomes a func.func, kernel_launch becomes launch_func.
// CHECK:      func.func @launchAdd
// CHECK:        gpu.launch_func @offload_device_module::@addKernel
// CHECK-SAME:   blocks in (%{{[^ ,)]+}}, %{{[^ ,)]+}}, %{{[^ ,)]+}})
// CHECK-SAME:   threads in (%{{[^ ,)]+}}, %{{[^ ,)]+}}, %{{[^ ,)]+}})
//
// Device side: gpu.module contains addKernel as a kernel gpu.func.
// CHECK:      gpu.module @offload_device_module
// CHECK:        gpu.func @addKernel(%{{.*}}: memref<f32>, %{{.*}}: i32) kernel
// CHECK:          gpu.return

offload.func @addKernel(%a: memref<f32>, %n: i32)
    exec_space = #offload.exec_space<global> {
  offload.return
}

offload.func @launchAdd(%da: memref<f32>, %n: i32, %gx: index, %bsz: index)
    exec_space = #offload.exec_space<host> {
  %one = arith.constant 1 : index
  offload.kernel_launch @addKernel
      grid  = (%gx, %one, %one)
      block = (%bsz, %one, %one)
      args  = (%da, %n : memref<f32>, i32)
  offload.return
}

// -----

//===----------------------------------------------------------------------===//
// Custom gpu-module-name option
//===----------------------------------------------------------------------===//

// CHECK-NAMED: gpu.module @my_device_mod
// CHECK-NAMED:   gpu.func @namedKernel

offload.func @namedKernel(%x: f32) exec_space = #offload.exec_space<global> {
  offload.return
}

// -----

//===----------------------------------------------------------------------===//
// __device__ (non-kernel) function
//===----------------------------------------------------------------------===//

// Should appear in gpu.module but NOT have the gpu.kernel attribute.
//
// CHECK:      gpu.module @offload_device_module
// CHECK:        gpu.func @devHelper(%{{.*}}: f32) -> f32
// CHECK-NOT:    kernel
// CHECK:          gpu.return

offload.func @devHelper(%x: f32) -> f32
    exec_space = #offload.exec_space<device> {
  offload.return %x : f32
}

// -----

//===----------------------------------------------------------------------===//
// __host__ __device__ function — must appear on BOTH sides
//===----------------------------------------------------------------------===//

// CHECK:      func.func @hdMath(%{{.*}}: f32) -> f32
// CHECK:      gpu.module @offload_device_module
// CHECK:        gpu.func @hdMath(%{{.*}}: f32) -> f32

offload.func @hdMath(%x: f32) -> f32
    exec_space = #offload.exec_space<host_device> {
  %c = arith.constant 2.0 : f32
  %r = arith.mulf %x, %c : f32
  offload.return %r : f32
}

// -----

//===----------------------------------------------------------------------===//
// Pure __host__ function — must NOT appear in gpu.module
//===----------------------------------------------------------------------===//

// CHECK:      func.func @hostUtil(%{{.*}}: f32) -> f32
// CHECK-NOT:  gpu.func @hostUtil

offload.func @hostUtil(%x: f32) -> f32
    exec_space = #offload.exec_space<host> {
  offload.return %x : f32
}

// -----

//===----------------------------------------------------------------------===//
// launch_bounds → rocdl.flat_work_group_size on the gpu.func
//===----------------------------------------------------------------------===//

// CHECK:      gpu.func @boundsKernel
// CHECK-SAME: rocdl.flat_work_group_size = "1,256"

offload.func @boundsKernel(%x: f32)
    exec_space = #offload.exec_space<global>
    launch_bounds = #offload.launch_bounds<256> {
  offload.return
}

// -----

//===----------------------------------------------------------------------===//
// Function body with arithmetic is cloned correctly into gpu.func
//===----------------------------------------------------------------------===//

// CHECK:      gpu.func @bodyKernel(%{{.*}}: f32) kernel
// CHECK:        arith.mulf
// CHECK:        gpu.return

offload.func @bodyKernel(%x: f32)
    exec_space = #offload.exec_space<global> {
  %two = arith.constant 2.0 : f32
  %r   = arith.mulf %x, %two : f32
  offload.return
}

// -----

//===----------------------------------------------------------------------===//
// Multiple referenced kernels — all end up in the primary gpu.module
//===----------------------------------------------------------------------===//

// CHECK:      gpu.module @offload_device_module
// CHECK-DAG:    gpu.func @kernelA
// CHECK-DAG:    gpu.func @kernelB
// CHECK-NOT:  gpu.module @offload_device_module_deferred

offload.func @kernelA(%a: memref<f32>) exec_space = #offload.exec_space<global> {
  offload.return
}
offload.func @kernelB(%b: memref<i32>) exec_space = #offload.exec_space<global> {
  offload.return
}
offload.func @launchBoth(%a: memref<f32>, %b: memref<i32>,
                          %gx: index, %bsz: index)
    exec_space = #offload.exec_space<host> {
  %one = arith.constant 1 : index
  offload.kernel_launch @kernelA
      grid = (%gx, %one, %one) block = (%bsz, %one, %one) args = (%a : memref<f32>)
  offload.kernel_launch @kernelB
      grid = (%gx, %one, %one) block = (%bsz, %one, %one) args = (%b : memref<i32>)
  offload.return
}

// -----

//===----------------------------------------------------------------------===//
// Full single-source module: target attr, kernel, launcher, and device helper
//===----------------------------------------------------------------------===//

// CHECK: module attributes {gpu.container_module
// CHECK-SAME: offload.target

// Host side.
// CHECK: func.func @vecAdd_launch
// CHECK:   gpu.launch_func @offload_device_module::@vecAdd

// Device side.
// CHECK: gpu.module @offload_device_module
// CHECK:   gpu.func @scale
// CHECK:   gpu.func @vecAdd{{.*}}kernel

module attributes {
  offload.target = #offload.target<runtime = "hip", architectures = ["gfx90a"]>
} {

  offload.func @scale(%x: f32) -> f32
      exec_space = #offload.exec_space<device> {
    %two = arith.constant 2.0 : f32
    %r = arith.mulf %x, %two : f32
    offload.return %r : f32
  }

  offload.func @vecAdd(%a: memref<f32>, %b: memref<f32>,
                       %c: memref<f32>, %n: i32)
      exec_space = #offload.exec_space<global>
      launch_bounds = #offload.launch_bounds<256> {
    offload.return
  }

  func.func @vecAdd_launch(%da: memref<f32>, %db: memref<f32>,
                            %dc: memref<f32>, %n: i32) {
    %c256 = arith.constant 256 : index
    %c1   = arith.constant 1   : index
    %ni   = arith.index_cast %n : i32 to index
    %gx   = arith.ceildivsi %ni, %c256 : index
    offload.kernel_launch @vecAdd
        grid  = (%gx, %c1, %c1)
        block = (%c256, %c1, %c1)
        args  = (%da, %db, %dc, %n : memref<f32>, memref<f32>, memref<f32>, i32)
    return
  }
}

// -----

//===----------------------------------------------------------------------===//
// Multi-block kernel — all blocks must be cloned into gpu.func
//===----------------------------------------------------------------------===//

// CHECK:      gpu.func @conditionalKernel(%{{.*}}: i32, %{{.*}}: memref<i32>) kernel
// Verify that the conditional branch structure is preserved (two successors).
// CHECK:        cf.cond_br
// CHECK:        gpu.return

offload.func @conditionalKernel(%cond: i32, %out: memref<i32>)
    exec_space = #offload.exec_space<global> {
  %zero = arith.constant 0 : i32
  %one  = arith.constant 1 : i32
  %isZ  = arith.cmpi eq, %cond, %zero : i32
  cf.cond_br %isZ, ^store_zero, ^store_one
^store_zero:
  memref.store %zero, %out[] : memref<i32>
  cf.br ^done
^store_one:
  memref.store %one, %out[] : memref<i32>
  cf.br ^done
^done:
  offload.return
}

// -----

//===----------------------------------------------------------------------===//
// Multi-block host function — all blocks cloned into func.func
//===----------------------------------------------------------------------===//

// CHECK:      func.func @conditionalHost(%{{.*}}: i32) -> i32
// CHECK:        cf.cond_br
// CHECK:        return

offload.func @conditionalHost(%cond: i32) -> i32
    exec_space = #offload.exec_space<host> {
  %zero = arith.constant 0 : i32
  %one  = arith.constant 1 : i32
  %isZ  = arith.cmpi eq, %cond, %zero : i32
  cf.cond_br %isZ, ^ret_zero, ^ret_one
^ret_zero:
  offload.return %zero : i32
^ret_one:
  offload.return %one : i32
}

// -----

//===----------------------------------------------------------------------===//
// Two-module split: unreferenced kernel goes to deferred module
//===----------------------------------------------------------------------===//

// The launched kernel goes to the primary module; the unreferenced one to the
// deferred module.  The host func.func is emitted before the gpu.modules.
//
// CHECK:      func.func @launchSite
// CHECK:        gpu.launch_func @offload_device_module::@launchedKernel
// CHECK:      gpu.module @offload_device_module
// CHECK:        gpu.func @launchedKernel{{.*}}kernel
// CHECK-NOT:    gpu.func @unusedKernel
// CHECK:      gpu.module @offload_device_module_deferred
// CHECK:        gpu.func @unusedKernel{{.*}}kernel
// CHECK-NOT:    gpu.func @launchedKernel

offload.func @launchedKernel(%a: memref<f32>)
    exec_space = #offload.exec_space<global> {
  offload.return
}

offload.func @unusedKernel(%b: memref<i32>)
    exec_space = #offload.exec_space<global> {
  offload.return
}

offload.func @launchSite(%a: memref<f32>, %gx: index, %bsz: index)
    exec_space = #offload.exec_space<host> {
  %one = arith.constant 1 : index
  offload.kernel_launch @launchedKernel
      grid  = (%gx, %one, %one)
      block = (%bsz, %one, %one)
      args  = (%a : memref<f32>)
  offload.return
}

// -----

//===----------------------------------------------------------------------===//
// Replicable helper — cloned into both modules
//===----------------------------------------------------------------------===//

// A device helper with no global deps is replicable: it appears in the primary
// module (used by @refKernel) and also in the deferred module (used by
// @deferredKernel).
//
// CHECK:      gpu.module @offload_device_module
// CHECK-DAG:    gpu.func @pureHelper
// CHECK-DAG:    gpu.func @refKernel{{.*}}kernel
// CHECK:      gpu.module @offload_device_module_deferred
// CHECK-DAG:    gpu.func @pureHelper
// CHECK-DAG:    gpu.func @deferredKernel{{.*}}kernel

offload.func @pureHelper(%x: f32) -> f32
    exec_space = #offload.exec_space<device> {
  %two = arith.constant 2.0 : f32
  %r = arith.mulf %x, %two : f32
  offload.return %r : f32
}

offload.func @refKernel(%x: f32) exec_space = #offload.exec_space<global> {
  offload.return
}

offload.func @deferredKernel(%x: f32) exec_space = #offload.exec_space<global> {
  offload.return
}

offload.func @launchRef(%x: f32, %gx: index, %bsz: index)
    exec_space = #offload.exec_space<host> {
  %one = arith.constant 1 : index
  offload.kernel_launch @refKernel
      grid  = (%gx, %one, %one)
      block = (%bsz, %one, %one)
      args  = (%x : f32)
  offload.return
}

// -----

//===----------------------------------------------------------------------===//
// All kernels referenced — no deferred module emitted
//===----------------------------------------------------------------------===//

// When every kernel has a launch site, the deferred module is omitted entirely.
//
// CHECK:      gpu.module @offload_device_module
// CHECK-DAG:    gpu.func @kernelX{{.*}}kernel
// CHECK-DAG:    gpu.func @kernelY{{.*}}kernel
// CHECK-NOT:  gpu.module @offload_device_module_deferred

offload.func @kernelX(%a: memref<f32>) exec_space = #offload.exec_space<global> {
  offload.return
}
offload.func @kernelY(%b: memref<i32>) exec_space = #offload.exec_space<global> {
  offload.return
}
offload.func @launchBothXY(%a: memref<f32>, %b: memref<i32>,
                             %gx: index, %bsz: index)
    exec_space = #offload.exec_space<host> {
  %one = arith.constant 1 : index
  offload.kernel_launch @kernelX
      grid = (%gx, %one, %one) block = (%bsz, %one, %one) args = (%a : memref<f32>)
  offload.kernel_launch @kernelY
      grid = (%gx, %one, %one) block = (%bsz, %one, %one) args = (%b : memref<i32>)
  offload.return
}

// -----

//===----------------------------------------------------------------------===//
// Custom deferred-gpu-module-name: unreferenced kernel goes to named module
//===----------------------------------------------------------------------===//

// The CHECK-NAMED prefix run already uses --gpu-module-name=my_device_mod for
// the whole file.  This section verifies the deferred module is named with the
// default name when the option is not set, and that it contains the unreferenced
// kernel.
//
// CHECK:      gpu.module @offload_device_module_deferred
// CHECK:        gpu.func @deferKernelNamed{{.*}}kernel

offload.func @refKernelNamed(%a: memref<f32>)
    exec_space = #offload.exec_space<global> {
  offload.return
}
offload.func @deferKernelNamed(%b: memref<i32>)
    exec_space = #offload.exec_space<global> {
  offload.return
}
offload.func @launchNamed(%a: memref<f32>, %gx: index, %bsz: index)
    exec_space = #offload.exec_space<host> {
  %one = arith.constant 1 : index
  offload.kernel_launch @refKernelNamed
      grid  = (%gx, %one, %one)
      block = (%bsz, %one, %one)
      args  = (%a : memref<f32>)
  offload.return
}

// -----

//===----------------------------------------------------------------------===//
// dead-kernel-action=none: all kernels go to the primary module
//===----------------------------------------------------------------------===//

// With dead-kernel-action=none, unreferenced kernels are NOT separated into a
// deferred module — they land in the primary module alongside launched kernels.
//
// CHECK-NONE:      gpu.module @offload_device_module
// CHECK-NONE-DAG:    gpu.func @refKernelNone{{.*}}kernel
// CHECK-NONE-DAG:    gpu.func @unrefKernelNone{{.*}}kernel
// CHECK-NONE-NOT:  gpu.module @offload_device_module_deferred

offload.func @refKernelNone(%a: memref<f32>)
    exec_space = #offload.exec_space<global> {
  offload.return
}

offload.func @unrefKernelNone(%b: memref<i32>)
    exec_space = #offload.exec_space<global> {
  offload.return
}

offload.func @launchNone(%a: memref<f32>, %gx: index, %bsz: index)
    exec_space = #offload.exec_space<host> {
  %one = arith.constant 1 : index
  offload.kernel_launch @refKernelNone
      grid  = (%gx, %one, %one)
      block = (%bsz, %one, %one)
      args  = (%a : memref<f32>)
  offload.return
}

// -----

//===----------------------------------------------------------------------===//
// dead-kernel-action=discard: unreferenced kernels are dropped entirely
//===----------------------------------------------------------------------===//

// With dead-kernel-action=discard, kernels with no launch site are erased
// rather than placed in a deferred module.
//
// CHECK-DISCARD:      gpu.module @offload_device_module
// CHECK-DISCARD:        gpu.func @refKernelDiscard{{.*}}kernel
// CHECK-DISCARD-NOT:    gpu.func @unrefKernelDiscard
// CHECK-DISCARD-NOT:  gpu.module @offload_device_module_deferred

offload.func @refKernelDiscard(%x: f32)
    exec_space = #offload.exec_space<global> {
  offload.return
}

offload.func @unrefKernelDiscard(%x: f32)
    exec_space = #offload.exec_space<global> {
  offload.return
}

offload.func @launchDiscard(%x: f32, %gx: index, %bsz: index)
    exec_space = #offload.exec_space<host> {
  %one = arith.constant 1 : index
  offload.kernel_launch @refKernelDiscard
      grid  = (%gx, %one, %one)
      block = (%bsz, %one, %one)
      args  = (%x : f32)
  offload.return
}
