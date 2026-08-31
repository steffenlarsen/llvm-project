// PROTOTYPE: an explicit class template specialization whose template argument
// is a fundamental type (e.g. `template<> struct traits<double> { ... };`, the
// shape libstdc++'s `numeric_limits<long double>` takes) tripped two independent
// bugs once its body genuinely diverges per target and gets widened:
//
// 1. isTagBrace (TokenStreamMerge.cpp) walks backward from the specialization's
//    opening brace tolerating `identifier`/`coloncolon`/`less`/`greater`/some
//    fundamental-type keywords, to recognize `Name < Args > {` as a tag body
//    rather than a function body. `kw_double` (and float/void/wchar_t/char8_t/
//    char16_t/char32_t) were missing from that list, so the scan hit its
//    `default: return false` case and misclassified the brace as a function
//    body. That made chunk()'s cut-legality check treat the closing `}` alone
//    (without its trailing `;`) as a legal split point, producing "expected
//    ';' after struct" and "expected unqualified-id" for the following target.
//
// 2. Sema::ActOnClassTemplateSpecialization's redefinition check looks up the
//    prior specialization via ClassTemplateDecl::findSpecialization, which is
//    keyed by template arguments in a FoldingSet, not by target -- so it always
//    hands back the *first* target's already-complete definition, regardless
//    of which target is currently being parsed. Without a target-variant guard
//    (mirroring the one in CheckForFunctionRedefinition and the primary
//    class-template path in CheckClassTemplate), the second target's
//    identical-looking definition was flagged as a self-referential
//    "redefinition of 'traits<double>'".
//
// Modeled on multi-target-class-template-value-member.hip's cc1-direct
// pattern: driver-level `%clang -x hip` tests route through the mandatory
// HIP runtime-wrapper preinclude, which pulls in libstdc++'s own
// numeric_limits machinery and hits the separate, pre-existing "ambiguous
// __numeric_traits_floating" bug -- unrelated to either fix under test here.
//
// REQUIRES: amdgpu-registered-target

// Ground truth: each arch compiled completely on its own.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx90a \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX90A %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX942 %s

// One primary (gfx942) cc1 job, with AuxGen configured for gfx90a and the
// full merge/reparse pipeline on -- each aux target's own bitcode output
// must have traits<double>::value computed for its own target, not the
// primary's.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o %t.primary.ll %s \
// RUN:   -mllvm -multi-target-codegen \
// RUN:   -mllvm -multi-target-aux-target=2:gfx90a: \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   -mllvm -multi-target-record \
// RUN:   -mllvm -parse-all-target-alternatives \
// RUN:   -mllvm -widen-target-alternatives \
// RUN:   -mllvm -allow-target-variant-decls \
// RUN:   -mllvm -reparse-divergent-users \
// RUN:   -mllvm -merge-equivalent-variants
// RUN: FileCheck -check-prefix=GFX942 %s < %t.primary.ll
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=GFX90A %s

template <typename T> struct traits {
  static constexpr int value = 0;
};

template <> struct traits<double> {
#if defined(__gfx942__)
  static constexpr int value = 942;
#else
  static constexpr int value = 90;
#endif
};

// GFX90A: store i32 90
// GFX942: store i32 942
__attribute__((global)) void kernel_like(int *out) {
  out[0] = traits<double>::value;
}
