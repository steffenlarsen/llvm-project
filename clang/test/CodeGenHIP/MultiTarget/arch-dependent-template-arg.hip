// A constant defined differently per device architecture, in the arms of an
// #if, selects code per target: rocWMMA picks its MFMA implementation with
// `GfxTargetId = Constants::AMDGCN_CURRENT_ARCH_ID` as a default template
// argument and recognises an implementation by matching it against a partial
// specialization that leaves that argument defaulted. Each target must see its
// own constant there, in the partial specialization's arguments as much as
// where the template is used, and code compiled once for every target that
// reads such a constant must read each target's own.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx942" \
// RUN:   -multi-target-aux-output=3:%t.gfx942.bc
// RUN: llvm-dis -o %t.gfx90a.ll %t.gfx90a.bc
// RUN: llvm-dis -o %t.gfx942.ll %t.gfx942.bc
// RUN: FileCheck -check-prefix=GFX90A %s < %t.gfx90a.ll \
// RUN:   --implicit-check-not="Lj2370E" --implicit-check-not="Lj0E"
// RUN: FileCheck -check-prefix=GFX942 %s < %t.gfx942.ll \
// RUN:   --implicit-check-not="Lj2314E" --implicit-check-not="Lj0E"
// RUN: FileCheck -check-prefix=VALUE90A %s < %t.gfx90a.ll
// RUN: FileCheck -check-prefix=VALUE942 %s < %t.gfx942.ll

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

namespace std {
template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { using type = T; };
template <bool B, class T = void>
using enable_if_t = typename enable_if<B, T>::type;
} // namespace std

namespace rw {
struct Constants {
  static constexpr unsigned ID_GFX90A = 0x90a;
  static constexpr unsigned ID_GFX942 = 0x942;
  static constexpr unsigned ID_NONE = 0;
#if defined(__gfx90a__)
  static constexpr unsigned CURRENT_ARCH_ID = ID_GFX90A;
#elif defined(__gfx942__)
  static constexpr unsigned CURRENT_ARCH_ID = ID_GFX942;
#else
  static constexpr unsigned CURRENT_ARCH_ID = ID_NONE;
#endif
};

#if defined(__gfx90a__)
constexpr unsigned WAVE_ID = 90;
#elif defined(__gfx942__)
constexpr unsigned WAVE_ID = 942;
#else
constexpr unsigned WAVE_ID = 1;
#endif

enum Arch {
#if defined(__gfx90a__)
  ARCH = 9
#elif defined(__gfx942__)
  ARCH = 42
#else
  ARCH = 1
#endif
};

template <typename T, unsigned GfxTargetId = Constants::CURRENT_ARCH_ID,
          typename Enabler = void>
struct mfma {
  __device__ static int exec() { return 1; }
};
template <typename T, unsigned GfxTargetId>
struct mfma<T, GfxTargetId,
            std::enable_if_t<GfxTargetId == Constants::ID_GFX90A>> {
  __device__ static int exec() { return 900; }
};
template <typename T, unsigned GfxTargetId>
struct mfma<T, GfxTargetId,
            std::enable_if_t<GfxTargetId == Constants::ID_GFX942>> {
  __device__ static int exec() { return 942; }
};

template <typename Op> struct is_mfma {
  static constexpr bool value = false;
};
template <typename T> struct is_mfma<mfma<T>> {
  static constexpr bool value = true;
};
template <typename Op, typename = void> struct Traits;
template <typename Op> struct Traits<Op, std::enable_if_t<is_mfma<Op>::value>> {
  __device__ static int get() { return Op::exec(); }
};

template <typename F, typename V> __host__ __device__ int for_each(V v, F f) {
  return f(v);
}
template <typename Op> struct Mma {
  template <typename A> __device__ static int exec(A a) {
    return for_each(a, [](auto x) -> int {
      return x + Op::exec() + (int)Constants::CURRENT_ARCH_ID;
    });
  }
};
} // namespace rw

template <typename T> __device__ int by_default() {
  return rw::mfma<T>::exec();
}
__global__ void k_default(int *o) { o[0] = by_default<float>(); }
// GFX90A-LABEL: define {{.*}} @_Z10by_defaultIfEiv(
// GFX90A: call {{.*}} @_ZN2rw4mfmaIfLj2314EvE4execEv(
// GFX942-LABEL: define {{.*}} @_Z10by_defaultIfEiv(
// GFX942: call {{.*}} @_ZN2rw4mfmaIfLj2370EvE4execEv(

template <typename T> __device__ int by_traits() {
  return rw::Traits<rw::mfma<T>>::get();
}
__global__ void k_traits(int *o) { o[0] = by_traits<int>(); }
// GFX90A-LABEL: define {{.*}} @_ZN2rw6TraitsINS_4mfmaIiLj2314EvEEvE3getEv(
// GFX90A: call {{.*}} @_ZN2rw4mfmaIiLj2314EvE4execEv(
// GFX942-LABEL: define {{.*}} @_ZN2rw6TraitsINS_4mfmaIiLj2370EvEEvE3getEv(
// GFX942: call {{.*}} @_ZN2rw4mfmaIiLj2370EvE4execEv(

template <typename T> __device__ int deduced(rw::mfma<T>) {
  return rw::mfma<T>::exec();
}
template <typename T> __device__ int by_deduction() {
  return deduced(rw::mfma<T>{});
}
__global__ void k_deduction(int *o) { o[0] = by_deduction<short>(); }
// The signature names the defaulted argument, mangled with the target's value
// as the target's own compilation does.
// GFX90A-LABEL: define {{.*}} @_Z7deducedIsEiN2rw4mfmaIT_Lj2314EvEE(
// GFX90A: call {{.*}} @_ZN2rw4mfmaIsLj2314EvE4execEv(
// GFX942-LABEL: define {{.*}} @_Z7deducedIsEiN2rw4mfmaIT_Lj2370EvEE(
// GFX942: call {{.*}} @_ZN2rw4mfmaIsLj2370EvE4execEv(

// A generic lambda in a member of a specialization made for one target.
template <typename T> __device__ int by_lambda(T x) {
  return rw::Mma<rw::mfma<T>>::exec(x);
}
__global__ void k_lambda(int *o, long v) { o[0] = by_lambda(v); }
// GFX90A-LABEL: define {{.*}} @_ZZN2rw3MmaINS_4mfmaIlLj2314EvEEE4execIlEEiT_ENKUlS5_E_clIlEEiS5_(
// GFX90A: add {{.*}}, 2314
// GFX942-LABEL: define {{.*}} @_ZZN2rw3MmaINS_4mfmaIlLj2370EvEEE4execIlEEiT_ENKUlS5_E_clIlEEiS5_(
// GFX942: add {{.*}}, 2370

// A static member function overloaded on the side, whose device body differs
// per architecture (HeCBench attention-paged's Utils::get_warp_size).
struct Utils {
  static __host__ int get_warp_size() { return 1; }
  static __device__ constexpr int get_warp_size() {
#if defined(__gfx942__)
    return 64;
#else
    return 32;
#endif
  }
};
template <int B> __device__ int warp_groups() {
  constexpr int Groups = Utils::get_warp_size() / B;
  return Groups;
}
__global__ void k_warp(int *o) { o[0] = warp_groups<2>(); }
// VALUE90A-LABEL: define {{.*}} @_Z11warp_groupsILi2EEiv(
// VALUE90A: store i32 16,
// VALUE942-LABEL: define {{.*}} @_Z11warp_groupsILi2EEiv(
// VALUE942: store i32 32,

// Read by code compiled once for every target.
__global__ void k_values(int *o) {
  o[0] = rw::Constants::CURRENT_ARCH_ID;
  o[1] = rw::WAVE_ID;
  o[2] = rw::ARCH;
}
__host__ __device__ int hd_value() { return rw::WAVE_ID; }
// VALUE90A-LABEL: define {{.*}} @_Z8k_valuesPi(
// VALUE90A: store i32 2314,
// VALUE90A: store i32 90,
// VALUE90A: store i32 9,
// VALUE90A-LABEL: define {{.*}} @_Z8hd_valuev(
// VALUE90A: ret i32 90
// VALUE942-LABEL: define {{.*}} @_Z8k_valuesPi(
// VALUE942: store i32 2370,
// VALUE942: store i32 942,
// VALUE942: store i32 42,
// VALUE942-LABEL: define {{.*}} @_Z8hd_valuev(
// VALUE942: ret i32 942
