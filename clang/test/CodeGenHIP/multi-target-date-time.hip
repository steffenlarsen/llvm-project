// __DATE__ and __TIME__ expand to a token spelled in scratch space and carrying
// no literal data. Inside a region that differs per target, a device target's
// expansion still compiles to the date and time, not to a failure to lex
// "__TIME__".
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -emit-llvm -o %t.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx942.bc
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefixes=CHECK,GFX90A %s
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck --check-prefixes=CHECK,GFX942 %s

// CHECK-DAG: c"{{[A-Z][a-z][a-z] [ 1-3][0-9] [0-9][0-9][0-9][0-9]}}\00"
// CHECK-DAG: c"{{[0-9][0-9]:[0-9][0-9]:[0-9][0-9]}}\00"
// GFX90A: store i32 1,
// GFX942: store i32 2,

#if defined(__gfx942__)
#define X 2
#else
#define X 1
#endif

__attribute__((device)) const char *date(int *p) { *p = X; return __DATE__; }
__attribute__((device)) const char *time(int *p) { *p = X; return __TIME__; }

__attribute__((global)) void kernel(const char **out, int *p) {
  out[0] = date(p);
  out[1] = time(p);
}
