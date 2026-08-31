// Merging declarations that every target parsed alike keys them by their
// printed names. A partial specialization whose arguments name its own
// template parameter, here through a variable template brought in by a
// using-declaration (rocWMMA's enable_gfx11_t), must print without naming that
// parameter by the specialization it belongs to, which never terminates.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx942" \
// RUN:   -multi-target-aux-output=3:%t.gfx942.bc
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=GFX90A %s
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefix=GFX942 %s

#define __global__ __attribute__((global))

namespace rw {
namespace detail {
template <class T, T N, T... Ns> struct contains_number {
  static constexpr bool value = ((N == Ns) || ...);
};
template <class T, T N, T... Ns>
static constexpr bool contains_number_v = contains_number<T, N, Ns...>::value;
} // namespace detail
using detail::contains_number_v;

template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { using type = T; };
template <bool B, class T = void>
using enable_if_t = typename enable_if<B, T>::type;

template <unsigned G, bool Cond = true>
using enable_some_t = enable_if_t<contains_number_v<unsigned, G, 1u, 2u> && Cond>;

template <class T, unsigned G, class E = void> struct W {
  static constexpr int v = 0;
};
template <unsigned G> struct W<int, G, enable_some_t<G>> {
#if defined(__gfx942__)
  static constexpr int v = 1;
#else
  static constexpr int v = 2;
#endif
};
} // namespace rw

// GFX90A: store i32 2,
// GFX942: store i32 1,
__global__ void kernel(int *out) { *out = rw::W<int, 1>::v; }
