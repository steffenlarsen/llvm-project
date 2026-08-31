// PROTOTYPE: a callee's Provenance (PendingInstantiationProvenanceVariant,
// SemaExpr.cpp) records which target(s) a reference to it should be visible
// for. When the reference happens from inside an enclosing template
// instantiation, the enclosing instantiation's own target tag must only ever
// be used to *downgrade* the callee's Provenance toward 0 (shared) -- e.g.
// when a genuinely shared/ambient-forced callee is reached from an enclosing
// context that is itself shared (tag 0), the callee's raw (non-primary)
// ambient tag must not stick. It must never be *adopted* outright: a
// genuinely target-exclusive enclosing caller (tag != 0, one `#if` branch of
// a real arch-dispatch chain) can still reference a shared, memoized callee
// -- e.g. a class-template method -- that other, differently-tagged callers
// also need. Adopting the exclusive caller's tag onto that shared callee
// hides it from every other caller once instantiation memoizes the same
// Decl, producing "no member named 'alloc'" for whichever target's caller
// is instantiated second. Found via the real ggml-cuda corpus:
// ggml_cuda_pool_alloc<int>::alloc, called from within launch_fattn<...>'s
// body, was wrongly hidden from every launch_fattn<...> instantiation
// except whichever one happened to instantiate it first.
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
