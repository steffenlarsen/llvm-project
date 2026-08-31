// A bare reference to a target-tagged struct/class resolves to the primary
// target's copy of that class. This checks that the class's own members
// (fields, methods) stay reachable through that reference: a divergent
// caller compiled for a non-primary target must still be able to
// construct/copy an instance of an otherwise-shared class whose method
// body diverges but whose fields do not.
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
