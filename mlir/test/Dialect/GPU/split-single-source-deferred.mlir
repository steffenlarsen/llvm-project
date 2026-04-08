// RUN: mlir-opt %s -gpu-split-single-source | FileCheck %s
// RUN: mlir-opt %s -gpu-split-single-source='dead-kernel-action=discard' \
// RUN:   | FileCheck %s --check-prefix=DISCARD

// With no gpu-module-name given, the pass finds its input by attribute rather
// than by name.  This matters because the producer's module name varies with
// the compilation mode, and a name-based lookup silently does nothing when it
// guesses wrong.
//
// Dependencies between device functions are expressed here as plain symbol
// references rather than call ops: neither func.call nor llvm.call may target
// a gpu.func, and the producer's own call op is not available to mlir-opt.
// The pass reasons about symbol uses, so this exercises the same edges.

module attributes {gpu.container_module} {
  // The name deliberately does not match any default the pass might assume.
  gpu.module @dev_gfx90a [#nvvm.target] attributes {
      gpu.offload_device_module, gpu.offload_target = "gfx90a"} {
    // An external declaration the device code needs.  It is neither a kernel
    // nor a global, so none of the classification steps claim it; it still has
    // to be carried out of the input module, which is erased at the end.
    llvm.func @ext_decl(f32) -> f32

    gpu.func @helper(%arg0: f32) -> f32 {
      %0 = llvm.call @ext_decl(%arg0) : (f32) -> f32
      gpu.return %0 : f32
    }

    // Reached only from the unlaunched kernel.
    gpu.func @helper_dead(%arg0: f32) -> f32 {
      gpu.return %arg0 : f32
    }

    gpu.func @k_launched(%arg0: f32) kernel attributes {uses = @helper} {
      gpu.return
    }

    gpu.func @k_unlaunched(%arg0: f32) kernel attributes {uses = @helper_dead} {
      gpu.return
    }
  }

  llvm.func @host(%arg0: f32) {
    %c1 = arith.constant 1 : index
    gpu.launch_func @dev_gfx90a::@k_launched
        blocks in (%c1, %c1, %c1) threads in (%c1, %c1, %c1)
        args(%arg0 : f32)
    llvm.return
  }
}

// The primary module keeps the input's name so launch sites stay valid, and
// keeps its target and offload marker.
// CHECK: gpu.module @dev_gfx90a [#nvvm.target]
// CHECK-SAME: gpu.offload_device_module
// CHECK-SAME: gpu.offload_target = "gfx90a"
// CHECK-DAG: llvm.func @ext_decl
// CHECK-DAG: gpu.func @helper
// CHECK-DAG: gpu.func @k_launched

// The deferred module is a standalone device image: it carries the same target
// so it is not dropped as untargeted, is marked deferred so it is packaged and
// registered separately, and holds a copy of the declaration its code needs.
// CHECK: gpu.module @dev_gfx90a_deferred [#nvvm.target]
// CHECK-SAME: gpu.offload_deferred
// CHECK-SAME: gpu.offload_target = "gfx90a"
// CHECK-DAG: llvm.func @ext_decl
// CHECK-DAG: gpu.func @k_unlaunched

// In discard mode there is no second module.  The helper reached from the
// launched kernel survives and the one reached only from the dropped kernel
// does not -- reversing the dependency edges would get both wrong.
// DISCARD: gpu.module @dev_gfx90a
// DISCARD-NOT: @dev_gfx90a_deferred
// DISCARD-DAG: llvm.func @ext_decl
// DISCARD-DAG: gpu.func @helper
// DISCARD-DAG: gpu.func @k_launched
// DISCARD-NOT: gpu.func @helper_dead
// DISCARD-NOT: gpu.func @k_unlaunched
