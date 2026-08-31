// A class template whose static data member is computed by calling a plain
// (non-template) function whose body -- not its signature -- is gated by
// target-CPU macros, mirroring llama.cpp's ggml_cuda_mma::tile<>: `static
// constexpr int ne = I_ * J_ / get_lane_count(); T x[ne];`. The primary
// template has no members of its own; every member (including `ne`) lives
// only in a partial specialization, and the specialization is never itself
// `#if`-tagged -- only the value its static member computes is
// target-divergent.
//
// `kernel_like` is a single, never-`#if`-dispatched entry point that
// instantiates the specialization and calls the divergent plain callee
// directly. Checks that each target computes its own value of `ne` (and its
// own array bound for the dependent member `x`) independently, rather than
// sharing whichever target's Sema pass instantiated the specialization
// first.
//
// REQUIRES: amdgpu-registered-target

// Ground truth: each arch compiled completely on its own.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx900 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX900 %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1100 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX1100 %s

// One primary (gfx942) cc1 job, with AuxGen configured for gfx900 and gfx1100
// and the full merge/reparse pipeline on -- each aux target's own bitcode
// output must have `ne` (and the array it sizes) computed for its own
// target, not gfx942's (the primary's) or each other's.
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

// Empty primary -- exactly ggml_cuda_mma::tile<>'s own shape: no members at
// all live here, they only live in the partial specialization below.
template <int I_, int J_, typename T> struct tile_like {};

// The one and only partial specialization, with a target-divergent value
// member: not itself target-#if-tagged (a single, unconditional definition),
// and tile_like has no other partial specialization either -- nothing about
// the specialization's own declaration is arch-divergent, only the value its
// static member computes.
template <int I_, int J_>
struct tile_like<I_, J_, int> {
  static constexpr int ne = I_ * J_ / get_lane_count();
  int x[ne];
};

// A single, never-#if-gated entry point -- no arch dispatch anywhere in this
// TU. It also calls the divergent plain callee directly, which gives it real
// per-target variance at the point it references tile_like<16,16,int>.
__attribute__((global)) void kernel_like(int *out) {
  tile_like<16, 16, int> t;
  t.x[0] = get_lane_count();
  out[0] = tile_like<16, 16, int>::ne;
}

// The array bound of `x` (sized by `ne`) must be forked per target too, not
// just the value stored into out[0].
// GFX900: %struct.tile_like{{[.0-9]*}} = type { [32 x i32] }
// GFX900: store i32 32
// GFX1100: %struct.tile_like{{[.0-9]*}} = type { [16 x i32] }
// GFX1100: store i32 16
// GFX942: %struct.tile_like{{[.0-9]*}} = type { [64 x i32] }
// GFX942: store i32 64
