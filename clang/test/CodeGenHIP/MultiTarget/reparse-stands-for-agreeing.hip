// A declaration's first parse stands for the targets that agree with its
// leader, the host. The others parse it again, the first of them leading and
// standing for the rest that agree with it. Here gfx90a and gfx942 agree with
// each other but not with the host, and gfx1100 agrees with none of them, so
// the declaration has three copies. What gfx90a's parse instantiated is
// gfx942's too.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -fsyntax-only %s -ast-dump -ast-dump-filter use \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx90a" \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx942" \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx1100" \
// RUN:   | FileCheck --check-prefix=AST %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O1 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -O1 -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -O1 -target-cpu gfx942" \
// RUN:   -multi-target-aux-output=3:%t.gfx942.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -O1 -target-cpu gfx1100" \
// RUN:   -multi-target-aux-output=4:%t.gfx1100.bc
// RUN: FileCheck --check-prefix=HOST %s < %t.host.ll
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefixes=DEV,W64 %s \
// RUN:   --implicit-check-not="declare {{.*}}@_Z"
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck --check-prefixes=DEV,W64 %s \
// RUN:   --implicit-check-not="declare {{.*}}@_Z"
// RUN: llvm-dis %t.gfx1100.bc -o - | FileCheck --check-prefixes=DEV,W32 %s \
// RUN:   --implicit-check-not="declare {{.*}}@_Z"

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

constexpr __host__ __device__ int width() {
#if !defined(__HIP_DEVICE_COMPILE__)
  return 16;
#elif defined(__GFX9__)
  return 64;
#else
  return 32;
#endif
}

template <int N> struct Holder {
  template <class T> __host__ __device__ static T get(T x) { return x * N; }
};

__host__ __device__ int use(int x) {
  constexpr int w = width();
  return Holder<w>::get(x);
}

__global__ void kernel(int *o) { o[0] = use(o[1]); }
int on_host(int x) { return use(x); }

// AST: FunctionDecl {{.*}} use 'int (int)' {{.*}}targets={host}{{$}}
// AST: FunctionDecl {{.*}} use 'int (int)' {{.*}}targets={gfx90a,gfx942}{{$}}
// AST: FunctionDecl {{.*}} use 'int (int)' {{.*}}targets={gfx1100}{{$}}
// AST-NOT: FunctionDecl {{.*}} use 'int (int)'

// HOST-LABEL: define {{.*}}@_Z3usei(
// HOST: shl nsw i32 %{{.*}}, 4

// DEV-LABEL: define {{.*}}@_Z3usei(
// W64: shl nsw i32 %{{.*}}, 6
// W32: shl nsw i32 %{{.*}}, 5
