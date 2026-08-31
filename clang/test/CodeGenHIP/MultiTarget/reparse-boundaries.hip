// Declarations are analysed for each target where what they consume differs,
// wherever they start: in a namespace whose name is followed by an attribute,
// as libstdc++'s `namespace std _GLIBCXX_VISIBILITY(default)` is, after a
// braced initializer in parentheses, as in `decltype(T{})`, and first in a
// class template, whose member typedefs and static data members are then each
// target's own.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a -O2" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx942 -O2" \
// RUN:   -multi-target-aux-output=3:%t.gfx942.bc
// RUN: FileCheck --check-prefix=HOST --input-file=%t.host.ll %s
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefixes=CHECK,GFX90A %s
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck --check-prefixes=CHECK,GFX942 %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

namespace N __attribute__((visibility("default"))) {
template <int S> struct I { static constexpr int v = S; };
constexpr int ld_size() { return I<sizeof(long double)>::v; }
}
// HOST-LABEL: define {{.*}}@host_ld_size(
// HOST: ret i32 16
extern "C" int host_ld_size() { return N::ld_size(); }
// CHECK-LABEL: define {{.*}}@device_ld_size(
// CHECK: store i32 8,
extern "C" __global__ void device_ld_size(int *p) { *p = N::ld_size(); }

#if defined(__gfx942__)
constexpr int W = 64;
__device__ int lane() { return 1; }
#else
constexpr int W = 32;
__device__ int lane() { return 2; }
#endif

struct Tag {};
__device__ auto braced_in_parens(int) -> decltype(Tag{}, int{}) {
  return N::I<W>::v;
}
// CHECK-LABEL: define {{.*}}@after_braced_initializer(
// GFX90A: store i32 32,
// GFX942: store i32 64,
extern "C" __global__ void after_braced_initializer(int *p) {
  *p = braced_in_parens(0);
}
template <class T> struct Tr {
  using arr = int[W];
  static constexpr int w = W;
  __device__ int f() { return W + lane(); }
  __device__ static int g() { return sizeof(arr); }
};
// 32 + 34 + 128 and 64 + 65 + 256.
// CHECK-LABEL: define {{.*}}@class_template_members(
// GFX90A: store i32 194,
// GFX942: store i32 385,
extern "C" __global__ void class_template_members(int *p) {
  Tr<int> t;
  *p = Tr<int>::w + t.f() + Tr<int>::g();
}
