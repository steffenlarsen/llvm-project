// Tests that AuxGen emits each aux target with its own
// -multi-target-aux-invocation's CodeGenOptions and per-target LangOptions
// rather than the host primary's. An aux invocation's device libraries are
// linked into that target, and its CodeGenOptions also drive that target's
// backend.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -fvisibility=protected -mframe-pointer=all -x hip" \
// RUN:   -mllvm -multi-target-codegen \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck --check-prefix=INVOCATION %s
// RUN: FileCheck --check-prefix=HOST %s < %t.host.ll

// RUN: echo 'int lib_fn(int x) { return x * 3; }' | %clang_cc1 -triple amdgcn-amd-amdhsa \
// RUN:   -x c -emit-llvm-bc -o %t.lib.bc -
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -emit-llvm -o %t.host-lib.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -flto=full -flto-unit -mlink-builtin-bitcode %t.lib.bc -x hip" \
// RUN:   -mllvm -multi-target-codegen \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a-lib.bc
// RUN: llvm-dis -o - %t.gfx90a-lib.bc | FileCheck --check-prefix=LIB %s
// RUN: FileCheck --check-prefix=HOST-LIB %s < %t.host-lib.ll

__attribute__((device)) int device_fn(int x) { return x + 1; }

extern "C" __attribute__((device)) int lib_fn(int x);
__attribute__((device)) int calls_lib_fn(int x) { return lib_fn(x); }

int host_fn(int x) { return x + 2; }

// INVOCATION: define protected {{.*}}i32 @_Z9device_fni(i32 {{.*}}) #[[ATTRS:[0-9]+]]
// INVOCATION: attributes #[[ATTRS]] = {{{.*}}"frame-pointer"="all"

// HOST: define dso_local {{.*}}i32 @_Z7host_fni(
// HOST-NOT: "frame-pointer"="all"

// LIB: define internal {{.*}}i32 @lib_fn(i32 {{.*}}) #[[LIB_ATTRS:[0-9]+]]
// LIB: attributes #[[LIB_ATTRS]] = {{{.*}}"target-cpu"="gfx90a"
// LIB: !{i32 1, !"ThinLTO", i32 0}

// HOST-LIB-NOT: @lib_fn
// HOST-LIB-NOT: !"ThinLTO"
