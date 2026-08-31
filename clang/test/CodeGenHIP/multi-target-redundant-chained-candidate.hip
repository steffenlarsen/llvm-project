// Function templates declared once and then defined in a widened region, so
// each target's copy of a definition redeclares the shared forward
// declaration. A candidate set frozen inside the same region names the copy
// belonging to the target being parsed; the region's merge then makes the
// copies of an identical definition redundant, keeping one as the shared
// copy. The lookup table holds only the most recent copy, which is redundant
// too, so an instantiation must find the shared copy in the redeclaration
// chain rather than dropping the candidate. This follows rocThrust's
// assign_value ("address of overloaded function 'copy' does not match
// required type").
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang -x hip --offload-arch=gfx90a -nogpulib -nogpuinc \
// RUN:   -fintegrated-hip-device-codegen -fsyntax-only -Xclang -verify %s
// RUN: %clang -x hip --offload-arch=gfx90a --offload-arch=gfx942 \
// RUN:   -nogpulib -nogpuinc -fintegrated-hip-device-codegen -fsyntax-only \
// RUN:   -Xclang -verify %s

// expected-no-diagnostics

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define HD __host__ __device__

namespace hr {
template <class D> struct pol {};
struct tag : pol<tag> {};
struct htag : pol<htag> {};
template <class A, class B> struct cross {
  cross<B, A> rotate() const { return {}; }
};
template <class S, class I, class O> HD O copy(pol<S> &, I, I, O);
template <class S1, class S2, class I, class O>
HD O copy(cross<S1, S2>, I, I, O);
} // namespace hr

namespace hr {
template <class S, class I, class O> HD O copy(pol<S> &, I f, I l, O r) {
#ifdef __HIP_DEVICE_COMPILE__
  return r + 1;
#else
  return r;
#endif
}
template <class S1, class S2, class I, class O>
HD O copy(cross<S1, S2>, I f, I l, O r) {
  return r;
}
template <class S1, class S2, class P1, class P2>
HD void assign(cross<S1, S2> &s, P1 d, P2 src) {
#ifndef __HIP_DEVICE_COMPILE__
  cross<S2, S1> r = s.rotate();
  hr::copy(r, src, src + 1, d);
#else
  P1 (*fp)(cross<S2, S1>, P2, P2, P1) = hr::copy;
  (void)fp;
  *d = *src;
#endif
}
} // namespace hr

template <class T> HD void get(T *d, T *s) {
  hr::cross<hr::htag, hr::tag> c;
  hr::assign(c, d, s);
}
void use(int *d, int *s) { get(d, s); }
