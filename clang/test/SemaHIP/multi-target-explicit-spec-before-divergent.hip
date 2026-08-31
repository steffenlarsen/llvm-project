// An explicit specialization declared before the primary template's
// definition stays visible after the definition diverges by target, in layout
// too, which gives the template a specialization per target.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -fsyntax-only -verify %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip"

// expected-no-diagnostics

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))

#ifdef __HIP_DEVICE_COMPILE__
#define IF_ON_HOST(X)
#else
#define IF_ON_HOST(X) X
#endif

template <class A = void, class B = void> class R;

template <> class R<void, void> {
public:
  __host__ __device__ static int tracking_enabled() { return 0; }
};

template <class A, class B> class R : public R<A, void> {
public:
  static R *make() { IF_ON_HOST(return new R;) return nullptr; }
#ifdef __HIP_DEVICE_COMPILE__
  int pad;
#endif
};

template <class A> class R<A, void> : public R<void, void> {};

int use() { return R<int>::tracking_enabled(); }

// The same lookups from device code.
__device__ int dev() {
  return R<void, void>::tracking_enabled() + R<int>::tracking_enabled();
}
