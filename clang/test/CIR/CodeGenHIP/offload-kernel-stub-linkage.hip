// Verify that the host-side __device_stub__ symbol emitted for a __global__
// kernel gets the linkage the kernel itself would get, derived from the AST
// rather than from isInlined()/templated-kind, and that dso_local follows from
// linkage and visibility instead of being forced on.
//
// Two things go wrong otherwise:
//
//   * A kernel that is both static and templated is internal, not
//     linkonce_odr.  Giving it linkonce_odr makes the symbol preemptible.
//   * Forcing dso_local on a preemptible linkonce_odr stub makes references to
//     it (the address taken by __hipRegisterFunction) use a direct PC32
//     relocation.
//
// Either one makes the linker reject the object when it is linked into a
// shared library:
//
//   relocation R_X86_64_PC32 against symbol `_ZL26__device_stub__...'
//   can not be used when making a shared object; recompile with -fPIC
//
// REQUIRES: amdgpu-registered-target
// RUN: %clang -x hip --offload-arch=gfx90a -fclangir -O1 -fPIC -Wno-everything \
// RUN:   -I%S/Inputs -c -o %t-cir.o %s
// RUN: llvm-nm %t-cir.o | FileCheck --check-prefix=CIR %s
//
// The same source through the classic codegen path, as a reference.
// RUN: %clang -x hip --offload-arch=gfx90a -O1 -fPIC -Wno-everything \
// RUN:   -I%S/Inputs -c -o %t-ogcg.o %s
// RUN: llvm-nm %t-ogcg.o | FileCheck --check-prefix=OGCG %s
//
// The regression itself: the object must be linkable into a shared library.
// RUN: %clang -shared %t-cir.o -o %t-cir.so

#include "hip_runtime_minimal.h"

// Declared here rather than in the shared Inputs header: only the classic
// codegen path emits the stub bodies, and adding this to the header would
// perturb the other tests that include it.
extern "C" hipError_t hipLaunchKernel(const void *func, dim3 gridDim,
                                      dim3 blockDim, void **args,
                                      size_t sharedMem, hipStream_t stream);

template <int N> static __global__ void tmpl_static(float *o) { o[0] = N; }
template <int N> __global__ void tmpl_extern(float *o) { o[0] = N; }
static __global__ void plain_static(float *o) { o[0] = 1; }
__global__ void plain_extern(float *o) { o[0] = 2; }

// Reference each kernel so its host-side stub is emitted.
void launch(float *o) {
  tmpl_static<7><<<1, 1>>>(o);
  tmpl_extern<7><<<1, 1>>>(o);
  plain_static<<<1, 1>>>(o);
  plain_extern<<<1, 1>>>(o);
}

// A static kernel is internal ('t') whether or not it is templated.  An
// externally visible template instantiation is weak ('W'), and a plain
// external kernel is strong ('T').

// CIR-DAG: t _ZL26__device_stub__tmpl_staticILi7EEvPf
// CIR-DAG: t _ZL27__device_stub__plain_staticPf
// CIR-DAG: W _Z26__device_stub__tmpl_externILi7EEvPf
// CIR-DAG: T _Z27__device_stub__plain_externPf

// OGCG-DAG: t _ZL26__device_stub__tmpl_staticILi7EEvPf
// OGCG-DAG: t _ZL27__device_stub__plain_staticPf
// OGCG-DAG: W _Z26__device_stub__tmpl_externILi7EEvPf
// OGCG-DAG: T _Z27__device_stub__plain_externPf
