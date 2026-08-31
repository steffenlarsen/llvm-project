// Member and friend function templates defined in a class whose definition
// differs per target. Each target's copy of the class instantiates its own
// copy's specialization, which the other targets' code names by its mangled
// name. They were undefined at the device link, and a friend template of a
// class whose copies lay out differently was not found.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx1030 -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx1030 -O2" \
// RUN:   -multi-target-aux-output=2:%t.gfx1030.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx90a -O2" \
// RUN:   -multi-target-aux-output=3:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx942 -O2" \
// RUN:   -multi-target-aux-output=4:%t.gfx942.bc
// RUN: llvm-dis -o - %t.gfx1030.bc | FileCheck -check-prefix=OTHER %s
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=OTHER %s
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefix=GFX942 %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

struct M {
  int v = 3;
#if defined(__gfx942__)
  template <class T> __device__ int peek(T) const { return v * 10; }
#else
  template <class T> __device__ int peek(T) const { return v; }
#endif
};

struct F {
  int v = 3;
#if defined(__gfx942__)
  template <class T> __device__ friend int look(const F &f, T) { return f.v * 10; }
#else
  template <class T> __device__ friend int look(const F &f, T) { return f.v; }
#endif
};

struct L {
  int v = 3;
#if defined(__gfx942__)
  int w;
#endif
  template <class T> __device__ friend int size(const L &, T) { return sizeof(L); }
};

// OTHER-LABEL: define {{.*}} @_Z1kPi(
// OTHER: store i32 3, ptr addrspace(1)
// OTHER: store i32 3, ptr addrspace(1)
// OTHER: store i32 4, ptr addrspace(1)
// GFX942-LABEL: define {{.*}} @_Z1kPi(
// GFX942: store i32 30, ptr addrspace(1)
// GFX942: store i32 30, ptr addrspace(1)
// GFX942: store i32 8, ptr addrspace(1)
__global__ void k(int *p) {
  p[0] = M().peek(p[0]);
  p[1] = look(F(), p[1]);
  p[2] = size(L(), p[2]);
}
