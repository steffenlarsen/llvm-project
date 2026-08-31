// A divergent region inside a class body widens to the whole class, and
// trimming then re-splits it at declaration ends. The class's closing `}` is
// not one -- its `;` is -- so the class head must be recognised as a tag
// even when it has template arguments that are not plain types (as in
// rocprim's `lookback_scan_state<T, UseSleep, false>` or thrust's sort.h) or a
// GNU attribute before its name (as export macros expand to). Otherwise the
// `;` is split off into a segment of its own: "expected ';' after struct".
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang -x hip --offload-arch=gfx90a -nogpulib -nogpuinc \
// RUN:   -fintegrated-hip-device-codegen -fsyntax-only -Xclang -verify %s
// RUN: %clang -x hip --offload-arch=gfx90a --offload-arch=gfx942 \
// RUN:   -nogpulib -nogpuinc -fintegrated-hip-device-codegen -fsyntax-only \
// RUN:   -Xclang -verify %s

// expected-no-diagnostics

#if __HIP_DEVICE_COMPILE__
#define DIV 1
#else
#define DIV 0
#endif

template <class T, bool B> struct BoolArg;
template <class T> struct BoolArg<T, false> {
  int get() {
#if DIV
    return 1;
#else
    return 2;
#endif
  }
};

template <class T, unsigned N> struct IntArg;
template <class T> struct IntArg<T, 4> {
#if DIV
  int a;
#else
  long a;
#endif
};

template <class T> struct Wrap {};
template <class T, unsigned N> struct Base {};
template <class T> struct Nested;
template <class T> struct Nested<Wrap<Wrap<T>>> : Base<T, (sizeof(T) > 2)> {
#if DIV
  int a;
#else
  long a;
#endif
};

struct Plain {};
struct __attribute__((visibility("default"))) Attr : public Plain {
  int get() {
#if DIV
    return 1;
#else
    return 2;
#endif
  }
};
