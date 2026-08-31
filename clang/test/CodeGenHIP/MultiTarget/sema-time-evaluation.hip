// What Sema evaluates and keeps is each target's own: an immediate
// invocation, an `if constexpr` condition calling a function each target has a
// copy of, a bit-field width or alignment a class's copies evaluate
// differently, and a floating literal's value. The devices lay out and compute
// `long double` as `double`, the host does not.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++20 -target-cpu gfx90a -O2" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++20 -target-cpu gfx942 -O2" \
// RUN:   -multi-target-aux-output=3:%t.gfx942.bc
// RUN: FileCheck -check-prefix=HOST %s < %t.host.ll
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=DEV %s
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefix=DEV %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

consteval int ld_size_ce() { return sizeof(long double); }
constexpr int ld_size() { return sizeof(long double); }

// DEV-LABEL: define {{.*}} i32 @_Z9immediatev(
// DEV: ret i32 8
__device__ int immediate() { return ld_size_ce(); }

// DEV-LABEL: define {{.*}} i32 @_Z12if_constexprv(
// DEV: ret i32 8
__device__ int if_constexpr() {
  if constexpr (ld_size() == 8)
    return 8;
  else
    return 16;
}

struct Bits {
  unsigned f : sizeof(long double);
};
// DEV-LABEL: define {{.*}} i32 @_Z9bit_widthv(
// DEV: ret i32 255
__device__ unsigned bit_width() {
  Bits b{};
  b.f = ~0u;
  return b.f;
}

struct alignas(alignof(long double)) Aligned {
  char c;
};
// DEV-LABEL: define {{.*}} i32 @_Z12aligned_sizev(
// DEV: ret i32 8
__device__ int aligned_size() { return sizeof(Aligned); }

// A literal's value is in the format of the target it is parsed for.
// DEV-LABEL: define {{.*}} double @_Z7literalv(
// DEV: ret double 0.000000e+00
__device__ __attribute__((host)) double literal() {
  return (double)(1.1L - 1.1);
}

// HOST-LABEL: define {{.*}} i32 @_Z4hostv(
// HOST: ret i32 16
int host() { return ld_size_ce() + sizeof(Aligned) - 16; }
