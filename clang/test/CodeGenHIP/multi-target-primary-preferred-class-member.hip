// PROTOTYPE: isVisibleForTarget's PreferPrimaryForRecordReferences special case
// (SemaLookup.cpp) forces a bare reference to a target-tagged struct/class to
// resolve to the primary target's copy -- see multi-target-scopes.hip and
// multi-target-injected-class-name.hip's own notes on this mechanism. That
// forcing is correct for the type reference itself, but the class's own
// members (fields, methods) must remain reachable too: an ordinary member
// lookup inside the class the reference resolved to (e.g. a field read by an
// implicit copy constructor) must not additionally require that member's own
// target tag to match whichever target is currently ambient. Without the
// fallback in isVisibleForTarget (added right after its `V == Current`
// check), a divergent caller compiled for a non-primary target that
// constructs/copies an instance of an otherwise-shared class whose *own*
// method body diverges (but whose fields do not) fails with
// "no member named '__x' in 'my_fp8'" -- __x exists unconditionally in every
// target's copy, but visibility for it was computed relative to the wrong
// target once the class reference itself had already been redirected to the
// primary's copy.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang -x hip -fsyntax-only --offload-arch=gfx900 --offload-arch=gfx942 \
// RUN:   -fintegrated-hip-device-codegen \
// RUN:   -mllvm -multi-target-record \
// RUN:   -mllvm -parse-all-target-alternatives \
// RUN:   -mllvm -allow-target-variant-decls \
// RUN:   %s

struct my_fp8 {
  unsigned char __x;

  __attribute__((device)) __attribute__((host)) my_fp8(int val) : __x(val) {}
  my_fp8() = default;

  __attribute__((device)) __attribute__((host)) operator float() const {
#if defined(__gfx942__) && __HIP_DEVICE_COMPILE__
    return (float)__x * 2;
#else
    return (float)__x;
#endif
  }
};

#if defined(__gfx942__)
__attribute__((device)) float use_it(unsigned char b) {
  const my_fp8 xf = *reinterpret_cast<const my_fp8 *>(&b);
  return (float)xf / 2;
}
#else
__attribute__((device)) float use_it(unsigned char b) {
  return (float)b / 2;
}
#endif
