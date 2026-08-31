// PROTOTYPE (Stage 5, Phase 5 Increment 12): a *primary* class template whose
// entire body -- not a partial specialization, and not just a single member
// -- is redeclared once per arch-#if arm (mirroring
// ggml_cuda_mma::mma_tile_sizes<DV,ncols> in fattn-mma-f16.cuh, whose six
// `using` aliases resolve to completely different tile<> specializations
// under Turing MMA / RDNA3 WMMA / MFMA / Volta). An instantiation whose
// arguments match none of the (also widened, but irrelevant here) partial
// specializations falls back to the primary template.
//
// Root cause: ClassTemplateSpecializationDecl::Profile only folds the
// ambient target variant into a specialization's identity when
// hasTargetTaggedPartialSpecialization() or hasTargetTaggedValueMember() is
// true -- neither notices that the *primary template's own redeclarations*
// are target-tagged, so exactly one ClassTemplateSpecializationDecl was
// created and reused across every real target, permanently freezing its
// members to whichever target's ambient triggered the first instantiation.
// Fixed with a third predicate, hasTargetTaggedPrimaryTemplate(), plus
// ambient-based redecl selection in getPatternForClassTemplateSpecialization's
// primary-template branch (mirroring the partial-spec branch's own
// exact-match-preferred idiom).
//
// REQUIRES: amdgpu-registered-target

// Ground truth: each arch compiled completely on its own.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx900 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX900 %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1100 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX1100 %s

// One primary (gfx942) cc1 job, with AuxGen configured for gfx900 and gfx1100
// and the full merge/reparse pipeline on -- each aux target's own bitcode
// output must see its own arm's members, not whichever arm's ambient
// happened to instantiate the primary template first.
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

#if defined(__gfx942__)
struct member_a { static constexpr int val = 40; };
struct member_b { static constexpr int val = 41; };
template <int N> struct sizes_like {
  using type_a = int;
  static constexpr int width = 4;
  using nested = member_a;
};
#elif defined(__gfx900__)
struct member_a { static constexpr int val = 40; };
struct member_b { static constexpr int val = 41; };
template <int N> struct sizes_like {
  using type_a = short;
  static constexpr int width = 8;
  using nested = member_b;
};
#elif defined(__gfx1100__)
struct member_a { static constexpr int val = 40; };
struct member_b { static constexpr int val = 41; };
template <int N> struct sizes_like {
  using type_a = long;
  static constexpr int width = 16;
  using nested = member_a;
};
#endif

__attribute__((global)) void kernel_like(int *out) {
  // Note: a bare type-only alias use (e.g. sizeof(typename
  // sizes_like<192>::type_a), no member access through it) is a known,
  // deliberately out-of-scope shape for the value/alias-member redirect
  // mechanisms below -- sizeof() bakes a QualType directly into the AST at
  // Sema time, with no Expr/VarDecl for a post-hoc redirect to key off of --
  // and is intentionally not exercised here, since the real corpus never
  // needs it (T_B_KQ is always used via a trailing member, never sizeof'd
  // bare).
  out[0] = sizes_like<192>::width;
  // Mirrors the real T_B_KQ::I shape: a value read through a *second*,
  // per-arch-divergent type alias member (`nested`), not a direct value
  // member of sizes_like<192> itself.
  out[1] = sizes_like<192>::nested::val;
}

// GFX900: store i32 8
// GFX900: store i32 41
// GFX1100: store i32 16
// GFX1100: store i32 40
// GFX942: store i32 4
// GFX942: store i32 40
