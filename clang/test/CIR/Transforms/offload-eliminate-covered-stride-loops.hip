// End-to-end grid-stride loop elimination for HIP: real CIRGen output for host
// and device TUs, merged by the merge tool. A launch that covers its range runs
// a copy of the kernel whose loop takes a single trip.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -fno-clangir-call-conv-lowering -O2 \
// RUN:   -target-sdk-version=9.2 -fhip-new-launch-api -x hip -I %S/../../CodeGenCUDA/Inputs \
// RUN:   -emit-cir %s -o %t-host.cir
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx900 -target-sdk-version=9.2 -O2 \
// RUN:   -fno-clangir-call-conv-lowering -fcuda-is-device -fhip-new-launch-api -x hip -I %S/../../CodeGenCUDA/Inputs \
// RUN:   -emit-cir %s -o %t-dev.cir
// RUN: cir-offload-merge -combine -input=%t-host.cir -input=%t-dev.cir \
// RUN:   -targets=host-x86_64-unknown-linux-gnu,hip-amdgcn-amd-amdhsa--gfx900 \
// RUN:   -output=%t-combined.cir
// RUN: FileCheck %s --input-file=%t-combined.cir
// RUN: FileCheck %s --input-file=%t-combined.cir --check-prefix=NEG
// RUN: FileCheck %s --input-file=%t-combined.cir --check-prefix=OFFSET

#include "cuda.h"

struct __hip_builtin_blockIdx_t {
  __device__ static unsigned __get_x();
};
struct __hip_builtin_blockDim_t {
  __device__ static unsigned __get_x();
  __device__ static unsigned __get_y();
};
struct __hip_builtin_threadIdx_t {
  __device__ static unsigned __get_x();
  __device__ static unsigned __get_y();
};
struct __hip_builtin_gridDim_t {
  __device__ static unsigned __get_x();
};

#define BLOCK_ID_X __hip_builtin_blockIdx_t::__get_x()
#define BLOCK_DIM_X __hip_builtin_blockDim_t::__get_x()
#define BLOCK_DIM_Y __hip_builtin_blockDim_t::__get_y()
#define THREAD_ID_X __hip_builtin_threadIdx_t::__get_x()
#define THREAD_ID_Y __hip_builtin_threadIdx_t::__get_y()
#define GRID_DIM_X __hip_builtin_gridDim_t::__get_x()

__device__ void use(int);

// The plain idiom.
__global__ void plain(int n) {
  for (int i = BLOCK_ID_X * BLOCK_DIM_X + THREAD_ID_X; i < n;
       i += BLOCK_DIM_X * GRID_DIM_X)
    use(i);
}
void host_plain(int n) { plain<<<(n + 255) / 256, 256>>>(n); }

// The shape Kokkos launches: the kernel calls a driver passed by value, whose
// policy -- a base class -- hands out the range through accessors; threads
// run along y, and the step is clamped to the end.
struct RangeBase {
  int space;
  int lo;
  int hi;
  __host__ __device__ int begin() const { return lo; }
  __host__ __device__ int end() const { return hi; }
};
struct Range : RangeBase {};
struct Driver {
  int scale;
  Range policy;
  __device__ void operator()() const {
    const unsigned stride = BLOCK_DIM_Y * GRID_DIM_X;
    const int last = policy.end();
    for (int i = policy.begin() + THREAD_ID_Y + BLOCK_DIM_Y * BLOCK_ID_X;
         i < last; i = i < (int)(last - stride) ? (int)(i + stride) : last)
      use(i * scale);
  }
};
__global__ void driven(const Driver d) { d(); }
void host_driven(Driver d, unsigned bs) {
  unsigned n = d.policy.end() - d.policy.begin();
  driven<<<dim3((n + bs - 1) / bs), dim3(1, bs)>>>(d);
}

// The plain launch picks the copy when the stride covers the range, and when
// the first step does not wrap in the int the variable is.
// CHECK-LABEL: cir.func{{.*}} @_Z10host_plaini(
// CHECK:         %[[COVERS:.*]] = cir.cmp ge %{{.*}}, %{{.*}} : !s64i
// CHECK-NOT:     4294967295
// CHECK:         %[[INTMAX:.*]] = cir.const #cir.int<2147483647> : !s64i
// CHECK:         cir.cmp le %{{.*}}, %[[INTMAX]] : !s64i
// CHECK-NOT:     4294967295
// CHECK:           cir.call @_Z20__device_stub__plaini.guards(
// CHECK-NEXT:    } else {
// CHECK-NEXT:      cir.call @_Z20__device_stub__plaini{{(\.[a-z]+)?}}(

// The driven launch reads begin and end out of the driver it passes, and
// takes the copy when begin is at least end - stride, which is where the
// clamp's comparison is false.
// CHECK-LABEL: cir.func{{.*}} @_Z11host_driven6Driverj(
// CHECK:         cir.extract_member
// CHECK:         cir.cmp ge %{{.*}}, %{{.*}} : !s64i
// CHECK:         cir.call @_Z21__device_stub__driven6Driver.guards(

// In the plain copy the step stores the bound, which LLVM turns into a single
// trip; the original keeps its loop.
// CHECK:       cir.func{{.*}} @_Z5plaini(
// CHECK:         } step {
// CHECK:           %[[SUM:.*]] = cir.add
// CHECK-NEXT:      %[[NEXT:.*]] = cir.cast integral %[[SUM]]
// CHECK-NEXT:      cir.store {{.*}}%[[NEXT]],
// CHECK:       cir.func{{.*}} @_Z5plaini.guards(
// CHECK:         } step {
// CHECK:           cir.cast integral {{.*}} -> !s32i
// CHECK-NEXT:      %[[BOUND:.*]] = cir.load
// CHECK-NEXT:      cir.store {{.*}}%[[BOUND]],

// The driven kernel has its driver inlined, the loop with it; in the copy the
// clamp's comparison is folded to false.
// CHECK:       cir.func{{.*}} @_Z6driven6Driver(
// CHECK:         cir.for
// CHECK:         } step {
// CHECK:           %[[CLAMP:.*]] = cir.cmp lt
// CHECK:           cir.ternary(%[[CLAMP]],
// CHECK:       cir.func{{.*}} @_Z6driven6Driver.guards(
// CHECK:         } step {
// CHECK:           %[[NO:.*]] = cir.const #false
// CHECK-NEXT:      cir.ternary(%[[NO]],

// The variable read after the loop would see the stored bound.
__global__ void read_after(int *out, int n) {
  int i = BLOCK_ID_X * BLOCK_DIM_X + THREAD_ID_X;
  for (; i < n; i += BLOCK_DIM_X * GRID_DIM_X)
    use(i);
  *out = i;
}
void host_read_after(int *out, int n) {
  read_after<<<(n + 255) / 256, 256>>>(out, n);
}
// NEG-NOT: read_after{{.*}}.guards

// The body steps the variable too.
__global__ void body_writes(int n) {
  for (int i = BLOCK_ID_X * BLOCK_DIM_X + THREAD_ID_X; i < n;
       i += BLOCK_DIM_X * GRID_DIM_X)
    if ((i & 1) == 0)
      use(i++);
}
void host_body_writes(int n) { body_writes<<<(n + 255) / 256, 256>>>(n); }
// NEG-NOT: body_writes{{.*}}.guards

// The end is changed before the loop reads it, through a mutable member of
// the const driver, so it is not the value the launch passes.
struct Mutating {
  mutable int hi;
  __device__ void operator()() const {
    hi = hi + 1;
    const unsigned stride = BLOCK_DIM_X * GRID_DIM_X;
    const int last = hi;
    for (int i = BLOCK_ID_X * BLOCK_DIM_X + THREAD_ID_X; i < last;
         i = i < (int)(last - stride) ? (int)(i + stride) : last)
      use(i);
  }
};
__global__ void mutating(const Mutating m) { m(); }
void host_mutating(Mutating m) { mutating<<<(m.hi + 255) / 256, 256>>>(m); }
// NEG-NOT: mutating{{.*}}.guards

// The clamp keeps the variable where it is instead of landing on the end:
// folding its comparison would leave the loop running forever.
__global__ void clamp_stays(int n) {
  const unsigned stride = BLOCK_DIM_X * GRID_DIM_X;
  for (int i = BLOCK_ID_X * BLOCK_DIM_X + THREAD_ID_X; i < n;
       i = i < (int)(n - stride) ? (int)(i + stride) : i)
    use(i);
}
void host_clamp_stays(int n) { clamp_stays<<<(n + 255) / 256, 256>>>(n); }
// NEG-NOT: clamp_stays{{.*}}.guards

// A tail guard whose index starts at a field of the driver: the host adds the
// field to the index's bound, once it has checked the field is not negative.
struct Offset {
  Range policy;
  __device__ void operator()() const {
    int i = policy.begin() + (int)(BLOCK_ID_X * BLOCK_DIM_X + THREAD_ID_X);
    if (i < policy.end())
      use(i);
  }
};
__global__ void offset(const Offset o) { o(); }
void host_offset(Offset o) {
  offset<<<(o.policy.end() - o.policy.begin() + 255) / 256, 256>>>(o);
}
// OFFSET-LABEL: cir.func{{.*}} @_Z11host_offset6Offset(
// OFFSET:         %[[BEGIN:.*]] = cir.extract_member %{{.*}}[1] : !rec{{.*}} -> !s32i
// OFFSET-NEXT:    %[[WIDE:.*]] = cir.cast integral %[[BEGIN]] : !s32i -> !s64i
// OFFSET:         cir.cmp ge %[[WIDE]], %{{.*}} : !s64i
// OFFSET:         cir.add %{{.*}}, %[[WIDE]] : !s64i
// OFFSET:         cir.call @_Z21__device_stub__offset6Offset.guards(
// OFFSET:       cir.func{{.*}} @_Z6offset6Offset.guards(
// OFFSET-NOT:     cir.cmp lt
// OFFSET:         cir.const #true
