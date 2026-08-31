// The single frontend analyses once, for every target, only what consumed
// nothing that differs between them; the rest is analysed per target.
// -mllvm -multi-target-audit reports what is still analysed once although it
// consumed such an input: the attributes a language option or target property
// gives a library function, and an instantiation (each target's is keyed by
// it). A declaration naming a per-target typedef or class, using the layout of
// a type that differs between the host and the devices, or a specialization
// instantiated from such a type is analysed per target and not reported; nor is
// a runtime read of a per-target constant or a call to a function whose copies
// differ only in their bodies (CodeGen picks each target's).
//
// REQUIRES: amdgpu-registered-target

// RUN: rm -f %t.audit
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -fsyntax-only -fmath-errno %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip" \
// RUN: -mllvm -multi-target-audit=%t.audit
// RUN: FileCheck %s < %t.audit
// RUN: FileCheck --check-prefix=NONE %s < %t.audit

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#if defined(__gfx942__)
typedef float T;
struct Base { int x = 42; };
#else
typedef double T;
struct Base { int x = 7; int y = 0; };
#endif
struct D : Base { int z = 1; };

__device__ T by_typedef(T x) { return x * 3; }
__host__ __device__ int by_long_double() { return sizeof(long double); }

#if defined(__gfx942__)
constexpr int W = 64;
__device__ int body() { return 1; }
#else
constexpr int W = 32;
__device__ int body() { return 2; }
#endif
__device__ int runtime_value() { return W; }
__device__ int body_only() { return body(); }
template <int N> struct Buf { int d[N]; };
__device__ int evaluated() { Buf<W> b{}; return sizeof(b); }

extern "C" __host__ __device__ double sqrt(double);
extern "C" __host__ __device__ float fmaf(float, float, float);
template <class U> struct Box { U v; };
__host__ __device__ int by_specialization() { return sizeof(Box<long double>); }

// CHECK-DAG: code parse shared sqrt @ {{.*}}: O:MathErrno(side)
// CHECK-DAG: code parse shared fmaf @ {{.*}}: O:MathErrno(side) T:fma without errno(side)
// CHECK-DAG: code inst shared Box @ {{.*}}: T:long double(side)
// NONE-NOT: by_typedef @
// NONE-NOT: by_long_double @
// NONE-NOT: D::z @
// NONE-NOT: by_specialization @
// NONE-NOT: runtime_value @
// NONE-NOT: body_only @
// NONE-NOT: evaluated @
// CHECK: summary parse-units={{.*}} lang-opts=CUDAIsDevice,ConvergentFunctions,MathErrno
