// Distinct from multi-target-class-template-value-member.hip: there,
// tile_like<...>::ne is read through a qualified name from a caller that is
// never itself arch-#if-divergent. Here, a function template whose own body
// is arch-#if-divergent declares a local `tile<64,64> t;` in each branch and
// reads `t.ne` off that local -- the call shape ggml_cuda_mma::tile<> uses
// inside real per-arch dispatch code. Checks that each branch's local
// `t.ne` read resolves to that branch's own target's value, not whichever
// target's Sema pass happened to run last in the same cc1 invocation.
//
// REQUIRES: amdgpu-registered-target

// Ground truth: each arch compiled completely on its own.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx900 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX900 %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1100 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX1100 %s

// One primary (gfx942) cc1 job, with AuxGen configured for gfx900 and gfx1100
// and the full merge/reparse pipeline on -- each target's own output must
// have `t.ne` (and the array it sizes) computed for its own target, not
// gfx942's (the primary's) or each other's.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o %t.primary.ll %s \
// RUN:   -mllvm -multi-target-codegen \
// RUN:   -mllvm -multi-target-aux-target=2:gfx900: \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx900.bc \
// RUN:   -mllvm -multi-target-aux-target=3:gfx1100: \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx1100.bc \
// RUN:   -mllvm -multi-target-record \
// RUN:   -mllvm -parse-all-target-alternatives \
// RUN:   -mllvm -widen-target-alternatives \
// RUN:   -mllvm -allow-target-variant-decls \
// RUN:   -mllvm -reparse-divergent-users \
// RUN:   -mllvm -merge-equivalent-variants
// RUN: FileCheck -check-prefix=GFX942 %s < %t.primary.ll
// RUN: llvm-dis -o - %t.gfx900.bc | FileCheck -check-prefix=GFX900 %s
// RUN: llvm-dis -o - %t.gfx1100.bc | FileCheck -check-prefix=GFX1100 %s

// Body-divergent, plain (non-template) callee -- mirrors
// ggml_cuda_get_physical_warp_size's exact shape.
__attribute__((device)) static constexpr int get_lane_count() {
#if defined(__gfx942__)
  return 4;
#elif defined(__gfx900__)
  return 8;
#elif defined(__gfx1100__)
  return 16;
#endif
}

// Mirrors ggml_cuda_mma::tile<>'s exact shape: a static data member computed
// from the divergent callee, sizing a dependent array member.
template <int I, int J>
struct tile {
  static constexpr int ne = I * J / get_lane_count();
  int x[ne];
};

// The function template itself is arch-#if-divergent (mirroring real
// per-arch dispatch code that instantiates ggml_cuda_mma::tile<>), and each
// branch reads its own local instance's `ne` member -- this is the shape
// multi-target-class-template-value-member.hip's kernel_like does NOT
// exercise (it reads tile_like<...>::ne via a qualified name from a
// never-#if-divergent caller instead).
template <int N>
__attribute__((device)) inline int caller_tmpl() {
#if defined(__gfx942__)
  tile<64, 64> t;
  return t.ne + N;
#elif defined(__gfx900__)
  tile<64, 64> t;
  return t.ne + N + 1;
#elif defined(__gfx1100__)
  tile<64, 64> t;
  return t.ne + N + 2;
#endif
}

__attribute__((global)) void kernel_like(int *out) {
  out[0] = caller_tmpl<2>();
}

// GFX900: %struct.tile{{[.0-9]*}} = type { [512 x i32] }
// GFX900: ret i32 515
// GFX1100: %struct.tile{{[.0-9]*}} = type { [256 x i32] }
// GFX1100: ret i32 260
// GFX942: %struct.tile{{[.0-9]*}} = type { [1024 x i32] }
// GFX942: ret i32 1026
