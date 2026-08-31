// What a pragma says can differ between targets: a loop hint's argument is a
// macro each target expands differently, and is part of what the targets'
// streams are compared by. "#pragma clang optimize" takes effect where it is
// written, though the source is preprocessed before it is parsed. A type's
// layout is each target's own also in code compiled for every target
// (long double is 80-bit on x86-64 and 64-bit on AMDGPU).
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -disable-llvm-passes -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -O2 -x hip -disable-llvm-passes" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx1030 -fcuda-is-device -O2 -x hip -disable-llvm-passes" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx1030.bc
// RUN: FileCheck --check-prefix=HOST %s < %t.host.ll
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=GFX90A %s
// RUN: llvm-dis %t.gfx1030.bc -o - | FileCheck --check-prefix=GFX1030 %s
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=OPT %s
// RUN: FileCheck --check-prefix=OPT %s < %t.host.ll

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#if defined(__gfx1030__)
#define WS 32
#else
#define WS 64
#endif

// GFX90A-LABEL: define {{.*}}@_Z6unrollPii(
// GFX90A: !llvm.loop ![[L90A:[0-9]+]]
// GFX1030-LABEL: define {{.*}}@_Z6unrollPii(
// GFX1030: !llvm.loop ![[L1030:[0-9]+]]
__global__ void unroll(int *p, int n) {
#pragma unroll WS / 16
  for (int i = 0; i < n; ++i)
    p[i] = i * 3;
}

// OPT: define {{.*}}offPi({{.*}}) #[[OFF:[0-9]+]]
// OPT: define {{.*}}onPi({{.*}}) #[[ON:[0-9]+]]
// OPT: attributes #[[ON]] = {
// OPT-NOT: optnone
// OPT-SAME: {{[}]$}}
// OPT: attributes #[[OFF]] = {{.*}}optnone
#pragma clang optimize off
__global__ void off(int *p) { p[0] = 1; }
#pragma clang optimize on
__global__ void on(int *p) { p[0] = 2; }

// HOST-LABEL: define {{.*}}@_Z2ldv(
// HOST: ret i32 176
// GFX90A-LABEL: define {{.*}}@_Z2ldv(
// GFX90A: ret i32 88
// GFX1030-LABEL: define {{.*}}@_Z2ldv(
// GFX1030: ret i32 88
__host__ __device__ int ld() {
  return sizeof(long double) * 10 + alignof(long double);
}
__global__ void k(int *p) { *p = ld(); }
int host_ld() { return ld(); }

// GFX90A-DAG: ![[L90A]] = distinct !{![[L90A]], {{.*}}![[C90A:[0-9]+]]}
// GFX90A-DAG: ![[C90A]] = !{!"llvm.loop.unroll.count", i32 4}
// GFX1030-DAG: ![[L1030]] = distinct !{![[L1030]], {{.*}}![[C1030:[0-9]+]]}
// GFX1030-DAG: ![[C1030]] = !{!"llvm.loop.unroll.count", i32 2}
