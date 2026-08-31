// A kernel's launch bounds are evaluated where its declaration is formed, for
// the target it is formed for, and code generation evaluates them again. A
// constant expression reaching a function each device architecture defines
// differently gives each device's code generation the answer of the target
// it evaluates them for, which need not be the kernel's.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -std=c++17 -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a -std=c++17" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1030 -std=c++17" \
// RUN:   -multi-target-aux-output=3:%t.gfx1030.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1100 -std=c++17" \
// RUN:   -multi-target-aux-output=4:%t.gfx1100.bc
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=W64 %s
// RUN: llvm-dis %t.gfx1030.bc -o - | FileCheck --check-prefix=W32 %s
// RUN: llvm-dis %t.gfx1100.bc -o - | FileCheck --check-prefix=W32 %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#if defined(__gfx1030__) || defined(__gfx1100__)
#define RDNA
#endif

static constexpr __host__ __device__ unsigned config_rdna(int n) {
  return (128 << 4) | 3;
}
static constexpr __host__ __device__ unsigned config_cdna(int n) {
  return (256 << 4) | 2;
}
static __host__ unsigned config(int n, int cc) { return cc; }
static constexpr __device__ unsigned config(int n) {
#ifdef RDNA
  return config_rdna(n);
#else
  return config_cdna(n);
#endif
}
static constexpr __device__ int nthreads(int n) { return config(n) >> 4; }
static constexpr __device__ int occupancy(int n) { return config(n) & 15; }

// W64: define {{.*}}@_Z7boundedILi1EEvPi({{.*}}) #[[BOUNDED:[0-9]+]]
// W32: define {{.*}}@_Z7boundedILi1EEvPi({{.*}}) #[[BOUNDED:[0-9]+]]
template <int N>
__attribute__((launch_bounds(nthreads(N), occupancy(N)))) __global__ void
bounded(int *p) {
  p[0] = occupancy(N);
}

// W64: define {{.*}}@_Z5sizedILi1EEvPi({{.*}}) #[[SIZED:[0-9]+]]
// W32: define {{.*}}@_Z5sizedILi1EEvPi({{.*}}) #[[SIZED:[0-9]+]]
template <int N>
__attribute__((amdgpu_flat_work_group_size(1, nthreads(N)),
               amdgpu_waves_per_eu(occupancy(N), 10))) __global__ void
sized(int *p) {
  p[0] = occupancy(N);
}

typedef void (*kernel_t)(int *);
template <int N> kernel_t select(bool b) {
  return b ? bounded<N> : sized<N>;
}
kernel_t launch(bool b) { return select<1>(b); }

// W64-DAG: attributes #[[BOUNDED]] = {{{.*}}"amdgpu-flat-work-group-size"="1,256"{{.*}}"amdgpu-waves-per-eu"="2"
// W64-DAG: attributes #[[SIZED]] = {{{.*}}"amdgpu-flat-work-group-size"="1,256"{{.*}}"amdgpu-waves-per-eu"="2,10"
// W32-DAG: attributes #[[BOUNDED]] = {{{.*}}"amdgpu-flat-work-group-size"="1,128"{{.*}}"amdgpu-waves-per-eu"="3"
// W32-DAG: attributes #[[SIZED]] = {{{.*}}"amdgpu-flat-work-group-size"="1,128"{{.*}}"amdgpu-waves-per-eu"="3,10"
