// Coverage mapping watches the tokens the host's preprocessor produces, as the
// fingerprinting of pragmas does. Both watch together, so pragmas that agree
// between the targets stay shared under coverage too, and coverage mapping is
// still emitted.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: rm -f %t.audit
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -O1 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   -fprofile-instrument=clang -fcoverage-mapping \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -O1 -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   -mllvm -multi-target-audit=%t.audit -mllvm -dump-token-stream 2> %t.stream
// RUN: FileCheck --check-prefix=HOST %s < %t.host.ll
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=DEV %s
// RUN: FileCheck --check-prefix=STREAM %s < %t.stream
// RUN: FileCheck --check-prefix=AUDIT %s < %t.audit

#define __device__ __attribute__((device))
#define __host__ __attribute__((host))

// HOST: @__covrec_

// The same pragma on every target is shared.
__host__ __device__ float zz_same(float a, float b, float c) {
#pragma clang fp contract(fast)
  return a * b + c;
}
// HOST-LABEL: define {{.*}} @_Z7zz_samefff(
// HOST: fmul contract float
// DEV-LABEL: define {{.*}} @_Z7zz_samefff(
// DEV: fmul contract float
// STREAM-NOT: annot_target_alt
// STREAM: annot_pragma_fp

// A pragma that differs is each target's own.
__host__ __device__ float zz_differs(float a, float b, float c) {
#if defined(__HIP_DEVICE_COMPILE__)
#pragma clang fp contract(fast)
#else
#pragma clang fp contract(off)
#endif
  return a * b + c;
}
// HOST-LABEL: define {{.*}} @_Z10zz_differsfff(
// HOST: fmul float
// DEV-LABEL: define {{.*}} @_Z10zz_differsfff(
// DEV: fmul contract float
// STREAM: annot_target_alt_begin
// STREAM: annot_pragma_fp
// STREAM: annot_target_alt_sep
// STREAM: annot_pragma_fp
// STREAM: annot_target_alt_end

// AUDIT: unfingerprinted-annotations {{.*}}pragma-annotations-coverage.hip total=0
