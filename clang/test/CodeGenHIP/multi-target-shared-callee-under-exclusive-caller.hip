// A shared class-template method (never itself arch-#if-dispatched) is
// called from inside two different target-exclusive callers, each behind
// its own `#if` branch. The shared method must stay visible to every
// caller: an exclusive caller's own target tag must not get adopted onto a
// shared callee it references, since that callee is memoized as a single
// Decl and other, differently-tagged callers still need to see it.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang -x hip -fsyntax-only --offload-arch=gfx90a --offload-arch=gfx942 \
// RUN:   -fintegrated-hip-device-codegen \
// RUN:   -mllvm -multi-target-record \
// RUN:   -mllvm -parse-all-target-alternatives \
// RUN:   -mllvm -allow-target-variant-decls \
// RUN:   %s

template <typename T> struct pool {
  T val;
  __attribute__((device)) void alloc(T v) { val = v; }
};

template <int Arch>
__attribute__((device)) void run(int x) {
  pool<int> p;
  p.alloc(x);
}

#if defined(__gfx942__)
__attribute__((global)) void kernel(int x) { run<942>(x); }
#elif defined(__gfx90a__)
__attribute__((global)) void kernel(int x) { run<90>(x); }
#endif
