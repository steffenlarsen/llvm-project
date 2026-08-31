// A device-primary compile whose devices carry the host as aux triple, as the
// driver's device invocations do. The host is no target of the compile: it is
// not recorded as one, and builtins of the aux triple (x86 here, from a host
// header) are checked as the aux triple's, not as the first aux device's.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target
//
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu \
// RUN:   -target-cpu gfx90a -fcuda-is-device -x hip -emit-llvm -o %t.gfx90a.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1100 -internal-isystem %resource_dir" \
// RUN:   -multi-target-aux-output=2:%t.gfx1100.bc \
// RUN: -mllvm -dump-conditional-regions \
// RUN:   2>&1 | FileCheck --check-prefix=REC %s
// RUN: FileCheck %s < %t.gfx90a.ll
// RUN: llvm-dis %t.gfx1100.bc -o - | FileCheck %s

// REC: multi-target recording:
// REC-NOT: x86_64
// REC: amdgcn-amd-amdhsa:
// REC-NOT: x86_64
// REC: shared tokens:

#include <mmintrin.h>

inline void host_only() { _mm_empty(); }

// The host's long double has a 64-bit mantissa, the devices' 53 bits.
template <class T> struct lim;
template <> struct lim<long double> {
#if __LDBL_MANT_DIG__ == 64
  static constexpr bool extended = true;
#endif
  static constexpr int digits = __LDBL_MANT_DIG__;
};

// CHECK-LABEL: define {{.*}}@_Z1kPi(
// CHECK: store i32 53
__attribute__((global)) void k(int *o) { o[0] = lim<long double>::digits; }
