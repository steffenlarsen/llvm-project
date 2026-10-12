// REQUIRES: amdgpu-registered-target
// RUN: %clang_cc1 -triple amdgpu9.0a-amd-amdhsa -x hip -fclangir -fcuda-is-device -emit-cir %s -o - \
// RUN: | FileCheck --check-prefix=CIR %s
// RUN: %clang_cc1 -triple amdgpu9.0a-amd-amdhsa -x hip -fclangir -fcuda-is-device -emit-llvm %s -o - \
// RUN: | FileCheck --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple amdgpu9.0a-amd-amdhsa -x hip -fcuda-is-device -emit-llvm %s -o - \
// RUN: | FileCheck --check-prefix=LLVM %s

// HIP atomics treat thread-private memory as undefined, so an atomic
// read-modify-write or compare-exchange on a flat pointer is marked as not
// accessing the private address space.

#define __device__ __attribute__((device))

__device__ void flat_rmw(int *p) {
  __hip_atomic_fetch_add(p, 1, __ATOMIC_RELAXED, __HIP_MEMORY_SCOPE_AGENT);
}

// CIR-LABEL: @_Z8flat_rmwPi
// CIR: cir.atomic.fetch add {{.*}} {cir.amdgpu_no_fine_grained_memory, cir.amdgpu_no_private_memory, cir.amdgpu_no_remote_memory}
// LLVM-LABEL: @_Z8flat_rmwPi
// LLVM: atomicrmw add ptr %{{.+}}, i32 %{{.+}} syncscope("agent") monotonic, align 4, !noalias.addrspace ![[PRIVATE:[0-9]+]]

__device__ void flat_cmpxchg(int *p, int e) {
  __hip_atomic_compare_exchange_strong(p, &e, 2, __ATOMIC_RELAXED,
                                       __ATOMIC_RELAXED,
                                       __HIP_MEMORY_SCOPE_AGENT);
}

// CIR-LABEL: @_Z12flat_cmpxchgPii
// CIR: cir.atomic.cmpxchg{{.*}} {cir.amdgpu_no_private_memory}
// LLVM-LABEL: @_Z12flat_cmpxchgPii
// LLVM: cmpxchg ptr %{{.+}}, i32 %{{.+}}, i32 %{{.+}} syncscope("agent") monotonic monotonic, align 4, !noalias.addrspace ![[PRIVATE]]

// A global pointer cannot point to private memory, so the metadata is only
// emitted for a flat one.
__device__ void global_rmw(__attribute__((address_space(1))) int *p) {
  __hip_atomic_fetch_add(p, 1, __ATOMIC_RELAXED, __HIP_MEMORY_SCOPE_AGENT);
}

// LLVM-LABEL: @_Z10global_rmwPU3AS1i
// LLVM: atomicrmw add ptr addrspace(1) %{{.+}}, i32 %{{.+}} syncscope("agent") monotonic, align 4
// LLVM-NOT: !noalias.addrspace
// LLVM: ret void

// The GNU atomic builtins do not make private memory undefined.
__device__ void gnu_rmw(int *p) {
  __atomic_fetch_add(p, 1, __ATOMIC_RELAXED);
}

// CIR-LABEL: @_Z7gnu_rmwPi
// CIR: cir.atomic.fetch add
// CIR-NOT: cir.amdgpu_no_private_memory
// CIR: cir.return
// LLVM-LABEL: @_Z7gnu_rmwPi
// LLVM: atomicrmw add ptr %{{.+}}, i32 %{{.+}} monotonic, align 4
// LLVM-NOT: !noalias.addrspace
// LLVM: ret void

// LLVM: ![[PRIVATE]] = !{i32 5, i32 6}
