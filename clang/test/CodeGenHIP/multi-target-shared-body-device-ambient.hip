// A shared host-callable specialization is instantiated once for every
// target: its body binds the host's entities, and each device target gets a
// redirect. When it is first reached while a device target's copy of a
// divergent caller is being instantiated, that device target is ambient and
// its body binds that target's entities instead, so the host needs redirects
// of its own. Likewise, a static data member of an ordinary class template
// first instantiated under a device ambient must stay shared. This follows
// rocThrust's undefined host symbols ("undefined reference to
// `thrust::detail::no_throw_allocator<...>::deallocate(...)'", "undefined
// reference to `rocprim::detail::wrapped_reduce_config<...>::
// architecture_config<...>::params'").
//
// REQUIRES: amdgpu-registered-target

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
// RUN: FileCheck -check-prefix=HOST %s < %t.host.ll
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=GFX90A %s
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefix=GFX942 %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))
#define HD __host__ __device__

// A dependent call to a divergent function template.
namespace hr {
template <class D> struct pol {};
struct tag : pol<tag> {};
template <class D> HD int sync(pol<D> &) {
#ifndef __HIP_DEVICE_COMPILE__
  return 4;
#elif defined(__gfx90a__)
  return 5;
#else
  return 6;
#endif
}
template <class D> HD int sync_opt(pol<D> &p) { return sync(p); }
} // namespace hr

template <class D> HD int call_outer(hr::pol<D> &p) {
#ifdef __HIP_DEVICE_COMPILE__
  return hr::sync_opt(p) + 100;
#else
  return hr::sync_opt(p);
#endif
}

// A call to a member function of a class template forked per target.
template <class T> struct alloc {
  HD int dealloc(T) {
#ifndef __HIP_DEVICE_COMPILE__
    return 4;
#elif defined(__gfx90a__)
    return 5;
#else
    return 6;
#endif
  }
};
template <class T> HD int release(T t) {
  alloc<T> a;
  return a.dealloc(t);
}
template <class T> HD int member_outer(T t) {
#ifdef __HIP_DEVICE_COMPILE__
  return release(t) + 100;
#else
  return release(t);
#endif
}

// A value member of a class template forked per target, read from host code
// that device code names.
constexpr int width() {
#ifndef __HIP_DEVICE_COMPILE__
  return 4;
#elif defined(__gfx90a__)
  return 5;
#else
  return 6;
#endif
}
template <class T> struct wcfg { static constexpr int val = width(); };
template <class T> int host_read(T) { return wcfg<T>::val; }

// A static data member of an ordinary class template, odr-used by host code
// that device code names.
struct params_t { int block, items; };
constexpr params_t pick(int a) { return {a * 10, a}; }
template <class T> struct cfg {
  template <int A> struct arch { static constexpr params_t params = pick(A); };
};
template <class C> params_t dispatch(int a) {
  switch (a) {
  case 1:
    return C::template arch<1>::params;
  default:
    return C::template arch<2>::params;
  }
}

template <class T> HD int value_use(T t) {
#ifndef __HIP_DEVICE_COMPILE__
  return host_read(t) + dispatch<cfg<T>>(t).block;
#else
  (void)&host_read<T>;
  (void)&dispatch<cfg<T>>;
  return t;
#endif
}
template <class T> HD int value_outer(T t) {
#ifdef __HIP_DEVICE_COMPILE__
  return value_use(t) + 100;
#else
  return value_use(t);
#endif
}

__global__ void kernel(int *o) {
  hr::tag t;
  o[0] = call_outer(t);
  o[1] = member_outer(1);
  o[2] = value_outer(1);
}
int host_use() {
  hr::tag t;
  return hr::sync_opt(t) + release(1) + value_use(1);
}

// HOST-DAG: @_ZN3cfgIiE4archILi1EE6paramsE = linkonce_odr {{.*}}constant %struct.params_t { i32 10, i32 1 }
// HOST-DAG: @_ZN3cfgIiE4archILi2EE6paramsE = linkonce_odr {{.*}}constant %struct.params_t { i32 20, i32 2 }

// HOST-LABEL: define {{.*}} @_ZN2hr4syncINS_3tagEEEiRNS_3polIT_EE(
// HOST: ret i32 4
// HOST-LABEL: define {{.*}} @_ZN5allocIiE7deallocEi(
// HOST: ret i32 4
// HOST-LABEL: define {{.*}} @_Z9host_readIiEiT_(
// HOST: ret i32 4

// GFX90A-LABEL: define {{.*}} @_ZN2hr4syncINS_3tagEEEiRNS_3polIT_EE(
// GFX90A: ret i32 5
// GFX90A-LABEL: define {{.*}} @_ZN5allocIiE7deallocEi(
// GFX90A: ret i32 5
// GFX942-LABEL: define {{.*}} @_ZN2hr4syncINS_3tagEEEiRNS_3polIT_EE(
// GFX942: ret i32 6
// GFX942-LABEL: define {{.*}} @_ZN5allocIiE7deallocEi(
// GFX942: ret i32 6
