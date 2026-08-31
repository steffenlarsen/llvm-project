// Copies of a class that one device architecture declares differently are
// kept apart where what they declare differs: an overload, a constructor, a
// conversion, a member type or class, a virtual function, or what a member
// using-declaration brings in, also in a class template. Code naming the class
// is analysed for each device with its own copy, also in a kernel whose
// linkage specification has no braces.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx942.bc
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefixes=CHECK,GFX90A %s
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck --check-prefixes=CHECK,GFX942 %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

struct Overload {
#if defined(__gfx942__)
  __device__ int g(long) { return 1; }
#endif
  __device__ int g(int) { return 2; }
};
// CHECK-LABEL: define {{.*}}@overload(
// GFX90A: store i32 2,
// GFX942: store i32 1,
extern "C" __global__ void overload(int *p) { Overload m; *p = m.g(5L); }

template <class T> __device__ int use(T t) { Overload m; return m.g(t); }
// CHECK-LABEL: define {{.*}}@overload_in_template(
// GFX90A: store i32 2,
// GFX942: store i32 1,
extern "C" __global__ void overload_in_template(int *p) { *p = use(5L); }

struct Ctor {
  int v;
#if defined(__gfx942__)
  __device__ Ctor(long) : v(1) {}
#endif
  __device__ Ctor(int) : v(2) {}
};
// CHECK-LABEL: define {{.*}}@ctor(
// GFX90A: store i32 2,
// GFX942: store i32 1,
extern "C" __global__ void ctor(int *p) { Ctor x(7L); *p = x.v; }

struct Conversion {
#if defined(__gfx942__)
  __device__ operator long() const { return 1; }
#endif
  __device__ operator int() const { return 2; }
};
__device__ long take(long v) { return v; }
// CHECK-LABEL: define {{.*}}@conversion(
// GFX90A: store i32 2,
// GFX942: store i32 1,
extern "C" __global__ void conversion(int *p) { Conversion w; *p = take(w); }

struct MemberType {
#if defined(__gfx942__)
  using v = double;
  struct In { int a, b; };
#else
  using v = float;
  struct In { int a; };
#endif
};
// CHECK-LABEL: define {{.*}}@member_type(
// GFX90A: store i32 4,
// GFX942: store i32 8,
extern "C" __global__ void member_type(int *p) { *p = sizeof(MemberType::v); }
// CHECK-LABEL: define {{.*}}@member_class(
// GFX90A: store i32 4,
// GFX942: store i32 8,
extern "C" __global__ void member_class(int *p) { *p = sizeof(MemberType::In); }

#if defined(__gfx942__)
enum class Enum : long { A = 1 };
#else
enum class Enum : char { A = 1 };
#endif
// CHECK-LABEL: define {{.*}}@enum_underlying(
// GFX90A: store i32 1,
// GFX942: store i32 8,
extern "C" __global__ void enum_underlying(int *p) { *p = sizeof(Enum); }

struct Virtual {
#if defined(__gfx942__)
  __device__ virtual int extra() { return 99; }
#endif
  __device__ virtual int f() { return 1; }
};
__device__ int call(Virtual *b) { return b->f(); }
// The call loads the second slot where the first is another function's.
// CHECK-LABEL: define {{.*}}@virtual_slot(
// GFX90A: %[[VT:.*]] = load ptr addrspace(1), ptr addrspace(1) %b.coerce
// GFX90A-NEXT: load ptr addrspace(1), ptr addrspace(1) %[[VT]]
// GFX942: %[[VT:.*]] = load ptr addrspace(1), ptr addrspace(1) %b.coerce
// GFX942-NEXT: getelementptr inbounds nuw i8, ptr addrspace(1) %[[VT]], i64 8
extern "C" __global__ void virtual_slot(int *p, Virtual *b) { *p = call(b); }

struct A { __device__ long f(long) { return 1; } };
struct B { __device__ int f(int) { return 2; } };
#if defined(__gfx942__)
struct UsingMember : A, B { using A::f; };
#else
struct UsingMember : A, B { using B::f; };
#endif
// CHECK-LABEL: define {{.*}}@using_member(
// GFX90A: store i32 6,
// GFX942: store i32 9,
extern "C" __global__ void using_member(int *p) {
  UsingMember d;
  *p = d.f(3) + sizeof(d.f(3));
}

template <class T> struct TA { __device__ long f(long) { return 1; } };
template <class T> struct TB { __device__ int f(int) { return 2; } };
template <class T> struct UsingDependent : TA<T>, TB<T> {
#if defined(__gfx942__)
  using TA<T>::f;
#else
  using TB<T>::f;
#endif
};
// CHECK-LABEL: define {{.*}}@using_dependent(
// GFX90A: store i32 6,
// GFX942: store i32 9,
extern "C" __global__ void using_dependent(int *p) {
  UsingDependent<int> d;
  *p = d.f(3) + sizeof(d.f(3));
}

#if defined(__gfx942__)
template <class T> struct UsingInTemplate : A, B { using A::f; };
#else
template <class T> struct UsingInTemplate : A, B { using B::f; };
#endif
// CHECK-LABEL: define {{.*}}@using_in_template(
// GFX90A: store i32 6,
// GFX942: store i32 9,
extern "C" __global__ void using_in_template(int *p) {
  UsingInTemplate<int> d;
  *p = d.f(3) + sizeof(d.f(3));
}

// A member function reads its own class copy's layout, so it is kept apart
// with the class.
struct Layout {
#if defined(__gfx942__)
  long pad;
#else
  int pad;
#endif
  int v;
  __device__ int size() { return sizeof(pad); }
  __device__ int get() { return v; }
};
// CHECK-LABEL: define {{.*}}@member_reads_layout(
// GFX90A: store i32 5,
// GFX942: store i32 9,
extern "C" __global__ void member_reads_layout(int *p) {
  Layout l;
  *p = l.size() + 1;
}
// CHECK-LABEL: define {{.*}}@member_reads_field(
// GFX90A: getelementptr inbounds nuw i8, ptr addrspace(1) %{{.*}}, i64 4
// GFX942: getelementptr inbounds nuw i8, ptr addrspace(1) %{{.*}}, i64 8
extern "C" __global__ void member_reads_field(int *p, Layout *l) {
  *p = l->get();
}

#if defined(__gfx942__)
struct UsingLayout : Layout { using Layout::size; };
#else
struct UsingLayout : Layout { using Layout::size; };
#endif
// CHECK-LABEL: define {{.*}}@using_member_reads_layout(
// GFX90A: store i32 5,
// GFX942: store i32 9,
extern "C" __global__ void using_member_reads_layout(int *p) {
  UsingLayout l;
  *p = l.size() + 1;
}
