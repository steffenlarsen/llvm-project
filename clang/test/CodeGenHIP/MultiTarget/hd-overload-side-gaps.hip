// Host-device code whose meaning depends on the side: what the host and the
// devices pick, evaluate, or reject differs. Each side gets what its own
// compilation gives.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// Taking the address of an overload with one candidate per side, and a
// constant whose evaluation calls a side's overload.
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -O1 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++20 -O1 -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc
// RUN: FileCheck --check-prefix=HOST %s < %t.host.ll
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=DEV %s
// The targets' own compilations agree.
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -O1 -x hip -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=HOST %s
// RUN: %clang_cc1 -std=c++20 %{hip_device} %{host_aux_triple} -O1 \
// RUN:   -target-cpu gfx90a -emit-llvm -o - %s | FileCheck --check-prefix=DEV %s

// The devices pick a deleted function.
// RUN: not %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -x hip -fsyntax-only %s -DDELETED \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++20 -target-cpu gfx90a -DDELETED" \
// RUN:   2>&1 | FileCheck --check-prefix=DELETED %s

// Calls of one host function, of which the devices resolve some to their own
// function. The others are wrong-side calls for them, also on the line of one
// they resolve.
// RUN: not %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -x hip -fsyntax-only %s -DTWO_CALLS \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++20 -target-cpu gfx90a -DTWO_CALLS" \
// RUN:   2>&1 | FileCheck --check-prefix=TWO-CALLS %s

// A kernel launch from host-device code, which the devices reject.
// RUN: not %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -x hip -fsyntax-only %s -DLAUNCH \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++20 -target-cpu gfx90a -DLAUNCH" \
// RUN:   2>&1 | FileCheck --check-prefix=LAUNCH %s

// A device lambda capturing a host variable by reference, launched through a
// kernel template the host instantiates. A capture by value is fine.
// RUN: not %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -x hip -fsyntax-only %s -DCAPTURE \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++20 -target-cpu gfx90a -DCAPTURE" \
// RUN:   2>&1 | FileCheck --check-prefix=CAPTURE %s

// A function template whose body differs per side, which a kernel
// instantiates. Only the side whose body calls a host function is wrong.
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -x hip -fsyntax-only %s -DBODY -verify \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++20 -target-cpu gfx90a -DBODY"
// RUN: not %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -x hip -fsyntax-only %s -DBODY \
// RUN:   -DDEVICE_BODY_CALLS_HOST \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++20 -target-cpu gfx90a -DBODY -DDEVICE_BODY_CALLS_HOST" \
// RUN:   2>&1 | FileCheck --check-prefix=DEVICE-BODY %s

#define __device__ __attribute__((device))
#define __host__ __attribute__((host))

#if defined(DELETED)
int g(int x) { return x + 1; }
__device__ int g(long x) = delete;
__host__ __device__ int hd(int x) { return g(x); }
__attribute__((device, used)) int use(int x) { return hd(x); }
// DELETED: error: call to deleted function 'g'
// DELETED: 1 error generated when compiling for gfx90a.

#elif defined(TWO_CALLS)
int g(int x) { return x + 1; }
template <class T>
  requires(sizeof(T) == 8)
__device__ int g(T x) { return x + 2; }
__host__ __device__ int hd(int x) {
  int a = g(1L);
  int b = g(x);
  int c = g(2L) + g(x);
  return a + b + c;
}
__attribute__((device, used)) int use(int x) { return hd(x); }
// TWO-CALLS: hd-overload-side-gaps.hip:[[@LINE-5]]:11: error: reference to __host__ function 'g' in __host__ __device__ function
// TWO-CALLS: hd-overload-side-gaps.hip:[[@LINE-5]]:19: error: reference to __host__ function 'g' in __host__ __device__ function
// TWO-CALLS: 2 errors generated when compiling for gfx90a.

#elif defined(BODY)
#define __global__ __attribute__((global))
struct dim3 {
  unsigned x, y, z;
  dim3(unsigned x = 1, unsigned y = 1, unsigned z = 1) : x(x), y(y), z(z) {}
};
int hipConfigureCall(dim3, dim3, unsigned long = 0, void * = nullptr);
void host_only();
#if defined(__HIP_DEVICE_COMPILE__) == defined(DEVICE_BODY_CALLS_HOST)
#define ON_ONE_SIDE() host_only()
#else
#define ON_ONE_SIDE()
#endif
template <class T> __host__ __device__ T bump(T x) {
  if (x > 100)
    ON_ONE_SIDE();
  return x + 1;
}
template <class T> __global__ void kern(T *p) { *p = bump(*p); }
void launch(int *p) { kern<<<1, 1>>>(p); }
#if !defined(DEVICE_BODY_CALLS_HOST)
// expected-no-diagnostics
#endif
// DEVICE-BODY: error: reference to __host__ function 'host_only' in __host__ __device__ function
// DEVICE-BODY: 1 error generated when compiling for gfx90a.

#elif defined(LAUNCH) || defined(CAPTURE)
#define __global__ __attribute__((global))
struct dim3 {
  unsigned x, y, z;
  dim3(unsigned x = 1, unsigned y = 1, unsigned z = 1) : x(x), y(y), z(z) {}
};
int hipConfigureCall(dim3, dim3, unsigned long = 0, void * = nullptr);
#if defined(CAPTURE)
template <class F> __global__ void kern(F f) { f(); }
void by_reference() {
  int x = 1;
  kern<<<1, 1>>>([&] __device__() { (void)x; });
}
void by_value() {
  int y = 1;
  kern<<<1, 1>>>([=] __device__() { (void)y; });
}
// CAPTURE: error: capture host variable 'x' by reference in device or host device lambda function
// CAPTURE: note: called by 'kern<
// CAPTURE-NOT: error:
// CAPTURE: 1 error generated when compiling for gfx90a.
#else
__global__ void k();
__host__ __device__ void hd() { k<<<1, 1>>>(); }
__global__ void caller() { hd(); }
// LAUNCH: error: device-side kernel call/launch is not supported
// LAUNCH: 1 error generated when compiling for gfx90a.
#endif

#else
// The address of g, one candidate for each side.
int g(int x) { return x + 1; }
__device__ int g(long x) { return x + 2; }
__host__ __device__ int hd_address(int x) {
  auto p = &g;
  return p(x);
}
// HOST-LABEL: define {{.*}} @_Z10hd_addressi(
// HOST: add nsw i32 %x, 1
// DEV-LABEL: define {{.*}} @_Z10hd_addressi(
// DEV: add i32 %x, 2

// The address of h, where each side keeps two candidates and the host's
// template is eliminated for its non-template, the devices' only candidate.
int h(int x) { return x + 1; }
template <class T> T h(T x) { return x + 2; }
template <class T> __device__ T h(T x) { return x + 3; }
__host__ __device__ int hd_address_template(int x) {
  int (*p)(int) = &h;
  return p(x);
}
// HOST-LABEL: define {{.*}} @_Z19hd_address_templatei(
// HOST: add nsw i32 %x, 1
// DEV-LABEL: define {{.*}} @_Z19hd_address_templatei(
// DEV: add nsw i32 %x, 3

// A constant that calls f, which each side resolves to its own.
__host__ constexpr int f(int x) { return x + 1; }
__device__ constexpr int f(long x) { return x + 2; }
__host__ __device__ constexpr int via_f(int x) { return f(x); }
__attribute__((device, used)) int dev_constant() {
  constexpr int v = via_f(1);
  return v;
}
int host_constant() {
  constexpr int v = via_f(1);
  return v;
}
__attribute__((device, used)) int dev_folded(int x) { return via_f(10) + x; }
// DEV-LABEL: define {{.*}} @_Z12dev_constantv(
// DEV: ret i32 3
// DEV-LABEL: define {{.*}} @_Z10dev_foldedi(
// DEV: add nsw i32 %x, 12
// HOST-LABEL: define {{.*}} @_Z13host_constantv(
// HOST: ret i32 2

__attribute__((device, used)) int use(int x) {
  return hd_address(x) + hd_address_template(x);
}
int host_use(int x) { return hd_address(x) + hd_address_template(x); }
#endif
