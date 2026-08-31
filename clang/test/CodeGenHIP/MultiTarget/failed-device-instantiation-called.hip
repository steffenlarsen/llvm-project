// Device code is instantiated before it is known which devices compile it,
// and a device it does not compile for keeps what that reported. A device
// whose code calls such an instantiation reports it, as its own compilation
// does, even where it has no definition to emit for it.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -emit-llvm -o /dev/null %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a" \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1100" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc -multi-target-aux-output=3:%t.gfx1100.bc \
// RUN:   2>&1 | FileCheck --implicit-check-not=error: %s
// The devices' own compilations agree.
// RUN: not %clang_cc1 %{hip_device} %{host_aux_triple} -target-cpu gfx90a \
// RUN:   -emit-llvm -o /dev/null %s 2>&1 | FileCheck --implicit-check-not=error: %s
// RUN: %clang_cc1 %{hip_device} %{host_aux_triple} -target-cpu gfx1100 \
// RUN:   -emit-llvm -o - %s | FileCheck --check-prefix=GFX1100 %s

#define __device__ __attribute__((device))

// Only gfx90a has the template, and only it instantiates it.
#if defined(__gfx90a__)
template <class T> __device__ int only_gfx90a(T x) { return T::nope; }
// CHECK-DAG: failed-device-instantiation-called.hip:[[@LINE-1]]:61: error: type 'int' cannot be used prior to '::' because it has no members
__attribute__((device, used)) int use_only() { return only_gfx90a(1); }
#endif

// Every device has the template, with a body of its own.
template <class T> __device__ int per_device(T x) {
#if defined(__gfx90a__)
  return T::nope;
  // CHECK-DAG: failed-device-instantiation-called.hip:[[@LINE-1]]:10: error: type 'int' cannot be used prior to '::' because it has no members
#else
  return x;
#endif
}
__attribute__((device, used)) int use_per_device() { return per_device(1); }

// CHECK: 2 errors generated when compiling for gfx90a.

// GFX1100-LABEL: define {{.*}} @_Z10per_deviceIiEiT_(
// GFX1100: ret i32
