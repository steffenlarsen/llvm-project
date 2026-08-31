// Layout read where a template's head is parsed: the type an alias template
// stands for and default template arguments. Each target evaluates them with
// its own layout.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx90a -O1 -x hip \
// RUN:   -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-std=c++17 %{hip_device} %{host_aux_triple} -target-cpu gfx90a -O1" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=DEV %s
// RUN: FileCheck --check-prefix=HOST %s < %t.host.ll

#define __global__ __attribute__((global))
#define __device__ __attribute__((device))

template <class T> using Arr = T[sizeof(long double)];
template <int N = sizeof(long double)> __device__ int by_default() { return N; }
template <int N = alignof(long double)> struct Holder { char v[N]; };

__global__ void kernel(int *out) {
  Arr<char> a;
  out[0] = sizeof(a);
  out[1] = by_default();
  out[2] = sizeof(Holder<>);
}

int on_host() {
  Arr<char> a;
  return sizeof(a) + sizeof(Holder<>);
}

// DEV-LABEL: define {{.*}}@_Z6kernelPi(
// DEV: store i32 8,
// DEV: store i32 8,
// DEV: store i32 8,
// DEV-NOT: store i32 16
// HOST-LABEL: define {{.*}}@_Z7on_hostv(
// HOST: ret i32 32
