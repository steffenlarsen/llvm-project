// Checks target-dependent caches keyed by target, and the scopes that say
// which target a query is being answered for.
//
// -mllvm -multi-target-scopes puts ASTContext into multi-target mode whenever
// an aux target is configured. A target-dependent answer the two targets
// disagree about can then no longer be computed silently for whichever target
// ran last: it is answered for the target in scope.
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
