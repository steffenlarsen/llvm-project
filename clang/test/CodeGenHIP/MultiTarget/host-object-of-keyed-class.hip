// An object of a class each device architecture has a specialization of, here
// through what a member function template evaluates, made by host code and
// captured by a lambda a kernel calls. A device template it is passed to names
// the device's specialization in its body, so its parameter is viewed as that
// too, as the device's own compilation has only that one. Each device then
// evaluates its own wave size.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -O2 -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -O2 -target-cpu gfx1100" \
// RUN:   -multi-target-aux-output=3:%t.gfx1100.bc
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefixes=DEV,W64 %s \
// RUN:   --implicit-check-not="declare {{.*}}@_Z"
// RUN: llvm-dis %t.gfx1100.bc -o - | FileCheck --check-prefixes=DEV,W32 %s \
// RUN:   --implicit-check-not="declare {{.*}}@_Z"

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

constexpr __host__ __device__ int wave() {
#if !defined(__HIP_DEVICE_COMPILE__)
  return 0;
#elif defined(__GFX9__)
  return 64;
#else
  return 32;
#endif
}

template <class T> struct State {
  T *flags;
  template <class F> __device__ T scan(F f) { return f(flags[0]) + wave(); }
};

template <class S> struct Prefix {
  S &state;
  __device__ Prefix(S &s) : state(s) {}
  __device__ int get() { return state.scan([](int x) { return x; }); }
};

template <class S> __device__ void impl(S state, int *out) {
  Prefix<S> p(state);
  out[0] = p.get();
}

template <class Kernel> __global__ void trampoline(Kernel k) { k(0); }

template <class T> void run(T *out) {
  State<T> st{out};
  auto kern = [=](auto) { impl(st, out); };
  void (*f)(decltype(kern)) = trampoline<decltype(kern)>;
  (void)f;
}

void host(int *out) { run(out); }

// DEV-LABEL: define {{.*}}amdgpu_kernel void @_Z10trampoline
// W64: add nsw i32 %{{.*}}, 64
// W32: add nsw i32 %{{.*}}, 32
