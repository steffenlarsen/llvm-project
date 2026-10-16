// End-to-end driver-field unswitching for HIP: real CIRGen output for host and
// device TUs, merged into a cir.offload.container by the merge tool with the
// pass enabled. Calling-convention lowering is deferred on both sides, as the
// driver does under -clangir-offload-merge.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -fno-clangir-call-conv-lowering \
// RUN:   -target-sdk-version=9.2 -fhip-new-launch-api -x hip -I %S/../../CodeGenCUDA/Inputs \
// RUN:   -emit-cir %s -o %t-host.cir
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx900 -target-sdk-version=9.2 \
// RUN:   -fcuda-is-device -fno-clangir-call-conv-lowering -fhip-new-launch-api -x hip \
// RUN:   -I %S/../../CodeGenCUDA/Inputs -emit-cir %s -o %t-dev.cir
// RUN: cir-offload-merge -combine -input=%t-host.cir -input=%t-dev.cir \
// RUN:   -targets=host-x86_64-unknown-linux-gnu,hip-amdgcn-amd-amdhsa--gfx900 \
// RUN:   -enable-cir-specialize-driver-fields -disable-cir-infer-launch-bounds \
// RUN:   -output=%t-combined.cir
// RUN: FileCheck %s --input-file=%t-combined.cir
// RUN: FileCheck %s --input-file=%t-combined.cir --check-prefix=DEV
// RUN: FileCheck %s --input-file=%t-combined.cir --check-prefix=VIS

// Off by default.
// RUN: cir-offload-merge -combine -input=%t-host.cir -input=%t-dev.cir \
// RUN:   -targets=host-x86_64-unknown-linux-gnu,hip-amdgcn-amd-amdhsa--gfx900 \
// RUN:   -disable-cir-infer-launch-bounds -output=%t-default.cir
// RUN: FileCheck %s --input-file=%t-default.cir --check-prefix=DEFAULT
// DEFAULT-NOT: .flags

// One field per kernel: the one tested most in loops.
// RUN: cir-opt %t-default.cir \
// RUN:   -pass-pipeline="builtin.module(cir-offload-specialize-driver-fields{max-fields=1})" \
// RUN:   -o - | FileCheck %s --check-prefix=ONE

#include "cuda.h"

extern "C" int hipMemcpyToSymbolAsync(const void *symbol, const void *src,
                                      unsigned long size, unsigned long offset,
                                      int kind, void *stream);
// Copies to a symbol too, but is not a function the pass knows to do so.
extern "C" int my_copy_to_symbol(const void *symbol, const void *src,
                                 unsigned long size, unsigned long offset,
                                 int kind, void *stream);

__constant__ unsigned long constant_buffer[64];

// `exclude` and `molecular` are flags tested in the loop; `n` is its bound,
// used other than by a test, so it is not a flag.
struct Exec {
  int exclude;
  int molecular;
  int n;
  __attribute__((visibility("hidden"))) __device__ int build(int i) const {
    int count = 0;
    for (int j = 0; j < n; ++j) {
      if (exclude && j == i)
        continue;
      if (molecular != 0)
        count += 2;
      else
        count += 1;
    }
    return count;
  }
};

struct Driver {
  Exec c;
  int *out;
  __device__ void operator()() const { out[0] = c.build(0); }
};

//===----------------------------------------------------------------------===//
// A driver copied into a __constant__ buffer
//===----------------------------------------------------------------------===//

__global__ void launch_constant() {
  const Driver &driver = *reinterpret_cast<const Driver *>(constant_buffer);
  driver();
}

// The host reads the flags from the bytes it copies, right after the copy, and
// picks the copy for their outcomes: both false, `molecular` only, `exclude`
// only, both. Anything else -- there is nothing else -- runs the original.
// CHECK-LABEL: cir.func {{.*}} @_Z12run_constantRK6DriverPv(
// CHECK:         cir.libc.memcpy {{.*}} from %[[DRV:[0-9]+]] {{.*}} to %arg1
// CHECK:         %[[B0:.*]] = cir.cast bitcast %[[DRV]] : !cir.ptr<!void> -> !cir.ptr<!u8i>
// CHECK:         %[[O0:.*]] = cir.const #cir.int<0> : !s64i
// CHECK:         %[[P0:.*]] = cir.ptr_stride %[[B0]], %[[O0]]
// CHECK:         %[[F0:.*]] = cir.cast bitcast %[[P0]] : !cir.ptr<!u8i> -> !cir.ptr<!s32i>
// CHECK:         %[[EXCLUDE:.*]] = cir.load align(1) %[[F0]] : !cir.ptr<!s32i>, !s32i
// CHECK:         %[[EXCLUDE_SET:.*]] = cir.cmp ne %[[EXCLUDE]], %{{.*}} : !s32i
// CHECK:         %[[O4:.*]] = cir.const #cir.int<4> : !s64i
// CHECK:         %[[MOLECULAR:.*]] = cir.load align(1)
// CHECK:         %[[MOLECULAR_SET:.*]] = cir.cmp ne %[[MOLECULAR]], %{{.*}} : !s32i
// CHECK:         cir.call @hipMemcpyToSymbolAsync(
// CHECK:         cir.call @__hipPushCallConfiguration(
// CHECK:         cir.not %[[EXCLUDE_SET]]
// CHECK:         cir.not %[[MOLECULAR_SET]]
// CHECK:         cir.call @_Z30__device_stub__launch_constantv.flags0()
// CHECK:         cir.call @_Z30__device_stub__launch_constantv.flags1()
// CHECK:         cir.call @_Z30__device_stub__launch_constantv.flags2()
// CHECK:         cir.call @_Z30__device_stub__launch_constantv.flags3()
// CHECK:         } else {
// CHECK-NEXT:      cir.call @_Z30__device_stub__launch_constantv() {cu.kernel_name = #cir.cu.kernel_name<"_Z15launch_constantv">
void run_constant(const Driver &driver, void *staging) {
  __builtin_memcpy(staging, &driver, sizeof(Driver));
  hipMemcpyToSymbolAsync(constant_buffer, staging, sizeof(Driver), 0, 1,
                         nullptr);
  launch_constant<<<1, 1>>>();
}

//===----------------------------------------------------------------------===//
// A driver passed by value
//===----------------------------------------------------------------------===//

__global__ void launch_by_value(const Driver driver) { driver(); }

// A by-value driver is read from a copy of its own on the host.
// CHECK-LABEL: cir.func {{.*}} @_Z12run_by_valueRK6Driver(
// CHECK:         %[[SLOT:.*]] = cir.alloca "driver" align(8) : !cir.ptr<!rec_Driver>
// CHECK:         cir.store %{{.*}}, %[[SLOT]] : !rec_Driver, !cir.ptr<!rec_Driver>
// CHECK:         cir.cast bitcast %[[SLOT]] : !cir.ptr<!rec_Driver> -> !cir.ptr<!u8i>
// CHECK:         cir.call @_Z30__device_stub__launch_by_value6Driver.flags0(
// CHECK:         cir.call @_Z30__device_stub__launch_by_value6Driver.flags3(
// CHECK:         cir.call @_Z30__device_stub__launch_by_value6Driver(
void run_by_value(const Driver &driver) { launch_by_value<<<1, 1>>>(driver); }

//===----------------------------------------------------------------------===//
// A field compared against several constants
//===----------------------------------------------------------------------===//

struct ModeExec {
  int mode;
  int n;
  int *out;
  __device__ void operator()() const {
    int count = 0;
    for (int j = 0; j < n; ++j) {
      if (mode == 2)
        count += 2;
      if (mode < 3)
        count += 1;
    }
    out[0] = count;
  }
};

__global__ void launch_modes(const ModeExec exec) { exec(); }

// `mode == 2` and `mode < 3` have three outcomes between them: below 2, 2, and
// 3 or more. So three copies, and the host evaluates both tests.
// CHECK-LABEL: cir.func {{.*}} @_Z9run_modesRK8ModeExec(
// CHECK-DAG:     cir.cmp eq %[[MODE:.*]], %{{.*}} : !s32i
// CHECK-DAG:     cir.cmp lt %[[MODE]], %{{.*}} : !s32i
// CHECK:         @_Z27__device_stub__launch_modes8ModeExec.flags0(
// CHECK:         @_Z27__device_stub__launch_modes8ModeExec.flags1(
// CHECK:         @_Z27__device_stub__launch_modes8ModeExec.flags2(
// CHECK-NOT:     @_Z27__device_stub__launch_modes8ModeExec.flags3(
// CHECK:         @_Z27__device_stub__launch_modes8ModeExec(
void run_modes(const ModeExec &exec) { launch_modes<<<1, 1>>>(exec); }

//===----------------------------------------------------------------------===//
// More flags than the copies allow
//===----------------------------------------------------------------------===//

// Four flags, tested in the loop four, three, two and once: the three tested
// most are chosen, 2^3 copies, and the fourth is left as it is.
struct ManyExec {
  int a, b, c, d;
  int n;
  int *out;
  __device__ void operator()() const {
    int count = 0;
    for (int j = 0; j < n; ++j) {
      count += a ? 1 : 0;
      count += a ? 2 : 0;
      count += a ? 3 : 0;
      count += a ? 4 : 0;
      count += b ? 1 : 0;
      count += b ? 2 : 0;
      count += b ? 3 : 0;
      count += c ? 1 : 0;
      count += c ? 2 : 0;
      count += d ? 1 : 0;
    }
    out[0] = count;
  }
};

__global__ void launch_many(const ManyExec exec) { exec(); }

// CHECK-LABEL: cir.func {{.*}} @_Z8run_manyRK8ManyExec(
// CHECK:         @_Z26__device_stub__launch_many8ManyExec.flags7(
// CHECK-NOT:     @_Z26__device_stub__launch_many8ManyExec.flags8(
// CHECK:         @_Z26__device_stub__launch_many8ManyExec(
void run_many(const ManyExec &exec) { launch_many<<<1, 1>>>(exec); }

// With one field allowed, only `a`, tested most, gets copies.
// ONE-LABEL: cir.func {{.*}} @_Z8run_manyRK8ManyExec(
// ONE:         @_Z26__device_stub__launch_many8ManyExec.flags1(
// ONE-NOT:     @_Z26__device_stub__launch_many8ManyExec.flags2(
// ONE:         @_Z26__device_stub__launch_many8ManyExec(

// Three outcomes of `mode` times two of `x` times two of `y` is more copies
// than allowed, so `y`, tested least, is dropped: 3 * 2 = 6 copies.
struct MixExec {
  int mode, x, y;
  int n;
  int *out;
  __device__ void operator()() const {
    int count = 0;
    for (int j = 0; j < n; ++j) {
      count += mode == 2 ? 1 : 0;
      count += mode == 2 ? 2 : 0;
      count += mode < 3 ? 1 : 0;
      count += mode < 3 ? 2 : 0;
      count += x ? 1 : 0;
      count += x ? 2 : 0;
      count += y ? 1 : 0;
    }
    out[0] = count;
  }
};

__global__ void launch_mix(const MixExec exec) { exec(); }

// CHECK-LABEL: cir.func {{.*}} @_Z7run_mixRK7MixExec(
// CHECK:         @_Z25__device_stub__launch_mix7MixExec.flags5(
// CHECK-NOT:     @_Z25__device_stub__launch_mix7MixExec.flags6(
// CHECK:         @_Z25__device_stub__launch_mix7MixExec(
void run_mix(const MixExec &exec) { launch_mix<<<1, 1>>>(exec); }

// A function copied for a kernel copy is local to it, and so loses the
// original's hidden visibility along with its linkage.
// VIS:         cir.func {{.*}} hidden @_ZNK4Exec5buildEi(
// VIS-NOT:     hidden
// VIS:         cir.func {{.*}} internal private @_ZNK4Exec5buildEi.flags

// The copy for all three chosen flags set: their tests folded, `d`'s kept.
// DEV-LABEL: cir.func {{.*}} @_ZNK8ManyExecclEv.flags7(
// DEV-NOT:     int_to_bool
// DEV:         cir.get_member %{{.*}}[0] {name = "a"}
// DEV:         cir.const #true
// DEV-NOT:     int_to_bool
// DEV:         cir.get_member %{{.*}}[1] {name = "b"}
// DEV:         cir.const #true
// DEV-NOT:     int_to_bool
// DEV:         cir.get_member %{{.*}}[2] {name = "c"}
// DEV:         cir.const #true
// DEV-NOT:     int_to_bool
// DEV:         cir.get_member %{{.*}}[3] {name = "d"}
// DEV-NEXT:    %[[D:.*]] = cir.load
// DEV-NEXT:    cir.cast int_to_bool %[[D]]

//===----------------------------------------------------------------------===//
// What is left alone
//===----------------------------------------------------------------------===//

// A `bool` flag is a test by itself.
struct BoolExec {
  bool flag;
  int n;
  int *out;
  __device__ void operator()() const {
    int count = 0;
    for (int j = 0; j < n; ++j)
      if (flag)
        ++count;
    out[0] = count;
  }
};
__global__ void launch_bool(const BoolExec exec) { exec(); }
// CHECK-LABEL: cir.func {{.*}} @_Z8run_boolRK8BoolExec(
// CHECK:         cir.load align(1) %{{.*}} : !cir.ptr<!cir.bool>, !cir.bool
// CHECK:         @_Z26__device_stub__launch_bool8BoolExec.flags1(
void run_bool(const BoolExec &exec) { launch_bool<<<1, 1>>>(exec); }

// A flag tested only outside any loop is not worth a copy.
struct OnceExec {
  int flag;
  int *out;
  __device__ void operator()() const { out[0] = flag ? 1 : 2; }
};
__global__ void launch_once(const OnceExec exec) { exec(); }
// DEFAULT-NOT: launch_once{{.*}}.flags
// CHECK-NOT: launch_once{{[A-Za-z0-9_]*}}.flags
void run_once(const OnceExec &exec) { launch_once<<<1, 1>>>(exec); }

// A field also used as a value is not a flag.
struct ValueExec {
  int k;
  int n;
  int *out;
  __device__ void operator()() const {
    int count = 0;
    for (int j = 0; j < n; ++j) {
      if (k)
        ++count;
      count += k;
    }
    out[0] = count;
  }
};
__global__ void launch_value(const ValueExec exec) { exec(); }
// CHECK-NOT: launch_value{{[A-Za-z0-9_]*}}.flags
void run_value(const ValueExec &exec) { launch_value<<<1, 1>>>(exec); }

// A kernel that writes its by-value driver may change a flag before testing it.
struct WriteExec {
  int flag;
  int n;
  int *out;
  __device__ void run() {
    flag = out[1];
    int count = 0;
    for (int j = 0; j < n; ++j)
      if (flag)
        ++count;
    out[0] = count;
  }
};
__global__ void launch_writes(WriteExec exec) { exec.run(); }
// CHECK-NOT: launch_writes{{[A-Za-z0-9_]*}}.flags
void run_writes(const WriteExec &exec) { launch_writes<<<1, 1>>>(exec); }

// Copies to the buffer the host cannot match to the driver the kernel reads:
// too few bytes, at an offset, or by an unknown function. Their kernels keep
// every launch, so they get no copies either.
__global__ void launch_short() {
  reinterpret_cast<const Driver *>(constant_buffer)->operator()();
}
__global__ void launch_offset() {
  reinterpret_cast<const Driver *>(constant_buffer)->operator()();
}
__global__ void launch_unknown() {
  reinterpret_cast<const Driver *>(constant_buffer)->operator()();
}
// CHECK-NOT: launch_short{{[A-Za-z0-9_]*}}.flags
// CHECK-NOT: launch_offset{{[A-Za-z0-9_]*}}.flags
// CHECK-NOT: launch_unknown{{[A-Za-z0-9_]*}}.flags
void run_rejected(const Driver &driver, void *staging) {
  __builtin_memcpy(staging, &driver, sizeof(Driver));
  hipMemcpyToSymbolAsync(constant_buffer, staging, 16, 0, 1, nullptr);
  launch_short<<<1, 1>>>();
  hipMemcpyToSymbolAsync(constant_buffer, staging, sizeof(Driver), 8, 1,
                         nullptr);
  launch_offset<<<1, 1>>>();
  my_copy_to_symbol(constant_buffer, staging, sizeof(Driver), 0, 1, nullptr);
  launch_unknown<<<1, 1>>>();
}
