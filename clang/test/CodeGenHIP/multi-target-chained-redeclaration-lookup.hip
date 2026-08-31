// A function template declared once before a definition whose body diverges
// per target. Widening gives each target its own copy of the definition, and
// every copy redeclares the shared forward declaration, so the copies form one
// redeclaration chain; the namespace's lookup table keeps only the most recent
// copy, which belongs to the last target parsed. Qualified lookup and ADL on
// behalf of any other target must walk the chain to that target's own copy
// rather than dropping the name. This follows rocThrust's sequential
// stable_sort_by_key ("static assertion failed due to requirement
// 'thrust::detail::depend_on_instantiation<...>::value': unimplemented").
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang -x hip --offload-arch=gfx90a -nogpulib -nogpuinc \
// RUN:   -fintegrated-hip-device-codegen -fsyntax-only -Xclang -verify %s
// RUN: %clang -x hip --offload-arch=gfx90a --offload-arch=gfx942 \
// RUN:   -nogpulib -nogpuinc -fintegrated-hip-device-codegen -fsyntax-only \
// RUN:   -Xclang -verify %s

// Each device target must instantiate its own copy of msk.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx942.bc \
// RUN:   -mllvm -multi-target-codegen \
// RUN:   -mllvm -multi-target-record \
// RUN:   -mllvm -parse-all-target-alternatives \
// RUN:   -mllvm -widen-target-alternatives \
// RUN:   -mllvm -allow-target-variant-decls \
// RUN:   -mllvm -reparse-divergent-users \
// RUN:   -mllvm -merge-equivalent-variants
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=GFX90A %s
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefix=GFX942 %s

// expected-no-diagnostics

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))
#define HD __host__ __device__

namespace sq {
template <class I> HD int msk(I);
template <class I> HD int msk(I i) {
#ifndef __HIP_DEVICE_COMPILE__
  return i + 1;
#elif defined(__gfx90a__)
  return i + 2;
#else
  return i + 3;
#endif
}
template <class I> HD int use(I i) { return sq::msk(i); }
} // namespace sq

template <class I> __device__ int dev(I i) { return sq::use(i); }
__global__ void kernel(int *p) { *p = dev(40); }

// GFX90A-LABEL: define {{.*}} @_ZN2sq3mskIiEEiT_(
// GFX90A: add nsw i32 %{{.*}}, 2
// GFX942-LABEL: define {{.*}} @_ZN2sq3mskIiEEiT_(
// GFX942: add nsw i32 %{{.*}}, 3

template <class T, bool B> struct dep { static const bool value = B; };
template <class D> struct base {};
template <class D> struct policy : base<D> {};

namespace gen {
template <class D, class I> HD void ssk(policy<D> &, I) {
  static_assert(dep<I, false>::value, "unimplemented");
}
} // namespace gen

namespace seq {
template <class D> struct seqpol : policy<D> {};
struct seq_t : seqpol<seq_t> {};
template <class D, class I> HD void ssk(seqpol<D> &, I);
template <class D, class I> HD void ssk(seqpol<D> &, I i) {
#ifndef __HIP_DEVICE_COMPILE__
  (void)i;
#else
  (void)(i + 1);
#endif
}
} // namespace seq

template <class D> HD D &derived(const base<D> &b) { return (D &)b; }
template <class D, class I> HD void ssk(const base<D> &exec, I i) {
  using gen::ssk;
  ssk(derived(exec), i);
}
void sort_seq() {
  seq::seq_t s;
  ssk(s, 1);
}
