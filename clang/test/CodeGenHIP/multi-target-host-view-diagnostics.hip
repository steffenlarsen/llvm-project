// The host compilation of the regular pipeline analyses device code too, as
// the host sees it, and reports what a host-only `#if` arm of a device
// template gets wrong. Device code here is instantiated for the devices only,
// so it is instantiated once more as the host sees it, for its diagnostics:
// reached from a kernel template's explicit instantiation or launch, from
// non-template device code, and through a member of a class template. A
// value that differs between the host and the devices is the host's own
// there, so no error is reported for what the host does not instantiate.
//
// REQUIRES: amdgpu-registered-target

// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -fsyntax-only %s -DERRORS \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip -DERRORS" \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip -DERRORS" \
// RUN: 2>&1 | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -fsyntax-only %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip"

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

struct dim3 {
  unsigned x, y, z;
  __host__ __device__ dim3(unsigned x = 1, unsigned y = 1, unsigned z = 1)
      : x(x), y(y), z(z) {}
};
typedef struct ihipStream_t *hipStream_t;
extern "C" int hipConfigureCall(dim3, dim3, unsigned long = 0,
                                hipStream_t = 0);

template <unsigned W, int Tag> __device__ int narrow() {
#if defined(__HIP_DEVICE_COMPILE__)
  return W;
#else
  static_assert(W <= 32, "host arm");
  return Tag;
#endif
}

template <class T, unsigned W> struct dpp {
  __device__ int reduce(T x) {
#if defined(__HIP_DEVICE_COMPILE__)
    return x;
#else
    static_assert(W <= 32, "member host arm");
    return 0;
#endif
  }
};

#if defined(__HIP_DEVICE_COMPILE__)
constexpr unsigned WARP = 64;
#else
constexpr unsigned WARP = 32;
#endif

#ifdef ERRORS
template <class T> __global__ void explicit_kernel(T *o) { *o = narrow<64, 1>(); }
template __global__ void explicit_kernel<int>(int *);
// CHECK: error: static assertion failed due to requirement '64U <= 32': host arm
// CHECK: note: in instantiation of function template specialization 'narrow<64U, 1>' requested here

__device__ int from_device_code() { return narrow<64, 2>(); }
// CHECK: error: static assertion failed due to requirement '64U <= 32': host arm
// CHECK: note: in instantiation of function template specialization 'narrow<64U, 2>' requested here

template <class T> __global__ void launched_kernel(T *o) { *o = narrow<64, 3>(); }
void launch(int *o) { launched_kernel<<<1, 1>>>(o); }
// CHECK: error: static assertion failed due to requirement '64U <= 32': host arm
// CHECK: note: in instantiation of function template specialization 'narrow<64U, 3>' requested here
// CHECK: note: in instantiation of function template specialization 'launched_kernel<int>' requested here

__global__ void member(int *o) { *o = dpp<int, 64>().reduce(*o); }
// CHECK: error: static assertion failed due to requirement '64U <= 32': member host arm
// CHECK: note: in instantiation of member function 'dpp<int, 64>::reduce' requested here
// CHECK: 4 errors generated when compiling for host.
#endif

// A static member of a class specialization named by host code, instantiated
// for the devices first, is still the host's member.
constexpr int arch() {
#if defined(__gfx942__)
  return 942;
#elif defined(__HIP_DEVICE_COMPILE__)
  return 90;
#else
  return 1;
#endif
}
template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { using type = T; };
template <class T, bool Sleep = false, bool Small = (sizeof(T) <= 15)>
struct state;
template <class T, bool Sleep> struct state<T, Sleep, true> {
  static constexpr bool use_sleep = Sleep;
  static constexpr int arch_id = arch();
  __device__ int id() {
#if defined(__gfx942__)
    return 942;
#elif defined(__HIP_DEVICE_COMPILE__)
    return 90;
#else
    return 1;
#endif
  }
};
template <class S> constexpr bool runnable() {
  return arch() == 908 || !S::use_sleep;
}
template <class S>
__device__ typename enable_if<runnable<S>(), int>::type run(S s) {
  return s.id();
}
template <class S>
__device__ typename enable_if<!runnable<S>(), int>::type run(S) {
  return 0;
}
template <class S> __global__ void state_kernel(S s, int *o) { *o = run(s); }
void launch_state(int *o) {
  state_kernel<<<1, 1>>>(state<int>{}, o);
  state_kernel<<<1, 1>>>(state<int, true>{}, o);
}

__device__ int host_value() { return narrow<WARP, 4>(); }
template <class T> __global__ void host_value_kernel(T *o) {
  *o = narrow<WARP, 5>() + dpp<int, WARP>().reduce(*o);
}
void launch_host_value(int *o) { host_value_kernel<<<1, 1>>>(o); }
