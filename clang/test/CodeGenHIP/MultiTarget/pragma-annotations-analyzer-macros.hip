// Fingerprinting pragmas, a multi-target compilation watches the tokens their
// handlers lex. Another token watcher, here the static analyzer's record of
// macro expansions, still sees only the tokens it would see without that.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -analyze -analyzer-checker=core -analyzer-output=plist \
// RUN:   -analyzer-config expand-macros=true -o %t.plist %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a"
// RUN: FileCheck %s < %t.plist
// The host's own compilation agrees.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -analyze -analyzer-checker=core -analyzer-output=plist \
// RUN:   -analyzer-config expand-macros=true -o %t.host.plist %s
// RUN: FileCheck %s < %t.host.plist

#define DEREF(p) _Pragma("clang diagnostic push") *(p) _Pragma("clang diagnostic pop")
int g() {
  int *p = 0;
  return DEREF(p);
}

// CHECK: <key>name</key><string>DEREF(p)</string>
// CHECK-NEXT: <key>expansion</key><string>*(p )</string>
