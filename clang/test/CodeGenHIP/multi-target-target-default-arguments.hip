// A template whose default argument a macro gives each target a value of its
// own for, e.g. rocPRIM's `bool UseDPP = ROCPRIM_DETAIL_USE_DPP`, has the same
// body for every target and is merged, but each target keeps its own default.
// A member function template whose body differs between targets keeps the
// class it is in apart; the members that agree are not merged out of it, and
// an overload of one of them is not taken for another copy of it.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -emit-llvm -o %t.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx942.bc
// RUN: FileCheck --check-prefix=HOST %s < %t.ll
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefixes=DEV,GFX90A %s
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck --check-prefixes=DEV,GFX942 %s

// Optimized, the kernel folds to each target's values. (Instantiating the
// overloads of a member function template that differ between targets only
// in one of them took a different path, which crashed.)
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.o2.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.o2.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.o2.gfx942.bc
// RUN: llvm-dis %t.o2.gfx90a.bc -o - | FileCheck --check-prefix=O2-GFX90A %s
// RUN: llvm-dis %t.o2.gfx942.bc -o - | FileCheck --check-prefix=O2-GFX942 %s

// Parsed for each device, the arms with the same tokens are merged, and the
// device arm's defaults are kept for each of them as well.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.each.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.each.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.each.gfx942.bc \
// RUN: -mllvm -parse-identical-alternatives-once=false
// RUN: llvm-dis %t.each.gfx90a.bc -o - | FileCheck --check-prefix=O2-GFX90A %s
// RUN: llvm-dis %t.each.gfx942.bc -o - | FileCheck --check-prefix=O2-GFX942 %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#if defined(__HIP_DEVICE_COMPILE__)
#define USE_DPP 1
#define HAS_BCAST 1
#else
#define USE_DPP 0
#endif
#if defined(__gfx942__)
#define WAVE 2
#else
#define WAVE 1
#endif

template <class T, bool UseDPP = USE_DPP>
struct selected { static constexpr int v = UseDPP ? 1 : 2; };
template <class T, int W = WAVE>
struct wave { static constexpr int v = W * 10; };
template <bool U = USE_DPP> __host__ __device__ int fn() { return U ? 100 : 200; }

__host__ __device__ int by_class() { return selected<int>::v; }
// HOST-LABEL: define {{.*}} @_Z8by_classv(
// HOST: ret i32 2
// DEV-LABEL: define {{.*}} @_Z8by_classv(
// DEV: ret i32 1

__host__ __device__ int by_arch() { return wave<int>::v; }
// GFX90A-LABEL: define {{.*}} @_Z7by_archv(
// GFX90A: ret i32 10
// GFX942-LABEL: define {{.*}} @_Z7by_archv(
// GFX942: ret i32 20

__host__ __device__ int by_function() { return fn(); }
// HOST: call {{.*}} @_Z2fnILb0EEiv(
// DEV: call {{.*}} @_Z2fnILb1EEiv(

// rocPRIM's warp_reduce_crosslane picks warp_reduce_dpp by such a default.
template <bool B> struct bool_c {};
template <class T, unsigned W> struct dpp {
  template <class F> __device__ int impl(F f, bool_c<false>) {
#if HAS_BCAST
    return f(1);
#else
    static_assert(W <= 32, "no broadcast");
    return f(2);
#endif
  }
  template <class F> __device__ int impl(F f, bool_c<true>) { return f(4); }
  template <class F> __device__ int reduce(F f) {
    return impl(f, bool_c<(W < 32)>{});
  }
  template <class F> __device__ int reduce(F f, unsigned n) { return f(n); }
};
template <class T, unsigned W> struct shfl {
  template <class F> __device__ int reduce(F f) { return f(3); }
  template <class F> __device__ int reduce(F f, unsigned n) { return f(n); }
};
template <bool B, class X, class Y> struct cond { using type = X; };
template <class X, class Y> struct cond<false, X, Y> { using type = Y; };
template <class T, unsigned W, bool UseDPP = USE_DPP>
struct crosslane : cond<UseDPP, dpp<T, W>, shfl<T, W>>::type {};
struct plus { __device__ int operator()(int x) { return x * 10; } };
template <class T> __device__ int block_reduce() {
  return crosslane<T, 64>().reduce(plus{}) + crosslane<T, 16>().reduce(plus{}) +
         crosslane<T, 64>().reduce(plus{}, 7);
}
__global__ void k(int *o) {
  o[0] = by_class();
  o[1] = by_arch();
  o[2] = by_function();
  o[3] = block_reduce<int>();
}
// O2-GFX90A-LABEL: define {{.*}} @_Z1kPi(
// O2-GFX90A: store i32 1,
// O2-GFX90A: store i32 10,
// O2-GFX90A: store i32 100,
// O2-GFX90A: store i32 120,
// O2-GFX942-LABEL: define {{.*}} @_Z1kPi(
// O2-GFX942: store i32 1,
// O2-GFX942: store i32 20,
// O2-GFX942: store i32 100,
// O2-GFX942: store i32 120,
// DEV-DAG: define {{.*}} @_ZN3dppIiLj64EE4implI4plusEEiT_6bool_cILb0EE(
// DEV-DAG: call {{.*}} @_ZN4plusclEi({{.*}} i32 noundef 1)
// DEV-DAG: call {{.*}} @_ZN4plusclEi({{.*}} i32 noundef 4)
// DEV-DAG: define {{.*}} @_ZN3dppIiLj64EE6reduceI4plusEEiT_j(
