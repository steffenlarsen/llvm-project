// A class whose targets differ only in the bodies of member functions is one
// class for every target. Each member that differs keeps one body per target,
// for constructors, destructors, conversion functions and members of class
// templates too. A constructor's braced member initializers stay part of it.
// A class whose data members differ keeps a copy per target.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx942" \
// RUN:   -multi-target-aux-output=3:%t.gfx942.bc \
// RUN:   -mllvm -trace-tagged-decl=Shared 2>&1 | FileCheck -check-prefix=TAGS %s \
// RUN:   --implicit-check-not="ClassTemplate Shared" --implicit-check-not="CXXRecord Shared"
// RUN: llvm-dis -o %t.gfx90a.ll %t.gfx90a.bc
// RUN: llvm-dis -o %t.gfx942.ll %t.gfx942.bc
// RUN: FileCheck -check-prefix=LAYOUT -DV=4 %s < %t.host.ll
// RUN: FileCheck -check-prefix=LAYOUT -DV=4 %s < %t.gfx90a.ll
// RUN: FileCheck -check-prefix=LAYOUT -DV=8 %s < %t.gfx942.ll
// RUN: FileCheck -check-prefix=CTOR -DV=7 %s < %t.host.ll
// RUN: FileCheck -check-prefix=DTOR -DV=11 %s < %t.host.ll
// RUN: FileCheck -check-prefix=GET -DV=3 %s < %t.host.ll
// RUN: FileCheck -check-prefix=CONV -DV=2.500000e-01 %s < %t.host.ll
// RUN: FileCheck -check-prefix=TMPL -DV=1000 %s < %t.host.ll
// RUN: FileCheck -check-prefix=CTOR -DV=90 %s < %t.gfx90a.ll
// RUN: FileCheck -check-prefix=DTOR -DV=13 %s < %t.gfx90a.ll
// RUN: FileCheck -check-prefix=GET -DV=5 %s < %t.gfx90a.ll
// RUN: FileCheck -check-prefix=CONV -DV=5.000000e-01 %s < %t.gfx90a.ll
// RUN: FileCheck -check-prefix=TMPL -DV=2000 %s < %t.gfx90a.ll
// RUN: FileCheck -check-prefix=CTOR -DV=42 %s < %t.gfx942.ll
// RUN: FileCheck -check-prefix=DTOR -DV=17 %s < %t.gfx942.ll
// RUN: FileCheck -check-prefix=GET -DV=6 %s < %t.gfx942.ll
// RUN: FileCheck -check-prefix=CONV -DV=7.500000e-01 %s < %t.gfx942.ll
// RUN: FileCheck -check-prefix=TMPL -DV=3000 %s < %t.gfx942.ll

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#if defined(__gfx942__)
#define PICK(H, D90A, D942) D942
#elif defined(__HIP_DEVICE_COMPILE__)
#define PICK(H, D90A, D942) D90A
#else
#define PICK(H, D90A, D942) H
#endif

__device__ int dtor_mark;

// Only the members whose bodies differ have a copy per target, never the class.
// TAGS-DAG: tag v1 CXXConstructor SharedS
// TAGS-DAG: tag v1 CXXDestructor ~SharedS
struct SharedS {
  int v;
  int w;
  __host__ __device__ SharedS() : v{1}, w{2} { v = PICK(7, 90, 42); }
  __host__ __device__ ~SharedS() { dtor_mark = PICK(11, 13, 17); }
  __host__ __device__ int get() const { return v * PICK(3, 5, 6); }
  __host__ __device__ operator float() const { return v + PICK(0.25f, 0.5f, 0.75f); }
};

template <int N> struct SharedT {
  __host__ __device__ int f(int x) const { return x * PICK(1000, 2000, 3000) + N; }
};

struct LayoutL {
  int a;
#if defined(__gfx942__)
  int b;
#endif
  __host__ __device__ int size() const { return sizeof(*this); }
};

__global__ void kernel(int *o, float *f) {
  SharedS s;
  o[0] = s.get();
  f[0] = s;
  o[1] = SharedT<4>().f(o[2]);
  o[3] = LayoutL().size();
}

int host_use(int x) {
  SharedS s;
  return s.get() + SharedT<4>().f(x) + (int)(float)s + LayoutL().size();
}

// Each function's body for each target, whatever order they are emitted in.
// CTOR-LABEL: define {{.*}} @_ZN7SharedSC2Ev(
// CTOR: store i32 [[V]],
// DTOR-LABEL: define {{.*}} @_ZN7SharedSD2Ev(
// DTOR: store i32 [[V]],
// GET-LABEL: define {{.*}} @_ZNK7SharedS3getEv(
// GET: mul nsw i32 %{{.*}}, [[V]]
// CONV-LABEL: define {{.*}} @_ZNK7SharedScvfEv(
// CONV: fadd {{.*}}float %{{.*}}, [[V]]
// TMPL-LABEL: define {{.*}} @_ZNK7SharedTILi4EE1fEi(
// TMPL: mul nsw i32 %{{.*}}, [[V]]
// LAYOUT-LABEL: define {{.*}} @_ZNK7LayoutL4sizeEv(
// LAYOUT: ret i32 [[V]]
