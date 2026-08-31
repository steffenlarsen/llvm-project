// Class templates whose declarations differ between the targets, through a
// member function's default argument, a member enumerator or a nested class,
// between device architectures or between the host and the devices. Each is
// parsed again for each target, and each target's specializations are its
// own. They were kept shared, and later rejected.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx90a -O2" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx942 -O2" \
// RUN:   -multi-target-aux-output=3:%t.gfx942.bc
// RUN: FileCheck -check-prefix=HOST %s < %t.host.ll
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=GFX90A %s
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefix=GFX942 %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))
#define __host__ __attribute__((host))

#if defined(__gfx942__)
constexpr int W = 42;
#else
constexpr int W = 90;
#endif

template <class T> struct Arg {
  __host__ __device__ int f(int a = sizeof(int[W])) const { return a; }
};
template <class T> struct Side {
  __host__ __device__ int f(int a = sizeof(long double)) const { return a; }
};
template <class T> struct Enum {
  enum { N = W };
  __device__ int f() const { return N; }
};
template <class T> struct Nested {
  struct In {
    int a[W];
  };
  __device__ int f() const { return sizeof(In); }
};

// GFX90A-LABEL: define {{.*}} @_Z1kPi(
// GFX90A: store i32 360, ptr addrspace(1)
// GFX90A: store i32 8, ptr addrspace(1)
// GFX90A: store i32 90, ptr addrspace(1)
// GFX90A: store i32 360, ptr addrspace(1)
// GFX942-LABEL: define {{.*}} @_Z1kPi(
// GFX942: store i32 168, ptr addrspace(1)
// GFX942: store i32 8, ptr addrspace(1)
// GFX942: store i32 42, ptr addrspace(1)
// GFX942: store i32 168, ptr addrspace(1)
__global__ void k(int *p) {
  p[0] = Arg<int>().f();
  p[1] = Side<int>().f();
  p[2] = Enum<int>().f();
  p[3] = Nested<int>().f();
}

// HOST-LABEL: define {{.*}} @_Z1hv(
// HOST: ret i32 376
int h() { return Arg<int>().f() + Side<int>().f(); }
