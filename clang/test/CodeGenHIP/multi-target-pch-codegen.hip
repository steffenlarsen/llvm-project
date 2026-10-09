// A precompiled header written by a multi-target compilation keeps every
// target's view of what it contains: a declaration whose tokens differ between
// targets keeps its copy for each target, and each target's preprocessor
// resumes with that target's macros, and a partial specialization whose body
// differs between targets keeps each target's copy. A translation unit using
// it compiles as if it had included the header.
//
// REQUIRES: amdgpu-registered-target

// RUN: rm -rf %t && split-file %s %t
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip-header -emit-pch -o %t/prefix.pch \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-record -mllvm -parse-all-target-alternatives \
// RUN:   -mllvm -widen-target-alternatives -mllvm -allow-target-variant-decls \
// RUN:   -mllvm -reparse-divergent-users -mllvm -merge-equivalent-variants \
// RUN:   %t/prefix.h

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -emit-llvm -o %t/host.ll \
// RUN:   -include-pch %t/prefix.pch \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t/gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t/gfx942.bc \
// RUN:   -mllvm -multi-target-codegen -mllvm -multi-target-record \
// RUN:   -mllvm -parse-all-target-alternatives -mllvm -widen-target-alternatives \
// RUN:   -mllvm -allow-target-variant-decls -mllvm -reparse-divergent-users \
// RUN:   -mllvm -merge-equivalent-variants %t/use.hip
// RUN: FileCheck -check-prefix=HOST %s < %t/host.ll \
// RUN:   --implicit-check-not="@_ZN3tagILi1090EE3getEv" \
// RUN:   --implicit-check-not="@_ZN3tagILi1942EE3getEv"
// RUN: llvm-dis -o - %t/gfx90a.bc | FileCheck -check-prefixes=DEV,GFX90A %s \
// RUN:   --implicit-check-not="@_ZN3tagILi1001EE3getEv" \
// RUN:   --implicit-check-not="@_ZN3tagILi1942EE3getEv"
// RUN: llvm-dis -o - %t/gfx942.bc | FileCheck -check-prefixes=DEV,GFX942 %s \
// RUN:   --implicit-check-not="@_ZN3tagILi1001EE3getEv" \
// RUN:   --implicit-check-not="@_ZN3tagILi1090EE3getEv"

// HOST-DAG: define {{.*}} @_ZN3tagILi1001EE3getEv(
// HOST-DAG: add nsw i32 %call, 32
// HOST-DAG: define {{.*}} @_ZN8dispatchIi4LessIjEE4doitEv(
// HOST-DAG: ret i32 32

// DEV-DAG: store i32 64
// DEV-DAG: define {{.*}} @_ZN8dispatchIi4LessIjEE4doitEv(
// DEV-DAG: ret i32 64
// GFX90A-DAG: define {{.*}} @_ZN3tagILi1090EE3getEv(
// GFX942-DAG: define {{.*}} @_ZN3tagILi1942EE3getEv(

//--- prefix.h
#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#if defined(__HIP_DEVICE_COMPILE__)
#define PREFIX_SIDE 64
#else
#define PREFIX_SIDE 32
#endif

constexpr int arch() {
#if defined(__gfx90a__)
  return 90;
#elif defined(__gfx942__)
  return 942;
#else
  return 1;
#endif
}

template <int N> struct tag {
  __host__ __device__ static int get() { return N; }
};

template <class T> struct Less {};
template <class S, class C> struct dispatch;
template <class K> struct dispatch<int, Less<K>> {
  __host__ __device__ static int doit() { return PREFIX_SIDE; }
};
template <class S, class C> __host__ __device__ int sort_it() {
  return dispatch<S, C>::doit();
}

//--- use.hip
__global__ void k(int *o) {
  o[0] = tag<arch() + 1000>::get();
  o[1] = PREFIX_SIDE;
  o[2] = sort_it<int, Less<unsigned>>();
}
int host() {
  return tag<arch() + 1000>::get() + PREFIX_SIDE +
         sort_it<int, Less<unsigned>>();
}
