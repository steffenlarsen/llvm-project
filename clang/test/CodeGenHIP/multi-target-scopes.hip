// PROTOTYPE (Stages 1.5 and 1.6): target-dependent caches keyed by target,
// and the scopes that say which target a query is being answered for.
//
// -mllvm -multi-target-scopes puts ASTContext into multi-target mode whenever
// an aux target is configured. A target-dependent answer the two targets
// disagree about can then no longer be computed silently for whichever target
// ran last: it is answered for the target in scope, or it is a hard error.
//
// long double is the one primitive type that differs between x86_64 and
// amdgcn, so it is what these tests ask about.
//
// REQUIRES: amdgpu-registered-target

// Multi-target mode is transparent: turning it on must not change the IR.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o %t.off %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o %t.on %s -mllvm -multi-target-scopes
// RUN: diff %t.off %t.on

// -tu-target-variant=0 leaves no target in scope, reproducing what the frontend
// did before the translation-unit boundary existed. A divergent query then has
// no correct answer, and must fail loudly naming the entity rather than guess.
// RUN: not %clang_cc1 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o /dev/null %s \
// RUN:   -mllvm -multi-target-scopes -mllvm -tu-target-variant=0 2>&1 | FileCheck %s
// CHECK: no target in scope while computing the layout of 'long double'
// CHECK-SAME: differs between the targets of this compilation

// The instantiation boundary is load-bearing: the same template instantiated
// under variant 1 (the amdgcn device target) and variant 2 (the x86_64 aux
// target) computes different sizes from one source, in one AST.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s -mllvm -multi-target-scopes \
// RUN:   -mllvm -instantiate-in-variant=1 | FileCheck --check-prefix=DEVICE %s
// DEVICE: define{{.*}}@_Z5boxedIeEjv
// DEVICE: ret i32 8

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s -mllvm -multi-target-scopes \
// RUN:   -mllvm -instantiate-in-variant=2 | FileCheck --check-prefix=HOST %s
// HOST: define{{.*}}@_Z5boxedIeEjv
// HOST: ret i32 16

// A plain sizeof in a function body is *not* governed by the instantiation
// scope: substitution records it symbolically and CodeGen materialises it, so
// it answers for whatever target CodeGen runs under -- the translation unit's.
// Forcing instantiation into the host variant therefore leaves it unchanged.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s -mllvm -multi-target-scopes \
// RUN:   -mllvm -instantiate-in-variant=2 | FileCheck --check-prefix=LATE %s
// LATE: define{{.*}}@_Z5plainIeEjv
// LATE: ret i32 8

template <unsigned N> struct Box { static constexpr unsigned v = N; };

// sizeof(T) has to be evaluated to form the template argument, so it is
// evaluated during substitution, under the instantiation's target.
template <typename T> __attribute__((device)) unsigned boxed() {
  return Box<sizeof(T)>::v;
}

// sizeof(T) here is just an expression; nothing forces it to a constant until
// CodeGen.
template <typename T> __attribute__((device)) unsigned plain() {
  return sizeof(T);
}

__attribute__((device)) unsigned shared_between_targets() { return sizeof(int); }

__attribute__((device)) unsigned use() {
  return boxed<long double>() + plain<long double>();
}
