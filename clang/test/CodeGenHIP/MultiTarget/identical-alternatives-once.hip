// An alternative with the same tokens as another device's is parsed once for
// both, unless what parsing it consumed differs between them. So is a
// declaration re-parsed for the devices, once for each group of devices it
// means the same to.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1100 -O2" \
// RUN:   -multi-target-aux-output=2:%t.gfx1100.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1101 -O2" \
// RUN:   -multi-target-aux-output=3:%t.gfx1101.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1200 -O2" \
// RUN:   -multi-target-aux-output=4:%t.gfx1200.bc
// RUN: llvm-dis %t.gfx1100.bc -o - | FileCheck --check-prefixes=RDNA3,GFX1100 %s
// RUN: llvm-dis %t.gfx1101.bc -o - | FileCheck --check-prefixes=RDNA3,GFX1101 %s
// RUN: llvm-dis %t.gfx1200.bc -o - | FileCheck --check-prefix=GFX1200 %s

// Parsing every alternative and comparing those that would be left to another
// finds no difference.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.v.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1100 -O2" \
// RUN:   -multi-target-aux-output=2:%t.v.gfx1100.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1101 -O2" \
// RUN:   -multi-target-aux-output=3:%t.v.gfx1101.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1200 -O2" \
// RUN:   -multi-target-aux-output=4:%t.v.gfx1200.bc \
// RUN: -mllvm -verify-identical-alternatives
// RUN: llvm-dis %t.v.gfx1101.bc -o - | FileCheck --check-prefixes=RDNA3,GFX1101 %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

// Specializations of `tile` are keyed by device, so every device parses the
// `sizes` alternative itself. Each device has its own copy of `sizes`, with its
// own partial specialization, so `f`, whose tokens are the same for gfx1100
// and gfx1101, is parsed for each of them and gfx1101's `f` instantiates
// gfx1101's partial specialization.
template <int N, bool B = false> struct tile { int x[N]; };
#if defined(__gfx1101__)
template <int N> struct tile<N, true> { int y[N]; };
#endif

#if defined(__gfx1100__) || defined(__gfx1101__)
template <int D, int C> struct sizes { using T = tile<1>; };
template <int C> struct sizes<80, C> { using T = tile<2>; };
#else
template <int D, int C> struct sizes { using T = tile<5>; };
#endif

#if defined(__gfx1100__) || defined(__gfx1101__)
template <int D> __device__ int f() { return sizeof(typename sizes<D, 4>::T) + 100; }
#else
template <int D> __device__ int f() { return sizeof(typename sizes<D, 4>::T) + 200; }
#endif

// `g` names a typedef gfx1200 has its own of, so it is re-parsed for the
// devices, once for gfx1100 and gfx1101 and once for gfx1200.
#if defined(__gfx1200__)
typedef short word;
#else
typedef int word;
#endif
__device__ int g() { return sizeof(word); }

// The devices have the same tokens, but gfx1101 answers the predicate
// differently.
#if defined(__HIP_DEVICE_COMPILE__)
__device__ int p() {
  if (__builtin_amdgcn_processor_is("gfx1101"))
    return 11;
  return 22;
}
#else
__device__ int p();
#endif

__global__ void k(int *o) {
  o[0] = f<80>();
  o[1] = g();
  o[2] = p();
}
// RDNA3-LABEL: define {{.*}} @_Z1kPi(
// RDNA3: store i32 108,
// RDNA3: store i32 4,
// GFX1100: store i32 22,
// GFX1101: store i32 11,
// GFX1200-LABEL: define {{.*}} @_Z1kPi(
// GFX1200: store i32 220,
// GFX1200: store i32 2,
// GFX1200: store i32 22,

// `nthreads` is re-parsed for the devices. Its call to `cfg` binds each
// device's copy, which constant evaluation follows, so gfx1200 is parsed for
// itself although the copies have the same signature.
#if defined(__gfx1100__) || defined(__gfx1101__)
constexpr __device__ int cfg() { return 64; }
#elif defined(__gfx1200__)
constexpr __device__ int cfg() { return 128; }
#else
constexpr __device__ int cfg() { return 32; }
#endif
constexpr __device__ int nthreads() { return cfg() * 2; }
__global__ void kc(int *o) {
  constexpr int n = nthreads();
  o[0] = n;
}
// RDNA3-LABEL: define {{.*}} @_Z2kcPi(
// RDNA3: store i32 128,
// GFX1200-LABEL: define {{.*}} @_Z2kcPi(
// GFX1200: store i32 256,
