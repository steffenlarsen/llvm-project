// A multi-target compilation preprocesses its source once for every target.
// -multi-target-preprocessed-output prints what each pass produced exactly as
// -E would for that target, including the pragmas a parser handles while the
// compilation lexes, which -E prints as written.
//
// REQUIRES: amdgpu-registered-target

// RUN: rm -rf %t && split-file %s %t
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -emit-llvm -o %t/host.ll %t/main.hip \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t/gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx942" \
// RUN:   -multi-target-aux-output=3:%t/gfx942.bc \
// RUN:   -multi-target-preprocessed-output=1:%t/host.hipi \
// RUN:   -multi-target-preprocessed-output=2:%t/gfx90a.hipi \
// RUN:   -multi-target-preprocessed-output=3:%t/gfx942.hipi

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -E -o %t/host.ref %t/main.hip
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu \
// RUN:   -target-cpu gfx90a -fcuda-is-device -x hip -E -o %t/gfx90a.ref %t/main.hip
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu \
// RUN:   -target-cpu gfx942 -fcuda-is-device -x hip -E -o %t/gfx942.ref %t/main.hip
// RUN: diff %t/host.ref %t/host.hipi
// RUN: diff %t/gfx90a.ref %t/gfx90a.hipi
// RUN: diff %t/gfx942.ref %t/gfx942.hipi

// RUN: FileCheck --check-prefixes=CHECK,HOST %s < %t/host.hipi
// RUN: FileCheck --check-prefixes=CHECK,GFX90A %s < %t/gfx90a.hipi
// RUN: FileCheck --check-prefixes=CHECK,GFX942 %s < %t/gfx942.hipi

// Pragmas the preprocessor handles itself are printed as -E prints them, or
// not at all.
// CHECK-NOT: #pragma once
// CHECK: #pragma clang diagnostic push
// CHECK-NOT: GCC poison
// CHECK-NOT: push_macro
// CHECK-NOT: pop_macro
// Those a parser handles, with or without leaving tokens behind, as written.
// CHECK: #pragma GCC visibility push(default)
// CHECK: #pragma pack(push, 4)
// CHECK: #pragma comment(lib, "m")
// CHECK: #pragma clang optimize on
// CHECK: #pragma STDC FP_CONTRACT ON
// CHECK: #pragma something_unknown 1 2 3
// CHECK: #pragma clang something_unknown_too
// HOST: const int arch = 1;
// GFX90A: const int arch = 90;
// GFX942: const int arch = 942;
// The identifier #pragma unused puts back is not printed again.
// CHECK: #pragma unused(unused_var)
// CHECK-NEXT: #pragma unroll 4
// CHECK: #pragma clang loop vectorize(enable)
// CHECK: #pragma unroll{{$}}
// CHECK: #pragma clang loop unroll(enable)
// CHECK: #pragma GCC visibility pop
// CHECK: #pragma clang diagnostic pop

//--- once.h
#pragma once
#define ONCE_INCLUDED 1

//--- main.hip
#include "once.h"
#include "once.h"
#define __global__ __attribute__((global))
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma GCC visibility push(default)
#define DO_PRAGMA(x) _Pragma(#x)
#define UNROLL4 _Pragma("unroll 4")
#pragma pack(push, 4)
struct packed_t { char c; int i; };
#pragma pack(pop)
#pragma comment(lib, "m")
#pragma clang optimize on
#pragma STDC FP_CONTRACT ON
#pragma something_unknown 1 2 3
#pragma clang something_unknown_too
#pragma GCC poison never_used_identifier
#pragma push_macro("ONCE_INCLUDED")
#pragma pop_macro("ONCE_INCLUDED")
__global__ void k(int *o, int n) {
#if defined(__gfx90a__)
  const int arch = 90;
#elif defined(__gfx942__)
  const int arch = 942;
#else
  const int arch = 1;
#endif
  int unused_var = 0;
#pragma unused(unused_var)
  UNROLL4
  for (int i = 0; i < n; ++i) o[i] = arch;
  DO_PRAGMA(clang loop vectorize(enable))
  for (int i = 0; i < n; ++i) o[i] += 1;
#pragma unroll
  for (int i = 0; i < 4; ++i) o[i] *= 2;
#pragma clang \
  loop unroll(enable)
  for (int i = 0; i < n; ++i) o[i] -= 3;
}
#pragma GCC visibility pop
#pragma clang diagnostic pop
