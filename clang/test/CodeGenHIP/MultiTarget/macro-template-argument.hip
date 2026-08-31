// A macro defined differently per target, used as a template argument in a
// function template's body, gives each target's copy of the template a body
// of its own. The value is not among the body's statements but in a qualifier,
// a local variable's type, a sizeof'd type or a call's template arguments,
// and each copy must keep its own.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a -O2" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx942 -O2" \
// RUN:   -multi-target-aux-output=3:%t.gfx942.bc
// RUN: FileCheck --check-prefix=HOST %s < %t.ll
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=GFX90A %s
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck --check-prefix=GFX942 %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#if defined(__gfx942__)
#define BLOCK 256
#else
#define BLOCK 128
#endif
#if defined(__HIP_DEVICE_COMPILE__)
#define SIDE 2
#else
#define SIDE 1
#endif

template <unsigned N> struct block {
  static constexpr unsigned v = N;
  char pad[N];
};
template <unsigned N> __device__ unsigned get() { return N; }

template <class T> __device__ unsigned by_qualifier(T) {
  return block<BLOCK>::v;
}
template <class T> __device__ unsigned by_local_type(T) {
  block<BLOCK> b;
  return b.v + 1;
}
template <class T> __device__ unsigned by_sizeof(T) {
  return sizeof(block<BLOCK>) + 2;
}
template <class T> __device__ unsigned by_template_argument(T) {
  return get<BLOCK>() + 3;
}
template <class T> __host__ __device__ unsigned by_side(T) {
  return block<SIDE>::v;
}

__global__ void k(unsigned *o) {
  o[0] = by_qualifier(0);
  o[1] = by_local_type(0);
  o[2] = by_sizeof(0);
  o[3] = by_template_argument(0);
  o[4] = by_side(0);
}
unsigned h() { return by_side(0); }

// HOST-LABEL: define {{.*}} @_Z1hv(
// HOST: ret i32 1

// GFX90A-LABEL: define {{.*}} @_Z1kPj(
// GFX90A: store i32 128,
// GFX90A: store i32 129,
// GFX90A: store i32 130,
// GFX90A: store i32 131,
// GFX90A: store i32 2,

// GFX942-LABEL: define {{.*}} @_Z1kPj(
// GFX942: store i32 256,
// GFX942: store i32 257,
// GFX942: store i32 258,
// GFX942: store i32 259,
// GFX942: store i32 2,
