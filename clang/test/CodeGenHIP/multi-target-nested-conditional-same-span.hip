// Conditionals nested with nothing between them -- `#if 1` / `#if 1` /
// an include guard -- cover the same tokens. The aligner must order them alike
// in every target's recording, or it matches no conditional inside them and
// treats all they contain as divergent: every declaration there is parsed once
// per target. This follows HeCBench's bf16 units, which include
// amd_hip_fp8.h (and so amd_hip_bf16.h) inside two such conditionals ("no
// matching conversion for functional-style cast from '__hip_bfloat16_raw' to
// '__hip_bfloat16'").
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang -x hip --offload-arch=gfx90a -nogpulib -nogpuinc \
// RUN:   -fintegrated-hip-device-codegen -fsyntax-only -Xclang -verify %s
// RUN: %clang -x hip --offload-arch=gfx90a --offload-arch=gfx942 \
// RUN:   -nogpulib -nogpuinc -fintegrated-hip-device-codegen -fsyntax-only \
// RUN:   -Xclang -verify %s

// Only div_fn's two arms are a host alternative; everything else the host
// sees is shared.
// RUN: %clang -x hip --offload-arch=gfx90a -nogpulib -nogpuinc \
// RUN:   -fintegrated-hip-device-codegen -fsyntax-only \
// RUN:   -mllvm -dump-conditional-regions %s 2>&1 | FileCheck %s
// CHECK: multi-target recording:
// CHECK: alternative: 22 /

// expected-no-diagnostics

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define HD __host__ __device__
#define D10(p)                                                                 \
  __device__ int p##0(); __device__ int p##1(); __device__ int p##2();         \
  __device__ int p##3(); __device__ int p##4(); __device__ int p##5();         \
  __device__ int p##6(); __device__ int p##7(); __device__ int p##8();         \
  __device__ int p##9();
#if 1
#if 1
#ifndef RAW_GUARD
#define RAW_GUARD
typedef struct { unsigned short x; } raw_t;
#if 1
int shared_before = 0;
#endif
#ifdef __HIP_DEVICE_COMPILE__
HD inline int div_fn() { return 1; }
#else
HD inline int div_fn() { return 2; }
#endif
#if 0 < 5
int m0 = 0;
#endif
#ifdef __HIP_DEVICE_COMPILE__
#if 1
int md0 = 1;
#endif
#endif
#if 1 < 5
int m1 = 1;
#endif
#if 2 < 5
int m2 = 2;
#endif
#if 0 < 5
int m3 = 3;
#endif
#ifdef __HIP_DEVICE_COMPILE__
#if 1
int md3 = 1;
#endif
#endif
#if 1 < 5
int m4 = 4;
#endif
#if 2 < 5
int m5 = 5;
#endif
#if 0 < 5
int m6 = 6;
#endif
#ifdef __HIP_DEVICE_COMPILE__
#if 1
int md6 = 1;
#endif
#endif
#if 1 < 5
int m7 = 7;
#endif
#if 2 < 5
int m8 = 8;
#endif
#ifdef __HIP_DEVICE_COMPILE__
D10(e0) D10(e1) D10(e2) D10(e3) D10(e4) D10(e5) D10(e6)
#endif
#if 0 < 5
int n0 = 0;
#endif
#ifdef __HIP_DEVICE_COMPILE__
#if 1
int nd0 = 1;
#endif
#endif
#if 1 < 5
int n1 = 1;
#endif
#if 2 < 5
int n2 = 2;
#endif
#if 0 < 5
int n3 = 3;
#endif
#ifdef __HIP_DEVICE_COMPILE__
#if 1
int nd3 = 1;
#endif
#endif
#if 1 < 5
int n4 = 4;
#endif
#if 2 < 5
int n5 = 5;
#endif
#if 0 < 5
int n6 = 6;
#endif
#ifdef __HIP_DEVICE_COMPILE__
#if 1
int nd6 = 1;
#endif
#endif
#if 1 < 5
int n7 = 7;
#endif
#if 2 < 5
int n8 = 8;
#endif
struct bf { unsigned short x; HD constexpr bf(const raw_t &r) : x(r.x) {} };
HD inline bf make_bf(unsigned short s) { return bf(raw_t{s}); }
#endif
#endif
#endif
