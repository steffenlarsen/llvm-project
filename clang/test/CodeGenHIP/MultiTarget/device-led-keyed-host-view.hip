// Device-only code a device leads names that device's specialization of a
// class whose definition differs between the host and the devices. The host
// parses it again, so its view of the code, e.g. a device template's host
// arm, names the host's specialization throughout.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -fsyntax-only -verify %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a"
// expected-no-diagnostics

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

template <int I, class T, int D = 0> struct tile {};
template <int I, class T> struct tile<I, T, 0> {
#if defined(__HIP_DEVICE_COMPILE__)
  T x[I];
#else
  T x[I + 1];
#endif
};

__device__ void mma(tile<16, int> &D, const tile<8, int> &A) { D.x[0] = A.x[0]; }

template <class T> __device__ void use(T *p) {
#if defined(__HIP_DEVICE_COMPILE__)
  tile<16, int> C;
  tile<8, int> A;
  mma(C, A);
  p[0] = C.x[0];
#else
  tile<16, int> C;
  tile<8, int> A;
  mma(C, A);
#endif
}

__global__ void k(int *p) { use(p); }
