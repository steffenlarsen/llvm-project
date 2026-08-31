// A bare reference to a target-tagged struct/class normally resolves to the
// primary target's copy (see multi-target-scopes.hip's struct-identity
// note), but that must not apply to a CXXRecordDecl's own
// injected-class-name: re-parsing an out-of-line member definition (e.g.
// std::complex<T>::operator=) for a non-primary target must resolve
// 'complex' to the RecordDecl copy currently being re-parsed, not get
// redirected to the primary target's copy.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang -x hip -fsyntax-only --offload-arch=gfx90a --offload-arch=gfx942 \
// RUN:   -fintegrated-hip-device-codegen \
// RUN:   -mllvm -multi-target-record \
// RUN:   -mllvm -parse-all-target-alternatives \
// RUN:   -mllvm -allow-target-variant-decls \
// RUN:   %s

#include <type_traits>

#if defined(__gfx942__)
__attribute__((device)) int arch_pick(int x) { return x + 942; }
#elif defined(__gfx90a__)
__attribute__((device)) int arch_pick(int x) { return x + 90; }
#endif
