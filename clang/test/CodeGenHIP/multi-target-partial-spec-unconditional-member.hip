// PROTOTYPE (Stage 5, Phase 5 Increment 6): a class template partial
// specialization whose body is entirely gated by an arch-#if (mirroring
// ggml_cuda_mma::tile<I_,J_,T,DATA_LAYOUT_I_MAJOR>) is widened by Increment 6
// into one ClassTemplatePartialSpecializationDecl per target arm. A member
// declared *before* the #if -- unconditional in every arm, like tile<>'s own
// `I`/`J`/`dl` -- is duplicated once per arm; DeclFingerprint.cpp's
// mergeWidenedAlternative then reconciles the duplicates by unclaiming one
// arm's copy back to TargetVariant 0 (the survivor) and tagging the other
// arm's own copy TargetVariantRedundant in place -- but the two copies are
// each the *only* physical declaration on their own arm (they are never both
// children of the same redecl), so InstantiateClassImpl's pre-existing
// skip-if-redundant logic (written for a different, in-place duplication
// case -- see multi-target-class-template-value-member.hip's `ne`) silently
// dropped the arm whose own copy lost the reconciliation, producing
// "no member named 'I'"/"'J'" for that arch alone.
//
// Root cause / fix: InstantiateClassImpl's member loop must only skip a
// TargetVariantRedundant member when a live, non-redundant duplicate of the
// same name is *also* a child of the same Pattern (Design 1's in-place
// duplication case). When no such duplicate exists in the pattern itself,
// Pattern's own (merely mis-tagged) copy is the only declaration of that
// member and must be instantiated normally rather than skipped.
//
// REQUIRES: amdgpu-registered-target

// Ground truth: each arch compiled completely on its own.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx900 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX900 %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1100 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX1100 %s

// One primary (gfx942) cc1 job, with AuxGen configured for gfx900 and gfx1100
// and the full merge/reparse pipeline on -- each aux target's own bitcode
// output must still see `I`/`J`, not just its own arch's `ne`.
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

template <int I_, int J_, typename T> struct tile_like {};

// The one and only partial specialization; I/J are unconditional (declared
// before the #if), mirroring tile<>'s own I/J/dl -- ne is the arch-divergent
// part that makes this partial spec's whole body widen per Increment 6.
template <int I_, int J_>
struct tile_like<I_, J_, int> {
  static constexpr int I = I_;
  static constexpr int J = J_;
#if defined(__gfx942__)
  static constexpr int ne = 4;
#elif defined(__gfx900__)
  static constexpr int ne = 8;
#elif defined(__gfx1100__)
  static constexpr int ne = 16;
#endif
};

__attribute__((global)) void kernel_like(int *out) {
  out[0] = tile_like<16, 16, int>::I;
  out[1] = tile_like<16, 16, int>::J;
  out[2] = tile_like<16, 16, int>::ne;
}

// GFX900: store i32 16
// GFX900: store i32 16
// GFX900: store i32 8
// GFX1100: store i32 16
// GFX1100: store i32 16
// GFX1100: store i32 16
// GFX942: store i32 16
// GFX942: store i32 16
// GFX942: store i32 4
