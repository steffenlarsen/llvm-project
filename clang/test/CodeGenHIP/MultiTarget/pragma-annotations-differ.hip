// A pragma or #embed that means something different to the host than to the
// device is the host's on the host and the device's on the device, as in
// separate compilations. Their annotation tokens look alike in the token
// streams, as the meaning is in a payload, so the merge compares what each
// pragma consumed and what each #embed read instead.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: rm -rf %t && split-file %s %t
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -Wno-c++26-extensions -O1 -x hip \
// RUN:   -emit-llvm -o %t/host.ll %t/main.hip \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -Wno-c++26-extensions -O1 -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t/gfx90a.bc
// RUN: llvm-dis %t/gfx90a.bc -o - | FileCheck --check-prefix=DEV %t/main.hip
// RUN: FileCheck --check-prefix=HOST %t/main.hip < %t/host.ll

// Each target's copy of a class declared under a different #pragma pack has
// that target's packing.
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -Wno-c++26-extensions -x hip \
// RUN:   -ast-dump %t/main.hip \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -Wno-c++26-extensions -target-cpu gfx90a" \
// RUN:   | FileCheck --check-prefix=AST %t/main.hip

//--- a.bin
AAAA
//--- b.bin
BBBBBBBB
//--- main.hip
#define __device__ __attribute__((device))
#define __host__ __attribute__((host))

// The pragma differs.
__host__ __device__ float zz_fp(float a, float b, float c) {
#if defined(__HIP_DEVICE_COMPILE__)
#pragma clang fp contract(fast)
#else
#pragma clang fp contract(off)
#endif
  return a * b + c;
}
// DEV-LABEL: define {{.*}} @_Z5zz_fpfff(
// DEV: fmul contract float
// HOST-LABEL: define {{.*}} @_Z5zz_fpfff(
// HOST: fmul float

// The pragma reads alike, but a macro it expands differs.
#if defined(__HIP_DEVICE_COMPILE__)
#define ZZ_CONTRACT fast
#else
#define ZZ_CONTRACT off
#endif
__host__ __device__ float zz_fp_macro(float a, float b, float c) {
#pragma clang fp contract(ZZ_CONTRACT)
  return a * b + c;
}
// DEV-LABEL: define {{.*}} @_Z11zz_fp_macrofff(
// DEV: fmul contract float
// HOST-LABEL: define {{.*}} @_Z11zz_fp_macrofff(
// HOST: fmul float

// The embedded file differs. The data is each file's text, newline included.
__host__ __device__ int zz_embed() {
  static const char d[] = {
#if defined(__HIP_DEVICE_COMPILE__)
#embed "b.bin"
#else
#embed "a.bin"
#endif
  };
  return sizeof(d) * 100 + d[0];
}
// DEV-LABEL: define {{.*}} @_Z8zz_embedv(
// DEV: ret i32 966
// HOST-LABEL: define {{.*}} @_Z8zz_embedv(
// HOST: ret i32 565

// The file is the same, but how much of it is embedded differs.
__host__ __device__ int zz_embed_limit() {
  static const char d[] = {
#if defined(__HIP_DEVICE_COMPILE__)
#embed "b.bin" limit(2)
#else
#embed "b.bin" limit(6)
#endif
  };
  return sizeof(d);
}
// DEV-LABEL: define {{.*}} @_Z14zz_embed_limitv(
// DEV: ret i32 2
// HOST-LABEL: define {{.*}} @_Z14zz_embed_limitv(
// HOST: ret i32 6

#if defined(__HIP_DEVICE_COMPILE__)
#pragma pack(push, 1)
#else
#pragma pack(push, 8)
#endif
struct zz_S { char c; int i; };
#pragma pack(pop)
__host__ __device__ int zz_size() { return sizeof(zz_S); }
// DEV-LABEL: define {{.*}} @_Z7zz_sizev(
// DEV: ret i32 5
// HOST-LABEL: define {{.*}} @_Z7zz_sizev(
// HOST: ret i32 8
// AST: CXXRecordDecl {{.*}} struct zz_S definition {{.*}}targets={host}
// AST: MaxFieldAlignmentAttr {{.*}} Implicit 64
// AST: CXXRecordDecl {{.*}} struct zz_S definition {{.*}}targets={gfx90a}
// AST: MaxFieldAlignmentAttr {{.*}} Implicit 8
