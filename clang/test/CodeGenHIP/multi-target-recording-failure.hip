// A multi-target compilation preprocesses its source once for every target.
// Each target's preprocessing reports its own diagnostics, as compiling for it
// alone would. If it fails for one target, the compilation fails with that
// target's errors instead of compiling every target from the primary target's
// tokens, and links no device code.
//
// REQUIRES: amdgpu-registered-target

// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -emit-llvm -o %t.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx942.bc \
// RUN:   -mllvm -multi-target-package-fatbin=%t.hipfb 2>&1 | FileCheck %s

// CHECK: multi-target-recording-failure.hip:[[@LINE+10]]:2: warning: "only gfx90a gets here"
// CHECK: 1 warning generated when compiling for gfx90a.
// CHECK: multi-target-recording-failure.hip:[[@LINE+5]]:2: error: "not supported on gfx942"
// CHECK: 1 error generated when compiling for gfx942.
// CHECK: error: cannot compile for multiple targets at once: preprocessing for 'gfx942' failed
// CHECK-NOT: error:
#if defined(__gfx942__)
#error "not supported on gfx942"
#endif
#if defined(__gfx90a__)
#warning "only gfx90a gets here"
#endif

__attribute__((global)) void kernel(int *out) { *out = 1; }
