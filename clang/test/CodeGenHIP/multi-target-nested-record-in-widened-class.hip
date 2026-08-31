// A member declaration present on only one side of a class body (here a
// device-only static_assert, as in rocprim's
// ROCPRIM_DETAIL_DEVICE_STATIC_ASSERT) widens the whole class into one copy
// per target. A class nested in it and named from inside the same copy must
// resolve to that copy's own nested class rather than be pinned to the
// primary target's copy, which is not reachable from there.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang -x hip --offload-arch=gfx90a -nogpulib -nogpuinc \
// RUN:   -fintegrated-hip-device-codegen -fsyntax-only -Xclang -verify %s
// RUN: %clang -x hip --offload-arch=gfx90a --offload-arch=gfx942 \
// RUN:   -nogpulib -nogpuinc -fintegrated-hip-device-codegen -fsyntax-only \
// RUN:   -Xclang -verify %s

// expected-no-diagnostics

#if __HIP_DEVICE_COMPILE__
#define DEVICE_STATIC_ASSERT(e, m) static_assert(e, m)
#define HOST_STATIC_ASSERT(e, m)
#else
#define DEVICE_STATIC_ASSERT(e, m)
#define HOST_STATIC_ASSERT(e, m) static_assert(e, m)
#endif

class Alias {
  DEVICE_STATIC_ASSERT(true, "");
  struct S { int a; };
  using T = S;
  S field;
  int get() { S s{1}; return s.a; }
};

class DeclaredBefore {
  struct S { int a; };
  HOST_STATIC_ASSERT(true, "");
  using T = S;
};

class Outer {
  DEVICE_STATIC_ASSERT(true, "");
  struct S;
  struct Inner { S *p; };
  struct S { int a; };
};

template <class K> struct raw_storage { K k; };

template <class K, unsigned N> class Sort {
  DEVICE_STATIC_ASSERT(N % 64 == 0, "");
  union storage_type_ {
    K keys[N];
    unsigned rank[N];
  };

public:
  using storage_type = raw_storage<storage_type_>;
};

Sort<int, 256>::storage_type storage;
