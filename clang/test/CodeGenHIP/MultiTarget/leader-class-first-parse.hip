// The first parse of shared code gives its leader's answers, here the host's.
// A declaration whose analysis consumed a value that differs between targets
// stands for every target that agrees with the leader, and only the others are
// parsed again. gfx90a agrees with the host, so the first parse is its too, and
// only gfx1100 gets a copy of its own.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -fsyntax-only %s -ast-dump -ast-dump-filter use \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx90a" \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx1100" \
// RUN:   | FileCheck --check-prefix=AST %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O1 -x hip -emit-llvm -o - %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -O1 -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -O1 -target-cpu gfx1100" \
// RUN:   -multi-target-aux-output=3:%t.gfx1100.bc \
// RUN:   | FileCheck --check-prefix=W64 %s
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=W64 %s
// RUN: llvm-dis %t.gfx1100.bc -o - | FileCheck --check-prefix=W32 %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))

constexpr __host__ __device__ int width() {
#if defined(__HIP_DEVICE_COMPILE__) && !defined(__GFX9__)
  return 32;
#else
  return 64;
#endif
}

__host__ __device__ int use(int x) {
  constexpr int w = width();
  return x * w;
}

// AST: FunctionDecl {{.*}} use 'int (int)' {{.*}}targets={host,gfx90a}
// AST: FunctionDecl {{.*}} use 'int (int)' {{.*}}targets={gfx1100}
// AST-NOT: FunctionDecl {{.*}} use 'int (int)'

// W64-LABEL: define {{.*}}@_Z3usei(
// W64: shl nsw i32 %{{.*}}, 6
// W32-LABEL: define {{.*}}@_Z3usei(
// W32: shl nsw i32 %{{.*}}, 5
