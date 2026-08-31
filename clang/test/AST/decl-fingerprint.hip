// PROTOTYPE (Stage 4): the verification harness for multi-target ASTs.
//
// Once one AST carries declarations for several targets, byte-identical output
// against an unmodified compiler is no longer available as a check. What has to
// hold instead is that the declarations belonging to a target are the ones that
// target's own compilation produces, and -mllvm -dump-decl-fingerprints is how
// that is compared: one normalised line per declaration, sorted, so neither
// pointer values nor declaration order can make two equal ASTs look different.
//
// A comparison harness is only worth having if it fails when it should, so most
// of this file is negative controls.
//
// REQUIRES: amdgpu-registered-target

// The dump is deterministic: same input, same output.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu \
// RUN:   -fcuda-is-device -x hip -fsyntax-only %s -mllvm -dump-decl-fingerprints=0 > %t.a
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu \
// RUN:   -fcuda-is-device -x hip -fsyntax-only %s -mllvm -dump-decl-fingerprints=0 > %t.b
// RUN: diff %t.a %t.b

// An added declaration is a difference.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu \
// RUN:   -fcuda-is-device -x hip -fsyntax-only %s -DEXTRA \
// RUN:   -mllvm -dump-decl-fingerprints=0 > %t.extra
// RUN: not diff %t.a %t.extra

// So is a changed type...
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu \
// RUN:   -fcuda-is-device -x hip -fsyntax-only %s -DRETYPE \
// RUN:   -mllvm -dump-decl-fingerprints=0 > %t.retype
// RUN: not diff %t.a %t.retype

// ...and a definition becoming a bare declaration, which no name-only
// comparison would catch.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu \
// RUN:   -fcuda-is-device -x hip -fsyntax-only %s -DNODEF \
// RUN:   -mllvm -dump-decl-fingerprints=0 > %t.nodef
// RUN: not diff %t.a %t.nodef

// Variant filtering: declarations marked for one target appear only under that
// target, and unmarked ones appear under every target. The dump is sorted, so
// presence is checked with -DAG and absence with a separate grep.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu \
// RUN:   -fcuda-is-device -x hip -fsyntax-only %s -DVARIANTS \
// RUN:   -mllvm -allow-target-variant-decls -mllvm -dump-decl-fingerprints=1 > %t.v1
// RUN: FileCheck --check-prefix=V1 --input-file=%t.v1 %s
// RUN: not grep 'tile_C.*long' %t.v1
// V1-DAG: Typedef{{.*}}tile_C{{.*}}int
// V1-DAG: Function{{.*}}shared_decl

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu \
// RUN:   -fcuda-is-device -x hip -fsyntax-only %s -DVARIANTS \
// RUN:   -mllvm -allow-target-variant-decls -mllvm -dump-decl-fingerprints=2 > %t.v2
// RUN: FileCheck --check-prefix=V2 --input-file=%t.v2 %s
// RUN: not grep 'tile_C.*int' %t.v2
// V2-DAG: Typedef{{.*}}tile_C{{.*}}long
// V2-DAG: Function{{.*}}shared_decl

// Variant 0 means "every target", and shows both.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu \
// RUN:   -fcuda-is-device -x hip -fsyntax-only %s -DVARIANTS \
// RUN:   -mllvm -allow-target-variant-decls -mllvm -dump-decl-fingerprints=0 \
// RUN:   | FileCheck --check-prefix=V0 %s
// V0-DAG: Typedef{{.*}}tile_C{{.*}}int
// V0-DAG: Typedef{{.*}}tile_C{{.*}}long

__attribute__((device)) int base(int x) { return x; }

template <typename T> __attribute__((device)) T tmpl(T x) { return x + x; }
__attribute__((device)) int use() { return tmpl<int>(1); }

#ifdef EXTRA
__attribute__((device)) int extra() { return 0; }
#endif

#ifdef RETYPE
__attribute__((device)) long retyped() { return 0; }
#else
__attribute__((device)) int  retyped() { return 0; }
#endif

#ifdef NODEF
__attribute__((device)) int maybe_defined();
#else
__attribute__((device)) int maybe_defined() { return 0; }
#endif

#ifdef VARIANTS
typedef int  tile_C;
typedef long tile_C;
__attribute__((device)) int shared_decl() { return 0; }
#endif
