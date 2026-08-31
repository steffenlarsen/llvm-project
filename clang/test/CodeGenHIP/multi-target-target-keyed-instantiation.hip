// The host-primary Sema analyses a template once, under the host, but a
// constant the template can evaluate may differ between the host and the
// devices, or between device architectures (e.g. rocPRIM's
// device_target_arch() and wavefront::get_target() as template arguments).
// A specialization of such a template is instantiated once per target that
// tells it apart: once for the host and once for the devices, or once per
// architecture, and each target compiles its own. A class specialization
// named by host code and passed into device code (e.g. a kernel's traits
// class) stands for the device's specialization there, and an explicit
// specialization stays every target's.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang -x hip --offload-arch=gfx90a -nogpulib -nogpuinc \
// RUN:   -fintegrated-hip-device-codegen -fsyntax-only -Xclang -verify %s
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
// RUN:   --implicit-check-not="@_ZN3tagILi90EE" \
// RUN:   --implicit-check-not="@_ZN3tagILi942EE" \
// RUN:   --implicit-check-not="@_ZN3tagILi64EE" \
// RUN:   --implicit-check-not="@_ZN3tagILi68EE" \
// RUN:   --implicit-check-not="@_ZN3tagILi72EE"
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefixes=DEV,GFX90A %s \
// RUN:   --implicit-check-not="@_ZN3tagILi942EE" \
// RUN:   --implicit-check-not="@_ZN3tagILi1EE" \
// RUN:   --implicit-check-not="@_ZN3tagILi32EE" \
// RUN:   --implicit-check-not="@_ZN3tagILi36EE" \
// RUN:   --implicit-check-not="@_ZN3tagILi34EE" \
// RUN:   --implicit-check-not="@_ZN3tagILi40EE"
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefixes=DEV,GFX942 %s \
// RUN:   --implicit-check-not="@_ZN3tagILi90EE" \
// RUN:   --implicit-check-not="@_ZN3tagILi1EE" \
// RUN:   --implicit-check-not="@_ZN3tagILi32EE" \
// RUN:   --implicit-check-not="@_ZN3tagILi36EE" \
// RUN:   --implicit-check-not="@_ZN3tagILi34EE" \
// RUN:   --implicit-check-not="@_ZN3tagILi40EE"

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

template <class T> __device__ int by_arch() { return tag<arch()>::get(); }

template <int N = side()> __host__ __device__ int by_side() {
  return tag<N>::get();
}

template <int N> struct traits {
  using t = tag<side() + N>;
};
template <class K> __global__ void by_traits(int *p) { p[0] = K::t::get(); }

template <class T> struct unless_specialized {
  __host__ __device__ static int get() { return tag<side() + sizeof(T)>::get(); }
};
template <> struct unless_specialized<char> {
  __host__ __device__ static int get() { return tag<7>::get(); }
};

template <class T> __host__ __device__ int nondependent() {
  return tag<side() + 8>::get();
}

template <class T> __host__ __device__ int local_side() {
  constexpr int v = side();
  return v;
}

__global__ void kernel(int *p) {
  p[0] = by_arch<int>() + by_side() + unless_specialized<int>::get() +
         unless_specialized<char>::get() + local_side<int>() +
         nondependent<int>();
}

int host_use() {
  return by_side() + traits<2>::t::get() + unless_specialized<int>::get() +
         unless_specialized<char>::get() + local_side<int>() +
         nondependent<int>();
}
template __global__ void by_traits<traits<2>>(int *);

// HOST-DAG: define {{.*}} @_Z7by_sideILi32EEiv(
// HOST-DAG: define {{.*}} @_ZN3tagILi32EE3getEv(
// HOST-DAG: define {{.*}} @_ZN3tagILi34EE3getEv(
// HOST-DAG: define {{.*}} @_ZN3tagILi36EE3getEv(
// HOST-DAG: define {{.*}} @_ZN3tagILi7EE3getEv(
// HOST-DAG: define {{.*}} @_ZN3tagILi40EE3getEv(
// HOST-DAG: define {{.*}} @_Z10local_sideIiEiv(
// HOST-DAG: store i32 32, ptr %v,

// DEV-DAG: define {{.*}} @_Z7by_sideILi64EEiv(
// DEV-DAG: define {{.*}} @_ZN3tagILi64EE3getEv(
// DEV-DAG: define {{.*}} @_ZN3tagILi66EE3getEv(
// DEV-DAG: define {{.*}} @_ZN3tagILi68EE3getEv(
// DEV-DAG: define {{.*}} @_ZN3tagILi7EE3getEv(
// DEV-DAG: define {{.*}} @_ZN3tagILi72EE3getEv(
// DEV-DAG: define {{.*}} @_Z9by_traitsI6traitsILi2EEEvPi(
// GFX90A-DAG: define {{.*}} @_ZN3tagILi90EE3getEv(
// GFX942-DAG: define {{.*}} @_ZN3tagILi942EE3getEv(
// DEV-DAG: define {{.*}} @_Z10local_sideIiEiv(
// DEV-DAG: store i32 64, ptr %v.ascast,
