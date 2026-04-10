// Tests that running --rocdl-attach-target twice after --offload-split-single-source
// produces a gpu.module with two #rocdl.target attributes — the foundation of
// multi-arch fat-binary compilation.
//
// This mirrors what populateCIRToLLVMPasses() does in the CIR pipeline when
// offloadArchs has more than one element: one GpuROCDLAttachTargetPass invocation
// per arch, each appending to the gpu.module's targets array.

// RUN: mlir-opt \
// RUN:   --offload-split-single-source \
// RUN:   '--rocdl-attach-target=chip=gfx906 triple=amdgcn-amd-amdhsa' \
// RUN:   '--rocdl-attach-target=chip=gfx90a triple=amdgcn-amd-amdhsa' \
// RUN:   %s | FileCheck %s

// The host launcher is a regular func.func referencing the gpu.module.
// CHECK:      func.func @launchAdd
// CHECK:        gpu.launch_func @offload_device_module::@addKernel

// The gpu.module should carry both targets.
// CHECK:      gpu.module @offload_device_module
// CHECK-SAME: #rocdl.target<chip = "gfx906">
// CHECK-SAME: #rocdl.target<chip = "gfx90a">

// The kernel appears inside the gpu.module.
// CHECK:      gpu.func @addKernel({{.*}}) kernel

offload.func @addKernel(%a: memref<f32>, %n: i32)
    exec_space = #offload.exec_space<global> {
  offload.return
}

offload.func @launchAdd(%a: memref<f32>, %n: i32,
                        %gx: index, %bsz: index)
    exec_space = #offload.exec_space<host> {
  %one = arith.constant 1 : index
  offload.kernel_launch @addKernel
      grid  = (%gx, %one, %one)
      block = (%bsz, %one, %one)
      args  = (%a, %n : memref<f32>, i32)
  offload.return
}
