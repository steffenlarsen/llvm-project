// A host function parsed in a device's alternative uses that device's
// specializations, as its class's members do. The class template differs in
// layout, so each target has its own specializations.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -fsyntax-only -verify %s \
// RUN:   "-multi-target-aux-invocation=-std=c++17 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip"

// expected-no-diagnostics

#ifdef __HIP_DEVICE_COMPILE__
#define IF_ON_HOST(X)
#else
#define IF_ON_HOST(X) X
#endif

#ifndef GUARD
#define GUARD
namespace ns {
template <class T, class... P> class View {
public:
  View() = default;
  View(const char *label, int n) { IF_ON_HOST((void)label;) (void)n; }
#ifdef __HIP_DEVICE_COMPILE__
  int pad;
#endif
  int extent() const { return 0; }
};
struct Space {};
class Token {
  View<unsigned *, Space> m_locks;
public:
  Token(int n) { m_locks = View<unsigned *, Space>("locks", n); }
  int size() const { return m_locks.extent(); }
};
} // namespace ns
#endif

int use() { ns::Token t(3); return t.size(); }
