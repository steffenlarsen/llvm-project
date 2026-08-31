// Tests that with -multi-target-aux-invocation, each aux target is recorded
// with its own invocation, so it sees its own predefined macros rather than
// the host primary's. Host and device disagree on __NO_MATH_ERRNO__ and
// __cpp_threadsafe_static_init.
//
// REQUIRES: amdgpu-registered-target

// Ground truth: the device compiled on its own.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu \
// RUN:   -target-cpu gfx90a -fcuda-is-device -fno-threadsafe-statics \
// RUN:   -x hip -emit-llvm -o - %s | FileCheck %s

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -emit-llvm -o %t.host.ll -fmath-errno %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -fno-threadsafe-statics -x hip" \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -fno-threadsafe-statics -x hip" \
// RUN:   -mllvm -multi-target-codegen \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx942.bc \
// RUN:   -mllvm -multi-target-record \
// RUN:   -mllvm -reparse-divergent-users \
// RUN:   -mllvm -parse-all-target-alternatives \
// RUN:   -mllvm -widen-target-alternatives \
// RUN:   -mllvm -allow-target-variant-decls
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck %s
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck %s

#if defined(__NO_MATH_ERRNO__) && !defined(__cpp_threadsafe_static_init)
__attribute__((device)) int device_macros(int x) { return x + 1; }
#else
__attribute__((device)) int device_macros(int x) { return x + 2; }
#endif

// CHECK: add nsw i32 %{{.*}}, 1
