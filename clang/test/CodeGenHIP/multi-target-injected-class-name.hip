// PROTOTYPE: isVisibleForTarget's PreferPrimaryForRecordReferences special case
// (SemaLookup.cpp) forces a bare reference to a target-tagged struct/class to mean
// the primary target's copy -- see multi-target-scopes.hip's own struct-identity
// note. That special case must not also apply to a CXXRecordDecl's own
// injected-class-name: re-parsing an out-of-line member definition (e.g.
// std::complex<T>::operator=) for a non-primary target looks up 'complex' via
// member lookup already scoped to the one, already-reentered RecordDecl copy
// being re-parsed, and must agree with that copy, not get redirected to the
// unrelated primary-target RecordDecl whose own injected name isn't reachable
// from here. Without the fix, every out-of-line template member definition in
// libstdc++'s <complex> (which force_cuda_host_device-wraps its entire
// #include_next) fails with "no template named 'complex'" for every
// non-primary target once real header content is widened/re-parsed.
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
