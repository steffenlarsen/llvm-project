// REQUIRES: amdgpu-registered-target
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx90a -x hip \
// RUN: -std=c++11 -fclangir -fcuda-is-device -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx90a -x hip \
// RUN: -std=c++11 -fcuda-is-device -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

//===----------------------------------------------------------------------===//
// `[[clang::atomic]]` adjusts the atomic options for the statement it is
// attached to, which decides the AMDGPU metadata on the resulting atomicrmw.
// The "no.X" metadata asserts the *absence* of a memory kind, so it is emitted
// when the corresponding option is off.
//
// The LLVM checks are shared with the classic CodeGen run line, so they pin the
// two paths to the same metadata. Neither the order the metadata is printed in
// nor the presence of !noalias.addrspace is asserted: the two paths differ on
// both and neither is semantically significant here.
//
// This is the shape HIP's amd_hip_atomic.h uses for its unsafeAtomic*
// functions.
//===----------------------------------------------------------------------===//

// The default: neither fine-grained nor remote memory is assumed present.
// LLVM-LABEL: @_Z9plain_addPff
// LLVM: atomicrmw fadd ptr %{{.+}}, float %{{.+}} syncscope("agent") monotonic, align 4{{.*}}!amdgpu.{{(no.remote.memory|no.fine.grained.memory)}} !{{[0-9]+}}{{.*}}!amdgpu.{{(no.remote.memory|no.fine.grained.memory)}}
__device__ float plain_add(float *p, float v) {
  return __hip_atomic_fetch_add(p, v, __ATOMIC_RELAXED, 4);
}

// Declaring the memory fine-grained suppresses no.fine.grained.memory: the
// compiler may no longer assert its absence.
// LLVM-LABEL: @_Z10unsafe_addPff
// LLVM: atomicrmw fadd ptr %{{.+}}, float %{{.+}} syncscope("agent") monotonic
// LLVM-NOT: amdgpu.no.fine.grained.memory
__device__ float unsafe_add(float *p, float v) {
  [[clang::atomic(fine_grained_memory)]] {
    return __hip_atomic_fetch_add(p, v, __ATOMIC_RELAXED, 4);
  }
}

// ignore_denormal_mode adds the third metadata; it only applies to a float add.
// LLVM-LABEL: @_Z9no_fg_addPff
// LLVM: atomicrmw fadd ptr %{{.+}}, float %{{.+}} syncscope("agent") monotonic, align 4{{.*}}!amdgpu.ignore.denormal.mode
__device__ float no_fg_add(float *p, float v) {
  [[clang::atomic(no_fine_grained_memory, ignore_denormal_mode)]] {
    return __hip_atomic_fetch_add(p, v, __ATOMIC_RELAXED, 4);
  }
}

// A double add gets no ignore.denormal.mode even when the option is on.
// LLVM-LABEL: @_Z9no_fg_addPdd
// LLVM: atomicrmw fadd ptr %{{.+}}, double %{{.+}} syncscope("agent") monotonic
// LLVM-NOT: amdgpu.ignore.denormal.mode
__device__ double no_fg_add(double *p, double v) {
  [[clang::atomic(no_fine_grained_memory, ignore_denormal_mode)]] {
    return __hip_atomic_fetch_add(p, v, __ATOMIC_RELAXED, 4);
  }
}

// A HIP scope names the same thing as the generic scope of the same name: the
// `-one-as` spellings belong to OpenCL with non-seq_cst ordering, never to HIP.
// LLVM-LABEL: @_Z12workgroup_ddPdd
// LLVM: atomicrmw fadd ptr %{{.+}}, double %{{.+}} syncscope("workgroup") monotonic
__device__ double workgroup_dd(double *p, double v) {
  return __hip_atomic_fetch_add(p, v, __ATOMIC_RELAXED, 3);
}

// The options are restored after the attributed statement, so this add is back
// to the default.
// LLVM-LABEL: @_Z12after_scopedPff
// LLVM: atomicrmw fadd ptr %{{.+}}, float %{{.+}} syncscope("agent") monotonic, align 4{{.*}}!amdgpu.no.fine.grained.memory
__device__ float after_scoped(float *p, float v) {
  [[clang::atomic(fine_grained_memory)]] { (void)0; }
  return __hip_atomic_fetch_add(p, v, __ATOMIC_RELAXED, 4);
}

__global__ void k(float *p, double *d, float *o) {
  o[0] = plain_add(p, 1.0f) + unsafe_add(p, 2.0f) + no_fg_add(p, 3.0f) +
         after_scoped(p, 4.0f) + (float)workgroup_dd(d, 5.0) +
         (float)no_fg_add(d, 6.0);
}
