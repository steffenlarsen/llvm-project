// #pragma clang optimize, #pragma clang section and #pragma comment take
// effect where they are lexed, which can be anywhere, an enum body, a
// parameter list, a template argument list or the middle of an expression. A
// multi-target compilation applies them where the parser's lexing reaches
// them in any of those places, as each target's own compilation does.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -O1 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -O1 -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc
// RUN: FileCheck --check-prefix=HOST %s < %t.host.ll
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=DEV %s
// The targets' own compilations agree.
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -O1 -x hip -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=HOST %s
// RUN: %clang_cc1 -std=c++17 %{hip_device} %{host_aux_triple} -O1 \
// RUN:   -target-cpu gfx90a -emit-llvm -o - %s | FileCheck --check-prefix=DEV %s

#define __device__ __attribute__((device))
#define __host__ __attribute__((host))

// Globals come first in the IR, the attribute groups last.
// DEV: @zz_after_params = {{.*}} #[[PARAMS:[0-9]+]]
// HOST: @zz_init = {{.*}} 3,

// In an enum body. An optnone function keeps its parameter in an alloca.
enum E { A,
#pragma clang optimize off
         B };
__host__ __device__ int zz_in_enum(int x) { return x + B; }
#pragma clang optimize on
// DEV-LABEL: define {{.*}} @_Z10zz_in_enumi(
// DEV-NEXT: entry:
// DEV-NEXT: alloca
// HOST-LABEL: define {{.*}} @_Z10zz_in_enumi(
// HOST-NEXT: entry:
// HOST-NEXT: alloca

// In a parameter list.
int zz_params(int a,
#pragma clang section bss = ".params_bss"
              int b);
__device__ int zz_after_params;
#pragma clang section bss = ""

// In an initializer. Only the host records libraries.
int zz_init = 1 +
#pragma comment(lib, "initlib")
    2;

// In a template argument list. The function it is in has been declared.
template <int N> struct T { static constexpr int v = N; };
__host__ __device__ int zz_targ() { return T<
#pragma clang optimize off
    3>::v; }
__host__ __device__ int zz_targ_after(int x) { return x * 2; }
#pragma clang optimize on
// DEV-LABEL: define {{.*}} @_Z7zz_targv(
// DEV-NOT: alloca
// DEV: ret i32 3
// DEV-LABEL: define {{.*}} @_Z13zz_targ_afteri(
// DEV-NEXT: entry:
// DEV-NEXT: alloca
// HOST-LABEL: define {{.*}} @_Z7zz_targv(
// HOST-NOT: alloca
// HOST: ret i32 3
// HOST-LABEL: define {{.*}} @_Z13zz_targ_afteri(
// HOST-NEXT: entry:
// HOST-NEXT: alloca

// In the middle of a statement, for the devices only.
__host__ __device__ int zz_stmt(int x) {
  int y = x
#if defined(__HIP_DEVICE_COMPILE__)
#pragma clang optimize off
#endif
      + 1;
  return y;
}
__host__ __device__ int zz_stmt_after(int x) { return x * 3; }
#if defined(__HIP_DEVICE_COMPILE__)
#pragma clang optimize on
#endif
// DEV-LABEL: define {{.*}} @_Z7zz_stmti(
// DEV-NOT: alloca
// DEV: ret i32
// DEV-LABEL: define {{.*}} @_Z13zz_stmt_afteri(
// DEV-NEXT: entry:
// DEV-NEXT: alloca
// HOST-LABEL: define {{.*}} @_Z7zz_stmti(
// HOST-NOT: alloca
// HOST: ret i32
// HOST-LABEL: define {{.*}} @_Z13zz_stmt_afteri(
// HOST-NOT: alloca
// HOST: ret i32

__attribute__((device, used)) int use(int x) {
  return zz_in_enum(x) + zz_targ() + zz_targ_after(x) + zz_stmt(x) +
         zz_stmt_after(x) + zz_after_params;
}
int host_use(int x) {
  return zz_in_enum(x) + zz_targ() + zz_targ_after(x) + zz_stmt(x) +
         zz_stmt_after(x) + zz_init;
}

// DEV: attributes #[[PARAMS]] = { "bss-section"=".params_bss" }
// HOST: !{!"initlib"}
