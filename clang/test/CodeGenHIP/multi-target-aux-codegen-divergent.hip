// PROTOTYPE: AuxGen (-multi-target-codegen's extra CodeGenModule per aux
// target, see BackendConsumer::AuxGenEntry) re-emits whatever AST the one
// host/primary cc1 job already built -- it does not itself make device code
// that branches on a target-identity macro (e.g. `#if defined(__gfx900__)`)
// resolve differently per aux target. Only the token-recording/merge/reparse
// machinery (-multi-target-record, -reparse-divergent-users,
// -parse-all-target-alternatives, -widen-target-alternatives,
// -allow-target-variant-decls) makes each aux target's own AST reflect its
// own `#if` branch; without it, every AuxGen entry would silently reuse the
// primary's answer. This is the real -fintegrated-hip-device-codegen driver
// flag's own AuxGen+aux-output path (Clang.cpp's
// emitIntegratedHipDeviceCodegenFlags), exercised directly at the cc1 level
// against a source using genuine `#if` arch divergence rather than the
// ambient-builtin case multi-target-builtin-split.hip covers.
//
// REQUIRES: amdgpu-registered-target

// Ground truth: each arch compiled completely on its own.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx900 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX900 %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1100 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX1100 %s

// One host/primary (gfx942) cc1 job, with AuxGen configured for gfx900 and
// gfx1100 and the full merge/reparse pipeline on -- each aux target's own
// bitcode output must match that target's own ground truth above, not
// gfx942's (the primary's) or each other's.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o %t.primary.ll %s \
// RUN:   -mllvm -multi-target-codegen \
// RUN:   -mllvm -multi-target-aux-target=2:gfx900: \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx900.bc \
// RUN:   -mllvm -multi-target-aux-target=3:gfx1100: \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx1100.bc \
// RUN:   -mllvm -multi-target-record \
// RUN:   -mllvm -reparse-divergent-users \
// RUN:   -mllvm -parse-all-target-alternatives \
// RUN:   -mllvm -widen-target-alternatives \
// RUN:   -mllvm -allow-target-variant-decls
// RUN: FileCheck -check-prefix=GFX942 %s < %t.primary.ll
// RUN: llvm-dis -o - %t.gfx900.bc | FileCheck -check-prefix=GFX900 %s
// RUN: llvm-dis -o - %t.gfx1100.bc | FileCheck -check-prefix=GFX1100 %s

#if defined(__gfx942__)
__attribute__((device)) int arch_specific(int x) { return x + 942; }
#elif defined(__gfx900__)
__attribute__((device)) int arch_specific(int x) { return x + 900; }
#elif defined(__gfx1100__)
__attribute__((device)) int arch_specific(int x) { return x + 1100; }
#else
__attribute__((device)) int arch_specific(int x) { return x; }
#endif

// GFX900: add nsw i32 %{{.*}}, 900
// GFX1100: add nsw i32 %{{.*}}, 1100
// GFX942: add nsw i32 %{{.*}}, 942
