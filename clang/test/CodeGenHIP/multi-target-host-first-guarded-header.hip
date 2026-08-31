// An include-guarded header first included only for the host (as
// amd_math_functions.h does with <assert.h>), then included again for every
// target. The second inclusion is empty for the host, so its declarations form
// an episode with only the device arms. Those arms agree with each other, but
// promoting their copy to shared would make it visible to the host next to the
// host's own copy from the first inclusion: "call to 'guarded_fn' is
// ambiguous". Needs at least two device archs, or there is nothing to merge.
//
// REQUIRES: amdgpu-registered-target

// RUN: rm -rf %t && split-file %s %t
// RUN: %clang -x hip --offload-arch=gfx90a --offload-arch=gfx942 \
// RUN:   -nogpulib -nogpuinc -fintegrated-hip-device-codegen -fsyntax-only \
// RUN:   -Xclang -verify %t/main.hip

//--- guarded.h
#ifndef GUARDED_H
#define GUARDED_H
extern "C" {
void guarded_fn(int);
}
struct GuardedStruct {
  int x;
};
extern int guarded_var;
#endif

//--- main.hip
// expected-no-diagnostics
#if !__HIP_DEVICE_COMPILE__
#include "guarded.h"
#endif
int separates_the_two_regions;
#include "guarded.h"

void use(int x) {
  guarded_fn(x);
  GuardedStruct S{guarded_var};
  (void)S;
}
