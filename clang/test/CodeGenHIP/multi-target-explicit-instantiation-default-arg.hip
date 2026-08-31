// An explicit instantiation in code compiled for every target names its
// template's specialization for each target. A parameter type whose defaulted
// template argument differs between device architectures is formed for the
// target the specialization is for, and an explicit instantiation of a
// function the devices can call is each device's too. Host code launching a
// kernel whose signature names such a type passes the host's: it calls the
// host's specialization, which registers the kernel by the name every device
// defines it under.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -emit-llvm -o %t.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx942.bc
// RUN: FileCheck --check-prefix=HOST %s < %t.ll
// RUN: FileCheck --check-prefix=HOST-NAME %s < %t.ll
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=GFX90A %s
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck --check-prefix=GFX942 %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))

struct C {
#if defined(__gfx90a__)
  static constexpr unsigned ID = 0x90a;
#elif defined(__gfx942__)
  static constexpr unsigned ID = 0x942;
#else
  static constexpr unsigned ID = 0;
#endif
};
template <typename T, unsigned N = C::ID> struct M { int x; };

template <typename T> __device__ int on_device(M<T> m) { return m.x; }
template __device__ int on_device<int>(M<int>);
// GFX90A-LABEL: define {{.*}} @_Z9on_deviceIiEi1MIT_Lj2314EE(
// GFX942-LABEL: define {{.*}} @_Z9on_deviceIiEi1MIT_Lj2370EE(

template <typename T> __host__ __device__ int both(M<T> m) { return m.x; }
template __host__ __device__ int both<int>(M<int>);
// HOST-LABEL: define {{.*}} @_Z4bothIiEi1MIT_Lj0EE(
// GFX90A-LABEL: define {{.*}} @_Z4bothIiEi1MIT_Lj2314EE(
// GFX942-LABEL: define {{.*}} @_Z4bothIiEi1MIT_Lj2370EE(

template <class T> struct Box { T v; };
template <typename T> __device__ int nested(Box<M<T>> b) { return b.v.x; }
template __device__ int nested<int>(Box<M<int>>);
// GFX90A-LABEL: define {{.*}} @_Z6nestedIiEi3BoxI1MIT_Lj2314EEE(
// GFX942-LABEL: define {{.*}} @_Z6nestedIiEi3BoxI1MIT_Lj2370EEE(

template <typename T> __host__ __device__ unsigned id(T) { return C::ID; }
template __host__ __device__ unsigned id<int>(int);
// HOST-LABEL: define {{.*}} @_Z2idIiEjT_(
// HOST: ret i32 0
// GFX90A-LABEL: define {{.*}} @_Z2idIiEjT_(
// GFX90A: ret i32 2314
// GFX942-LABEL: define {{.*}} @_Z2idIiEjT_(
// GFX942: ret i32 2370

struct dim3 {
  unsigned x, y, z;
  dim3(unsigned x = 1, unsigned y = 1, unsigned z = 1) : x(x), y(y), z(z) {}
};
int hipConfigureCall(dim3, dim3, unsigned long = 0, void * = nullptr);
template <typename T> __attribute__((global)) void kernel(M<T> m, int *o) {
  *o = m.x;
}
void launch(int *o) { kernel<<<1, 1>>>(M<int>{}, o); }
// HOST-LABEL: define {{.*}} @_Z21__device_stub__kernelIiEv1MIT_L_ZN1C2IDEEEPi(
// HOST-NAME: c"_Z6kernelIiEv1MIT_L_ZN1C2IDEEEPi\00"
// GFX90A-LABEL: define {{.*}} @_Z6kernelIiEv1MIT_L_ZN1C2IDEEEPi(
// GFX942-LABEL: define {{.*}} @_Z6kernelIiEv1MIT_L_ZN1C2IDEEEPi(
