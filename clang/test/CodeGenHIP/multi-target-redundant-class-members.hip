// A function that uses a divergent entity is re-parsed once per aux target;
// when every copy agrees with the host's, the host's copy is kept and the
// others are marked redundant, together with everything nested in them such
// as a lambda's closure class. Templates the redundant copies instantiated
// with their own closure type still look up the closure's members (its
// captures, call operator and implicitly declared members) within it, and
// must find them. CodeGen must then emit the specializations the kept copy
// instantiated, which mangle the same as the redundant copies' ones. This
// follows HeCBench nosync's std::async call ("no member named '' in '(lambda
// at ...)'", "type '(lambda at ...)' does not provide a call operator", then
// "undefined reference to `main::{lambda()#1}::operator()() const'").
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang -x hip --offload-arch=gfx90a -nogpulib -nogpuinc \
// RUN:   -fintegrated-hip-device-codegen -fsyntax-only -Xclang -verify %s
// RUN: %clang -x hip --offload-arch=gfx90a --offload-arch=gfx942 \
// RUN:   -nogpulib -nogpuinc -fintegrated-hip-device-codegen -fsyntax-only \
// RUN:   -Xclang -verify %s

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx942.bc \
// RUN:   -mllvm -multi-target-codegen \
// RUN:   -mllvm -multi-target-record \
// RUN:   -mllvm -parse-all-target-alternatives \
// RUN:   -mllvm -widen-target-alternatives \
// RUN:   -mllvm -allow-target-variant-decls \
// RUN:   -mllvm -reparse-divergent-users \
// RUN:   -mllvm -merge-equivalent-variants
// RUN: FileCheck -check-prefix=HOST %s < %t.host.ll

// expected-no-diagnostics

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define HD __host__ __device__

HD int divergent(int x) {
#ifdef __HIP_DEVICE_COMPILE__
  return x + 1;
#else
  return x;
#endif
}

template <class F> struct holder {
  F f;
  holder(F &&g) : f(static_cast<F &&>(g)) {}
  int call() { return f(); }
};

template <class F> int run(F &&f) {
  holder<F> h(static_cast<F &&>(f));
  holder<F> h2(static_cast<holder<F> &&>(h));
  return h2.call();
}

int use(int n) {
  int x = divergent(n);
  return run([=] { return x + 1; });
}

// HOST-LABEL: define {{.*}} @_ZN6holderIZ3useiEUlvE_E4callEv(
// HOST: call {{.*}} @_ZZ3useiENKUlvE_clEv(
// HOST-LABEL: define {{.*}} @_ZZ3useiENKUlvE_clEv(
// HOST: add nsw i32 %{{.*}}, 1
