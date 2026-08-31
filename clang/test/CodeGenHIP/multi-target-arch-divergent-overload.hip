// A function that the device architectures declare differently -- only on one,
// or as __host__ on one -- can be picked, or rejected, differently per
// architecture by overload resolution. A kernel calling it is analysed for
// each architecture, as each architecture's own compilation does.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -emit-llvm -o %t.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx942.bc
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=GFX90A %s
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck --check-prefix=GFX942 %s
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=LAMBDA-GFX90A %s
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck --check-prefix=LAMBDA-GFX942 %s

// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -emit-llvm -o %t.err.ll %s -DHOST_ONLY_ON_GFX942 \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip -DHOST_ONLY_ON_GFX942" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.err.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip -DHOST_ONLY_ON_GFX942" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.err.gfx942.bc \
// RUN: 2>&1 | FileCheck --check-prefix=ERR %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#ifdef HOST_ONLY_ON_GFX942
#if defined(__gfx942__)
#define VIS __host__
#else
#define VIS __host__ __device__
#endif
VIS inline int make(int g) { return g + 1; }
// ERR: error: no matching function for call to 'make'
// ERR: note: candidate function not viable: call to __host__ function from __global__ function
__global__ void k_host_only(int *o) { *o = make(*o); }
// ERR: error: no matching function for call to 'make'
// ERR: note: in instantiation of function template specialization 'k_host_only_template<int>' requested here
template <class T> __global__ void k_host_only_template(T *o) { *o = make(*o); }
template __global__ void k_host_only_template<int>(int *);
#else

// An overload only one architecture has.
__device__ inline int only(double) { return 2; }
#if defined(__gfx942__)
__device__ inline int only(int) { return 1; }
#endif
__global__ void k_only(int *o) { *o = only(*o); }
// GFX90A-LABEL: define {{.*}} @_Z6k_onlyPi(
// GFX90A: call {{.*}} @_Z4onlyd(
// GFX942-LABEL: define {{.*}} @_Z6k_onlyPi(
// GFX942: call {{.*}} @_Z4onlyi(

// An overload one architecture cannot call from device code.
#if defined(__gfx942__)
#define VIS __host__
#else
#define VIS __host__ __device__
#endif
VIS inline int side(int) { return 3; }
__host__ __device__ inline int side(double) { return 4; }
__global__ void k_side(int *o) { *o = side(*o); }
// GFX90A-LABEL: define {{.*}} @_Z6k_sidePi(
// GFX90A: call {{.*}} @_Z4sidei(
// GFX942-LABEL: define {{.*}} @_Z6k_sidePi(
// GFX942: call {{.*}} @_Z4sided(

// In a template, each architecture's instantiation resolves the call among
// its own overloads: explicitly instantiated, called from device code, and
// launched from the host.
template <class T> __global__ void k_template(T *o) { *o = only(*o); }
template __global__ void k_template<int>(int *);
// GFX90A-LABEL: define {{.*}} @_Z10k_templateIiEvPT_(
// GFX90A: call {{.*}} @_Z4onlyd(
// GFX942-LABEL: define {{.*}} @_Z10k_templateIiEvPT_(
// GFX942: call {{.*}} @_Z4onlyi(

template <class T> __device__ int by_template(T x) { return only(x); }
__global__ void k_device_template(int *o) { *o = by_template(*o); }
// GFX90A-LABEL: define {{.*}} @_Z11by_templateIiEiT_(
// GFX90A: call {{.*}} @_Z4onlyd(
// GFX942-LABEL: define {{.*}} @_Z11by_templateIiEiT_(
// GFX942: call {{.*}} @_Z4onlyi(

// An overload the other targets have but one architecture leaves out.
__host__ __device__ inline int most(double) { return 2; }
#if !defined(__gfx942__)
__host__ __device__ inline int most(int) { return 1; }
#endif
__global__ void k_most(int *o) { *o = most(*o); }
// GFX90A-LABEL: define {{.*}} @_Z6k_mostPi(
// GFX90A: call {{.*}} @_Z4mosti(
// GFX942-LABEL: define {{.*}} @_Z6k_mostPi(
// GFX942: call {{.*}} @_Z4mostd(

struct dim3 {
  unsigned x, y, z;
  dim3(unsigned x = 1, unsigned y = 1, unsigned z = 1) : x(x), y(y), z(z) {}
};
int hipConfigureCall(dim3, dim3, unsigned long = 0, void * = nullptr);
template <class T> __global__ void k_launched(T *o) { *o = only(*o); }
void launch(int *o) { k_launched<<<1, 1>>>(o); }
// GFX90A-LABEL: define {{.*}} @_Z10k_launchedIiEvPT_(
// GFX90A: call {{.*}} @_Z4onlyd(
// GFX942-LABEL: define {{.*}} @_Z10k_launchedIiEvPT_(
// GFX942: call {{.*}} @_Z4onlyi(

// So does a lambda in it, whose closure is each instantiation's own.
template <class F> __device__ int apply(F f, int x) { return f(x); }
template <class T> __global__ void k_lambda(T *o) {
  *o = apply([](int x) { return only(x); }, *o);
}
template __global__ void k_lambda<int>(int *);
// LAMBDA-GFX90A-LABEL: define {{.*}} @_ZZ8k_lambdaIiEvPT_ENKUliE_clEi(
// LAMBDA-GFX90A: call {{.*}} @_Z4onlyd(
// LAMBDA-GFX942-LABEL: define {{.*}} @_ZZ8k_lambdaIiEvPT_ENKUliE_clEi(
// LAMBDA-GFX942: call {{.*}} @_Z4onlyi(
#endif
