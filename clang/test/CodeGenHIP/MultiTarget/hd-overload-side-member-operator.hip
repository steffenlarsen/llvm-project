// Member and operator calls in host-device functions whose callee the host and
// the devices pick differently. Where the picks have different signatures, the
// function is analysed for each side, and each side calls its own pick. Where
// they are one symbol, which each side defines for itself, it stays shared.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -O1 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++20 -O1 -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   -mllvm -multi-target-audit=%t.audit
// RUN: FileCheck --check-prefix=HOST %s < %t.host.ll
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=DEV %s
// The device's own compilation agrees.
// RUN: %clang_cc1 -std=c++20 %{hip_device} %{host_aux_triple} -O1 \
// RUN:   -target-cpu gfx90a -emit-llvm -o - %s | FileCheck --check-prefix=DEV %s
// RUN: FileCheck --check-prefix=AUDIT %s < %t.audit

#define __device__ __attribute__((device))
#define __host__ __attribute__((host))

// The picks differ in signature.
struct S {
  __host__ int g(int x) const { return x + 1; }
  __device__ int g(long x) const { return x + 2; }
};
struct T { int v; };
__host__ int operator+(T a, int b) { return a.v + b + 10; }
__device__ int operator+(T a, long b) { return a.v + b + 20; }
__host__ __device__ int hd_member(int x) { S s; return s.g(x); }
__host__ __device__ int hd_operator(int x) { T t{1}; return t + x; }
// HOST-LABEL: define {{.*}} @_Z9hd_memberi(
// HOST: add {{.*}}i32 %x, 1{{$}}
// HOST-LABEL: define {{.*}} @_Z11hd_operatori(
// HOST: add {{.*}}i32 %x, 11{{$}}
// DEV-LABEL: define {{.*}} @_Z9hd_memberi(
// DEV: add {{.*}}i32 %x, 2{{$}}
// DEV-LABEL: define {{.*}} @_Z11hd_operatori(
// DEV: add {{.*}}i32 %x, 21{{$}}

// The picks are one symbol.
struct U {
  __host__ int g() const { return 1; }
  __device__ int g() const { return 2; }
};
__host__ __device__ int hd_same_member() { U u; return u.g(); }
// HOST-LABEL: define {{.*}} @_Z14hd_same_memberv(
// HOST: ret i32 1
// DEV-LABEL: define {{.*}} @_Z14hd_same_memberv(
// DEV: ret i32 2
// AUDIT: diag parse shared hd_same_member @ {{.*}}: R:U::g(side)

__attribute__((device, used)) int use(int x) {
  return hd_member(x) + hd_operator(x) + hd_same_member();
}
int host_use(int x) { return hd_member(x) + hd_operator(x) + hd_same_member(); }
