// Whether a template's instantiations differ between targets depends on
// definitions the host-primary Sema may only see after the template is first
// used: a class template's members defined outside the class, and function
// templates declared before their definitions. A member defined outside the
// class counts as much as one defined inside it. A specialization made before
// such a definition was seen is shared by every target at first; once the
// definition shows that targets differ, it is kept for the devices (or the
// host, for host-only code) and every other target that needs one gets its
// own, using the same members.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang -x hip --offload-arch=gfx90a --offload-arch=gfx942 \
// RUN:   -nogpulib -nogpuinc -fintegrated-hip-device-codegen -fsyntax-only \
// RUN:   -Xclang -verify %s

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx942.bc \
// RUN:   -mllvm -multi-target-codegen \
// RUN:   -mllvm -multi-target-record \
// RUN:   -mllvm -parse-all-target-alternatives \
// RUN:   -mllvm -widen-target-alternatives \
// RUN:   -mllvm -allow-target-variant-decls \
// RUN:   -mllvm -reparse-divergent-users \
// RUN:   -mllvm -merge-equivalent-variants
// RUN: FileCheck -check-prefix=HOST %s < %t.host.ll \
// RUN:   --implicit-check-not="@_ZN3tagILi2064EE" \
// RUN:   --implicit-check-not="@_ZN3tagILi4064EE"
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefixes=DEV,GFX90A %s \
// RUN:   --implicit-check-not="@_ZN3tagILi2032EE" \
// RUN:   --implicit-check-not="@_ZN3tagILi4032EE" \
// RUN:   --implicit-check-not="@_ZN3tagILi1942EE" \
// RUN:   --implicit-check-not="@_ZN3tagILi3942EE" \
// RUN:   --implicit-check-not="@_ZN3tagILi5942EE" \
// RUN:   --implicit-check-not="declare {{.*}}@_Z3fwdIiEiv" \
// RUN:   --implicit-check-not="declare {{.*}}@_ZN5laterIiE1hEv"
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefixes=DEV,GFX942 %s \
// RUN:   --implicit-check-not="@_ZN3tagILi2032EE" \
// RUN:   --implicit-check-not="@_ZN3tagILi4032EE" \
// RUN:   --implicit-check-not="@_ZN3tagILi1090EE" \
// RUN:   --implicit-check-not="@_ZN3tagILi3090EE" \
// RUN:   --implicit-check-not="@_ZN3tagILi5090EE" \
// RUN:   --implicit-check-not="declare {{.*}}@_Z3fwdIiEiv" \
// RUN:   --implicit-check-not="declare {{.*}}@_ZN5laterIiE1hEv"

// expected-no-diagnostics

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

constexpr int arch() {
#if defined(__gfx90a__)
  return 90;
#elif defined(__gfx942__)
  return 942;
#else
  return 1;
#endif
}

__host__ __device__ constexpr int side() {
#if defined(__HIP_DEVICE_COMPILE__)
  return 64;
#else
  return 32;
#endif
}

template <int N> struct tag {
  __host__ __device__ static int get() { return N; }
};

// A member defined outside the class, before the class is used.
template <class T> struct before {
  __device__ int f();
};
template <class T> __device__ int before<T>::f() {
  return tag<arch() + 1000>::get();
}
template <class T> __global__ void kb(int *o) { o[0] = before<T>().f(); }
template __global__ void kb<int>(int *);

// A member defined outside the class, after the class is used on the device.
template <class T> struct after {
  __host__ __device__ int g();
};
template <class T> __global__ void ka(int *o) { o[0] = after<T>().g(); }
template __global__ void ka<int>(int *);
template <class T> __host__ __device__ int after<T>::g() {
  return tag<side() + 2000>::get();
}
int host_after() { return after<int>().g(); }

// A device function template used before its definition.
template <class T> __device__ int fwd();
template <class T> __global__ void kf(int *o) { o[0] = fwd<T>(); }
template __global__ void kf<int>(int *);
template <class T> __device__ int fwd() { return tag<arch() + 3000>::get(); }

// A host-device function template used by both sides before its definition.
template <class T> __host__ __device__ int hfwd();
template <class T> __global__ void kh(int *o) { o[0] = hfwd<T>(); }
template __global__ void kh<int>(int *);
int host_hfwd() { return hfwd<int>(); }
template <class T> __host__ __device__ int hfwd() {
  return tag<side() + 4000>::get();
}

// A class used before a member that depends on the architecture is defined.
template <class T> struct later {
  __device__ int h();
};
template <class T> __global__ void kl(int *o) { o[0] = later<T>().h(); }
template __global__ void kl<int>(int *);
template <class T> __device__ int later<T>::h() {
  return tag<arch() + 5000>::get();
}

// HOST-DAG: define {{.*}} @_ZN3tagILi2032EE3getEv(
// HOST-DAG: define {{.*}} @_ZN3tagILi4032EE3getEv(

// DEV-DAG: define {{.*}} @_ZN3tagILi2064EE3getEv(
// DEV-DAG: define {{.*}} @_ZN3tagILi4064EE3getEv(
// GFX90A-DAG: define {{.*}} @_ZN3tagILi1090EE3getEv(
// GFX90A-DAG: define {{.*}} @_ZN3tagILi3090EE3getEv(
// GFX90A-DAG: define {{.*}} @_ZN3tagILi5090EE3getEv(
// GFX942-DAG: define {{.*}} @_ZN3tagILi1942EE3getEv(
// GFX942-DAG: define {{.*}} @_ZN3tagILi3942EE3getEv(
// GFX942-DAG: define {{.*}} @_ZN3tagILi5942EE3getEv(
