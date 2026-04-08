// RUN: mlir-opt %s -gpu-split-single-source='gpu-module-name=offload_device_module dead-kernel-action=discard' | FileCheck %s

// In "discard" mode a kernel is kept only if it is reachable from a launch.
// A kernel reached through a function pointer has no gpu.launch_func, so
// launches alone under-approximate liveness.  Such kernels are marked
// "gpu.address_taken" by the producer and must survive.

module attributes {gpu.container_module} {
  gpu.module @offload_device_module {
    // Launched directly -- kept.
    gpu.func @k_launched(%arg0: !llvm.ptr) kernel {
      gpu.return
    }

    // Never launched here, but its host stub's address is taken, so it may
    // be launched indirectly at runtime -- must be kept.
    gpu.func @k_address_taken(%arg0: !llvm.ptr) kernel attributes {gpu.address_taken} {
      gpu.return
    }

    // Neither launched nor address-taken -- genuinely dead, discarded.
    gpu.func @k_dead(%arg0: !llvm.ptr) kernel {
      gpu.return
    }
  }

  llvm.func @host(%arg0: !llvm.ptr) {
    %c1 = arith.constant 1 : index
    gpu.launch_func @offload_device_module::@k_launched
        blocks in (%c1, %c1, %c1) threads in (%c1, %c1, %c1)
        args(%arg0 : !llvm.ptr)
    llvm.return
  }
}

// CHECK-DAG: gpu.func @k_launched
// CHECK-DAG: gpu.func @k_address_taken
// CHECK-NOT: gpu.func @k_dead
