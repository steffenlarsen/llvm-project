// A precompiled header written for several device targets serves a
// compilation for a subset of them that keeps its first device target, or for
// any one of them. Each target still uses the specializations instantiated for
// it while precompiling, and the attribute values computed for it.
//
// REQUIRES: amdgpu-registered-target

// RUN: rm -rf %t && split-file %s %t
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip-header -emit-pch -o %t/prefix.pch \
// RUN:   -fpch-instantiate-templates \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -fpch-instantiate-templates -x hip" \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -fpch-instantiate-templates -x hip" \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx908 -fcuda-is-device -fpch-instantiate-templates -x hip" \
// RUN:   -mllvm -multi-target-record -mllvm -parse-all-target-alternatives \
// RUN:   -mllvm -widen-target-alternatives -mllvm -allow-target-variant-decls \
// RUN:   -mllvm -reparse-divergent-users -mllvm -merge-equivalent-variants \
// RUN:   %t/prefix.h

// A single device target other than the first.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx942 -x hip -emit-llvm -o %t/one.ll \
// RUN:   -include-pch %t/prefix.pch \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -include %t/prefix.h -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t/one-gfx942.bc \
// RUN:   -mllvm -multi-target-codegen -mllvm -multi-target-record \
// RUN:   -mllvm -parse-all-target-alternatives -mllvm -widen-target-alternatives \
// RUN:   -mllvm -allow-target-variant-decls -mllvm -reparse-divergent-users \
// RUN:   -mllvm -merge-equivalent-variants %t/use.hip
// RUN: FileCheck -check-prefix=HOST %s < %t/one.ll
// RUN: llvm-dis -o - %t/one-gfx942.bc | FileCheck -check-prefix=GFX942 %s \
// RUN:   --implicit-check-not="ret i32 90{{$}}" --implicit-check-not="ret i32 180{{$}}" \
// RUN:   --implicit-check-not="ret i32 720{{$}}" --implicit-check-not='"1,90"' \
// RUN:   --implicit-check-not="ret i32 908{{$}}" --implicit-check-not="ret i32 1816{{$}}" \
// RUN:   --implicit-check-not="ret i32 7264{{$}}" --implicit-check-not='"1,908"'

// The first device target and the last, without the one in between.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -emit-llvm -o %t/two.ll \
// RUN:   -include-pch %t/prefix.pch \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -include %t/prefix.h -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t/two-gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx908 -fcuda-is-device -include %t/prefix.h -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t/two-gfx908.bc \
// RUN:   -mllvm -multi-target-codegen -mllvm -multi-target-record \
// RUN:   -mllvm -parse-all-target-alternatives -mllvm -widen-target-alternatives \
// RUN:   -mllvm -allow-target-variant-decls -mllvm -reparse-divergent-users \
// RUN:   -mllvm -merge-equivalent-variants %t/use.hip
// RUN: FileCheck -check-prefix=HOST %s < %t/two.ll
// RUN: llvm-dis -o - %t/two-gfx90a.bc | FileCheck -check-prefix=GFX90A %s \
// RUN:   --implicit-check-not="ret i32 942{{$}}" --implicit-check-not="ret i32 1884{{$}}" \
// RUN:   --implicit-check-not="ret i32 7536{{$}}" --implicit-check-not='"1,942"' \
// RUN:   --implicit-check-not="ret i32 908{{$}}" --implicit-check-not="ret i32 1816{{$}}" \
// RUN:   --implicit-check-not="ret i32 7264{{$}}" --implicit-check-not='"1,908"'
// RUN: llvm-dis -o - %t/two-gfx908.bc | FileCheck -check-prefix=GFX908 %s \
// RUN:   --implicit-check-not="ret i32 90{{$}}" --implicit-check-not="ret i32 180{{$}}" \
// RUN:   --implicit-check-not="ret i32 720{{$}}" --implicit-check-not='"1,90"' \
// RUN:   --implicit-check-not="ret i32 942{{$}}" --implicit-check-not="ret i32 1884{{$}}" \
// RUN:   --implicit-check-not="ret i32 7536{{$}}" --implicit-check-not='"1,942"'

// Without the first device target, the specializations it shared with the
// others can no longer be told apart.
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx942 -x hip -emit-llvm -o /dev/null \
// RUN:   -include-pch %t/prefix.pch \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -include %t/prefix.h -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t/no-gfx942.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx908 -fcuda-is-device -include %t/prefix.h -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t/no-gfx908.bc \
// RUN:   -mllvm -multi-target-codegen -mllvm -multi-target-record \
// RUN:   -mllvm -parse-all-target-alternatives -mllvm -widen-target-alternatives \
// RUN:   -mllvm -allow-target-variant-decls -mllvm -reparse-divergent-users \
// RUN:   -mllvm -merge-equivalent-variants %t/use.hip 2>&1 \
// RUN: | FileCheck -check-prefix=REJECT %s

// HOST: define weak_odr void @_Z19__device_stub__kernIsEvPi(
// HOST: define dso_local void @_Z16__device_stub__kPi(

// GFX942-DAG: define amdgpu_kernel void @_Z4kernIsEvPi(
// GFX942-DAG: define {{.*}} i32 @_Z5widthIcEiv() {{.*}}{
// GFX942-DAG: ret i32 942
// GFX942-DAG: ret i32 1884
// GFX942-DAG: ret i32 7536
// GFX942-DAG: "amdgpu-flat-work-group-size"="1,942"

// GFX90A-DAG: define amdgpu_kernel void @_Z4kernIsEvPi(
// GFX90A-DAG: ret i32 90
// GFX90A-DAG: ret i32 180
// GFX90A-DAG: ret i32 720
// GFX90A-DAG: "amdgpu-flat-work-group-size"="1,90"

// GFX908-DAG: define amdgpu_kernel void @_Z4kernIsEvPi(
// GFX908-DAG: ret i32 908
// GFX908-DAG: ret i32 1816
// GFX908-DAG: ret i32 7264
// GFX908-DAG: "amdgpu-flat-work-group-size"="1,908"

// REJECT: fatal error: precompiled file '{{.*}}prefix.pch' was built for the targets '{{.*}}gfx90a{{.*}}gfx942{{.*}}gfx908{{.*}}', but this compilation is for '{{.*}}gfx942{{.*}}gfx908{{.*}}'
// REJECT-NEXT: note: a precompiled file serves a subset of its targets only if the subset keeps its first device target or has a single one

//--- prefix.h
#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

constexpr int arch() {
#if defined(__gfx90a__)
  return 90;
#elif defined(__gfx942__)
  return 942;
#elif defined(__gfx908__)
  return 908;
#else
  return 1;
#endif
}

template <typename T> __device__ int width() {
  constexpr int A = arch() * sizeof(T);
  return A;
}
inline __device__ int prefix_width() { return width<char>(); }
template <typename T> __global__ void kern(int *o) { o[0] = width<T>(); }
template __global__ void kern<short>(int *);

// One specialization for every target, with an attribute argument whose value
// is each target's own.
struct Bounds { static constexpr int max() { return arch(); } };
template <typename B>
__global__ void __attribute__((launch_bounds(B::max()))) bounded(int *o) {}
template __global__ void bounded<Bounds>(int *);

//--- use.hip
__global__ void k(int *o) {
  o[0] = prefix_width();
  o[1] = width<long>();
}
