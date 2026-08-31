// PROTOTYPE (N-way alignRecordings generalization): the same round-trip and
// alignment-reporting checks as multi-target-merge.hip, but with 3 real
// device-arch recordings instead of 1 host + 1 device, exercising the N-way
// path (alignRecordings no longer rejects Recordings.size() != 2) with real
// TargetInfos, not synthetic recordings.
//
// The primary compile is gfx942; -multi-target-aux-target configures two more
// real device-arch TargetInfos (gfx900, gfx1100) sharing the primary's
// amdgcn-amd-amdhsa triple, matching how -offload-arch=gfx900,gfx1100,gfx942
// would really be split into per-arch jobs. No -aux-triple/host recording is
// configured here, so this is a pure N=3 (3 total, 2 aux) test.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o %t.direct %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o %t.merged %s \
// RUN:   -mllvm -multi-target-record \
// RUN:   -mllvm -multi-target-aux-target=2:gfx900: \
// RUN:   -mllvm -multi-target-aux-target=3:gfx1100:
// RUN: diff %t.direct %t.merged

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 \
// RUN:   -fcuda-is-device -x hip -fsyntax-only %s \
// RUN:   -mllvm -multi-target-record \
// RUN:   -mllvm -multi-target-aux-target=2:gfx900: \
// RUN:   -mllvm -multi-target-aux-target=3:gfx1100: \
// RUN:   -mllvm -dump-conditional-regions 2>&1 | FileCheck %s
// CHECK: multi-target recording:
// CHECK-DAG: amdgcn-amd-amdhsa: {{[0-9]+}} tokens
// The per-arch function below diverges across all 3 targets, so this must
// no longer collapse to "1 shared, 0 alternative" the way the N=2 all-shared
// test does -- confirming the N-way alignment path actually ran.
// CHECK: segments: {{[0-9]+}} shared, {{[1-9][0-9]*}} alternative

#if defined(__gfx942__)
__attribute__((device)) int arch_specific(int x) { return x + 942; }
#elif defined(__gfx900__)
__attribute__((device)) int arch_specific(int x) { return x + 900; }
#elif defined(__gfx1100__)
__attribute__((device)) int arch_specific(int x) { return x + 1100; }
#else
__attribute__((device)) int arch_specific(int x) { return x; }
#endif

__attribute__((device)) int shared_between_targets(int x) {
  return x * 2 + sizeof(int);
}
