// A class the targets define differently, with a member class each target's
// definition declares alike. A member alias naming that member class is
// parsed again for the devices, where it names the member class of their own
// definition, which the host's stands for.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++20 -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc
// RUN: FileCheck --check-prefix=HOST %s < %t.host.ll
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=DEV %s
// The targets' own compilations agree.
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -x hip -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=HOST %s
// RUN: %clang_cc1 -std=c++20 %{hip_device} %{host_aux_triple} \
// RUN:   -target-cpu gfx90a -emit-llvm -o - %s | FileCheck --check-prefix=DEV %s

#define __device__ __attribute__((device))
#define __host__ __attribute__((host))

#if defined(__HIP_DEVICE_COMPILE__)
class pool { public: struct Inner; Inner *a(); int x; int y; int z; };
#else
class pool { public: struct Inner; Inner *a(); int x; };
#endif
class user {
  using Inner = pool::Inner;
  pool impl;
  Inner *p = nullptr;
public:
  bool eq(const user &o) const { return this == &o; }
};

__host__ __device__ int size() { return sizeof(user); }
// HOST-LABEL: define {{.*}} @_Z4sizev(
// HOST: ret i32 16
// DEV-LABEL: define {{.*}} @_Z4sizev(
// DEV: ret i32 24

__attribute__((device, used)) int use() { return size(); }
int host_use() { return size(); }
