// Members whose declarations differ between targets, in shapes a member
// cannot be re-parsed on its own: a field of a class template, a field after
// another member, and a static data member after an access specifier. The
// template's field and the static data member were silently the host's on
// every device, and uses of the class's field were ambiguous.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -std=c++17 -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a -O2 -std=c++17" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1030 -O2 -std=c++17" \
// RUN:   -multi-target-aux-output=3:%t.gfx1030.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1100 -O2 -std=c++17" \
// RUN:   -multi-target-aux-output=4:%t.gfx1100.bc
// RUN: FileCheck --check-prefix=HOST %s < %t.host.ll
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=GFX9 %s
// RUN: llvm-dis %t.gfx1030.bc -o - | FileCheck --check-prefix=RDNA %s
// RUN: llvm-dis %t.gfx1100.bc -o - | FileCheck --check-prefix=RDNA %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))
#define __shared__ __attribute__((shared))

#if defined(__GFX9__)
typedef long sidet;
#else
typedef int sidet;
#endif
static const int w = sizeof(sidet);

template <int N> struct field_tmpl {
  static const int one = N;
  sidet x;
  int pad;
};

template <int N> struct after_access {
public:
  static const int a = w * N;
};

struct part {
#if defined(__GFX9__)
  int a;
#endif
  int b;
};
struct outer { int z; part s; };

// GFX9-LABEL: define {{.*}}@_Z1kPi(
// GFX9: store i32 16,
// GFX9: store i32 24,
// GFX9: store i32 12,
// RDNA-LABEL: define {{.*}}@_Z1kPi(
// RDNA: store i32 8,
// RDNA: store i32 12,
// RDNA: store i32 8,
// HOST-LABEL: define {{.*}}@_Z8host_usePi(
// HOST: store i32 8,
// HOST: store i32 12,
// HOST: store i32 8,
__global__ void k(int *p) {
  p[0] = sizeof(field_tmpl<3>);
  p[1] = after_access<3>::a;
  outer o{};
  o.s.b = 9;
  p[2] = sizeof(outer);
}
void host_use(int *p) {
  p[0] = sizeof(field_tmpl<3>);
  p[1] = after_access<3>::a;
  outer o{};
  o.s.b = 9;
  p[2] = sizeof(outer);
}
