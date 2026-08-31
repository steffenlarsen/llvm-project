// String literals spelled in a macro body (or by #) and used only inside a
// device-only #if region. Those tokens come from an aux target's recording,
// and their translated location is the macro's expansion site, which spells
// the macro name rather than the literal. Sema re-lexes some literals from
// their location (inline-asm operands, format strings), so the literal must
// still be spelled wherever its location points.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang -x hip --offload-arch=gfx90a -nogpulib -nogpuinc \
// RUN:   -fintegrated-hip-device-codegen -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck %s

extern "C" __attribute__((device)) int printf(const char *, ...);

#define LOAD_ZERO(r) asm volatile("v_mov_b32 %0, 0" : "=v"(r))
#define FMT "value %d\n"
#define STR(x) #x

__attribute__((device)) unsigned f() {
  unsigned result;
#ifdef __HIP_DEVICE_COMPILE__
  LOAD_ZERO(result);
  // CHECK: warning: format specifies type 'int' but the argument has type 'double'
  printf(FMT, 1.0);
  // CHECK: warning: format specifies type 'int' but the argument has type 'double'
  printf(STR(v %d), 1.0);
#endif
  return result;
}
