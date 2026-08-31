// A call in a host-device function whose callee the host and the devices pick
// differently. The device's pick may have a deduced return type, which has to
// be deduced before its call is built. Where the two picks return different
// types, the code around the call means something different to each side, so
// the function is analysed for each, and the device's copy is built around
// the device's call.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -O1 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++20 -O1 -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc
// RUN: FileCheck --check-prefix=HOST %s < %t.host.ll
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=DEV %s
// The device's own compilation agrees.
// RUN: %clang_cc1 -std=c++20 %{hip_device} %{host_aux_triple} -O1 \
// RUN:   -target-cpu gfx90a -emit-llvm -o - %s | FileCheck --check-prefix=DEV %s

// With two devices, each calls the devices' pick.
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -O1 -x hip -emit-llvm -o %t.host3.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++20 -O1 -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t.3.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++20 -O1 -target-cpu gfx942" \
// RUN:   -multi-target-aux-output=3:%t.3.gfx942.bc
// RUN: FileCheck --check-prefix=HOST %s < %t.host3.ll
// RUN: llvm-dis %t.3.gfx90a.bc -o - | FileCheck --check-prefix=DEV %s
// RUN: llvm-dis %t.3.gfx942.bc -o - | FileCheck --check-prefix=DEV %s

// A device pick that cannot be instantiated is diagnosed as the device's own
// compilation diagnoses it.
// RUN: not %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -x hip -emit-llvm -o /dev/null %s \
// RUN:   -DBAD_DEVICE_PICK \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++20 -target-cpu gfx90a -DBAD_DEVICE_PICK" \
// RUN:   -multi-target-aux-output=2:%t.bad.bc 2>&1 | FileCheck --check-prefix=BAD %s

#define __device__ __attribute__((device))
#define __host__ __attribute__((host))

#ifndef BAD_DEVICE_PICK
// The device's pick has a deduced return type, the same as the host's.
int f(int x) { return x + 1; }
__device__ auto f(auto x) { return x + 2; }
__host__ __device__ int hd(int x) { return f(x); }
// HOST-LABEL: define {{.*}} @_Z2hdi(
// HOST: add nsw i32 %x, 1
// DEV-LABEL: define {{.*}} @_Z2hdi(
// DEV: add nsw i32 %x, 2

// The device's pick returns another type, deduced.
int g(int x) { return x + 1; }
__device__ auto g(auto x) { return x + 1.5; }
__host__ __device__ int hd_deduced(int x) { return g(x); }
// HOST-LABEL: define {{.*}} @_Z10hd_deducedi(
// HOST: add nsw i32 %x, 1
// DEV-LABEL: define {{.*}} @_Z10hd_deducedi(
// DEV: sitofp {{.*}} to double
// DEV: fadd {{.*}}double {{.*}}1.5
// DEV: fptosi double {{.*}} to i32

// The device's pick returns another type, as declared.
int h(int x) { return x + 1; }
__device__ double h(double x) { return x + 1.5; }
__host__ __device__ int hd_declared(int x) { return h(x); }
// HOST-LABEL: define {{.*}} @_Z11hd_declaredi(
// HOST: add nsw i32 %x, 1
// DEV-LABEL: define {{.*}} @_Z11hd_declaredi(
// DEV: sitofp {{.*}} to double
// DEV: fadd {{.*}}double {{.*}}1.5
// DEV: fptosi double {{.*}} to i32

__attribute__((device, used)) int use(int x) {
  return hd(x) + hd_deduced(x) + hd_declared(x);
}
#else
int f(int x) { return x + 1; }
__device__ auto f(auto x) { return x.member; }
__host__ __device__ int hd(int x) { return f(x); }
__attribute__((device, used)) int use(int x) { return hd(x); }
// BAD: error: member reference base type 'int' is not a structure or union
// BAD: note: in instantiation of function template specialization 'f<int>' requested here
// BAD: 1 error generated when compiling for gfx90a.
#endif
