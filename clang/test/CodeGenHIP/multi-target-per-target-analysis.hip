// Code written once for every target that depends on what differs between
// them: the layout of a class (or class template) whose fields differ per
// architecture or between the host and the devices, a constant that selects
// a template specialization (rocWMMA's arch selection) where a kernel names
// the template itself, a host/device constant evaluated while host/device
// code is analysed, and explicit instantiations of templates that depend on
// the architecture. Each target compiles what it would compile on its own.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx942.bc
// RUN: FileCheck -check-prefix=HOST %s < %t.host.ll
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=GFX90A %s
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefix=GFX942 %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

namespace std {
template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { using type = T; };
template <bool B, class T = void>
using enable_if_t = typename enable_if<B, T>::type;
} // namespace std

// A class whose fields differ per architecture.
struct S {
#if defined(__gfx90a__)
  int a[9];
#elif defined(__gfx942__)
  int a[94];
#else
  int a[1];
#endif
};
__global__ void k_sizeof(int *o) { o[0] = sizeof(S); }
// GFX90A-LABEL: define {{.*}} @_Z8k_sizeofPi(
// GFX90A: store i32 36,
// GFX942-LABEL: define {{.*}} @_Z8k_sizeofPi(
// GFX942: store i32 376,
__global__ void k_local(int *o) { S s; o[0] = sizeof(s.a); }
// GFX90A-LABEL: define {{.*}} @_Z7k_localPi(
// GFX90A: store i32 36,
// GFX942-LABEL: define {{.*}} @_Z7k_localPi(
// GFX942: store i32 376,
__global__ void k_stride(S *p, int *o) { o[0] = p[1].a[0]; }
// GFX90A-LABEL: define {{.*}} @_Z8k_strideP1SPi(
// GFX90A: getelementptr inbounds nuw i8, ptr addrspace(1) %p.coerce, i64 36
// GFX942-LABEL: define {{.*}} @_Z8k_strideP1SPi(
// GFX942: getelementptr inbounds nuw i8, ptr addrspace(1) %p.coerce, i64 376
__device__ S g;
__global__ void k_global(int *o) { o[0] = sizeof(g); }
// GFX90A-LABEL: define {{.*}} @_Z8k_globalPi(
// GFX90A: store i32 36,
// GFX942-LABEL: define {{.*}} @_Z8k_globalPi(
// GFX942: store i32 376,
template <class T> __device__ int size_of() { return sizeof(T); }
__global__ void k_template_arg(int *o) { o[0] = size_of<S>(); }
// GFX90A-LABEL: define {{.*}} @_Z14k_template_argPi(
// GFX90A: store i32 36,
// GFX942-LABEL: define {{.*}} @_Z14k_template_argPi(
// GFX942: store i32 376,
template <class T> __device__ int in_template() {
  S s{};
  return sizeof(s.a) + sizeof(T);
}
__global__ void k_in_template(int *o) { o[0] = in_template<char>(); }
// GFX90A-LABEL: define {{.*}} @_Z13k_in_templatePi(
// GFX90A: store i32 37,
// GFX942-LABEL: define {{.*}} @_Z13k_in_templatePi(
// GFX942: store i32 377,

// A class template whose fields differ per architecture.
template <int N> struct T {
#if defined(__gfx90a__)
  int a[9];
#elif defined(__gfx942__)
  int a[94];
#else
  int a[1];
#endif
};
__global__ void k_class_template(int *o) { o[0] = sizeof(T<0>); }
// GFX90A-LABEL: define {{.*}} @_Z16k_class_templatePi(
// GFX90A: store i32 36,
// GFX942-LABEL: define {{.*}} @_Z16k_class_templatePi(
// GFX942: store i32 376,
template <class U> __device__ int class_template_in_template() {
  return sizeof(T<1>);
}
__global__ void k_class_template_in_template(int *o) {
  o[0] = class_template_in_template<int>();
}
// GFX90A-LABEL: define {{.*}} @_Z28k_class_template_in_templatePi(
// GFX90A: store i32 36,
// GFX942-LABEL: define {{.*}} @_Z28k_class_template_in_templatePi(
// GFX942: store i32 376,

// A class whose copies differ only in member function bodies, each copy with
// its own copy of a nested class (rocRAND's lfsr113_engine), lays out alike
// on every target.
class engine {
public:
  struct state_t { int z; };
  __host__ __device__ engine(int s) { seed(s); }
  __host__ __device__ void seed(int s) {
#ifdef __HIP_DEVICE_COMPILE__
    st.z = s + 1;
#else
    st.z = s + 2;
#endif
  }
  state_t st;
};
__host__ __device__ void init(int s, engine *e) { *e = engine(s); }
__global__ void k_engine(engine *e) { init(1, e); }
// GFX90A-LABEL: define {{.*}} @_Z8k_engineP6engine(
// GFX90A: store i32 2,
// GFX942-LABEL: define {{.*}} @_Z8k_engineP6engine(
// GFX942: store i32 2,

// A class whose fields differ between the host and the devices only.
struct P {
#if defined(__HIP_DEVICE_COMPILE__)
  int a[9];
#else
  int a[1];
#endif
};
__host__ __device__ int host_device_size() { return sizeof(P); }
__global__ void k_host_device(int *o) { o[0] = host_device_size(); }
// GFX90A-LABEL: define {{.*}} @_Z13k_host_devicePi(
// GFX90A: store i32 36,
// GFX942-LABEL: define {{.*}} @_Z13k_host_devicePi(
// GFX942: store i32 36,

// rocWMMA's arch selection, named directly in a kernel.
struct Constants {
#if defined(__gfx90a__)
  static constexpr unsigned ARCH = 0x90a;
#elif defined(__gfx942__)
  static constexpr unsigned ARCH = 0x942;
#else
  static constexpr unsigned ARCH = 0;
#endif
};
template <class X, unsigned G = Constants::ARCH, class E = void> struct mfma {
  __device__ static int exec() { return 1; }
};
template <class X, unsigned G>
struct mfma<X, G, std::enable_if_t<G == 0x90a>> {
  __device__ static int exec() { return 900; }
};
template <class X, unsigned G>
struct mfma<X, G, std::enable_if_t<G == 0x942>> {
  __device__ static int exec() { return 942; }
};
__global__ void k_default_arg(int *o) { o[0] = mfma<float>::exec(); }
// GFX90A-LABEL: define {{.*}} @_Z13k_default_argPi(
// GFX90A: store i32 900,
// GFX942-LABEL: define {{.*}} @_Z13k_default_argPi(
// GFX942: store i32 942,
__global__ void k_explicit_arg(int *o) {
  o[0] = mfma<float, Constants::ARCH>::exec();
}
// GFX90A-LABEL: define {{.*}} @_Z14k_explicit_argPi(
// GFX90A: store i32 900,
// GFX942-LABEL: define {{.*}} @_Z14k_explicit_argPi(
// GFX942: store i32 942,
template <class X, unsigned G = Constants::ARCH> __device__ int arch() {
  return G;
}
__global__ void k_function_default_arg(int *o) { o[0] = arch<float>(); }
// GFX90A-LABEL: define {{.*}} @_Z22k_function_default_argPi(
// GFX90A: store i32 2314,
// GFX942-LABEL: define {{.*}} @_Z22k_function_default_argPi(
// GFX942: store i32 2370,

// Evaluated while host/device code is analysed.
#if defined(__HIP_DEVICE_COMPILE__)
constexpr int W = 100;
#else
constexpr int W = 200;
#endif
template <int N> struct tag { static constexpr int v = N; };
__host__ __device__ int bound() { int a[W]; return sizeof(a) / 4; }
__host__ __device__ int argument() { return tag<W>::v; }
__host__ __device__ int constexpr_local() { constexpr int x = W; return x; }
__global__ void k_bound(int *o) { o[0] = bound(); }
// GFX90A-LABEL: define {{.*}} @_Z7k_boundPi(
// GFX90A: store i32 100,
// GFX942-LABEL: define {{.*}} @_Z7k_boundPi(
// GFX942: store i32 100,
__global__ void k_argument(int *o) { o[0] = argument(); }
// GFX90A-LABEL: define {{.*}} @_Z10k_argumentPi(
// GFX90A: store i32 100,
// GFX942-LABEL: define {{.*}} @_Z10k_argumentPi(
// GFX942: store i32 100,
__global__ void k_constexpr_local(int *o) { o[0] = constexpr_local(); }
// GFX90A-LABEL: define {{.*}} @_Z17k_constexpr_localPi(
// GFX90A: store i32 100,
// GFX942-LABEL: define {{.*}} @_Z17k_constexpr_localPi(
// GFX942: store i32 100,
int host_uses() { return bound() + argument() + constexpr_local(); }
// HOST-LABEL: define {{.*}} @_Z9host_usesv(
// HOST: ret i32 600

// Explicit instantiations.
template <int B> __global__ void k_explicit_instantiation(int *o) {
  o[0] = sizeof(S) / B;
}
template __global__ void k_explicit_instantiation<4>(int *);
// GFX90A-LABEL: define {{.*}} @_Z24k_explicit_instantiationILi4EEvPi(
// GFX90A: store i32 9,
// GFX942-LABEL: define {{.*}} @_Z24k_explicit_instantiationILi4EEvPi(
// GFX942: store i32 94,

