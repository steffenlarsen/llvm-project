// A kernel launched over a lambda of host code, and the lambda's call
// operator, are every device's, even where that host code is the host's own
// specialization, e.g. one evaluating what differs between host and device. A
// generic lambda called by the kernel with a class the device instantiated
// takes that class as it is.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -emit-llvm -o %t.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx942" \
// RUN:   -multi-target-aux-output=3:%t.gfx942.bc
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=DEV %s \
// RUN:   --implicit-check-not="declare {{.*}}@_ZZ4scan"
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck --check-prefix=DEV %s \
// RUN:   --implicit-check-not="declare {{.*}}@_ZZ4scan"

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

constexpr __host__ __device__ int wave() {
#ifdef __HIP_DEVICE_COMPILE__
  return 64;
#else
  return 0;
#endif
}

template <int W = wave()> struct Config {
  static constexpr int Size = W;
};

template <class Kernel> __global__ void trampoline(Kernel K) { K(Config<>{}); }

template <class Kernel> void launch(Kernel K) {
  void (*F)(Kernel) = trampoline<Kernel>;
  (void)F;
}

template <class T> void scan(T *Out) {
  constexpr int Wave = wave();
  launch([=](auto C) { Out[0] = decltype(C)::Size + Wave; });
}

void host(int *Out) { scan(Out); }

// DEV-LABEL: define {{.*}}amdgpu_kernel void @_Z10trampolineIZ4scanIiEvPT_EUlS1_E_EvS1_(
// DEV: call void @_ZZ4scanIiEvPT_ENKUlS0_E_clI6ConfigILi64EEEEDaS0_(
// DEV-LABEL: define {{.*}} void @_ZZ4scanIiEvPT_ENKUlS0_E_clI6ConfigILi64EEEEDaS0_(
// DEV: store i32 128,
