// An AST dump names the targets each copy of a declaration belongs to, the
// target a keyed specialization was made for, both sides of a call resolved
// per side, and the bodies made for some targets.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx90a -x hip \
// RUN:   -fsyntax-only -ast-dump -ast-dump-filter=zz %s \
// RUN:   "-multi-target-aux-invocation=-std=c++17 %{hip_device} %{host_aux_triple} -target-cpu gfx90a" \
// RUN:   "-multi-target-aux-invocation=-std=c++17 %{hip_device} %{host_aux_triple} -target-cpu gfx942" \
// RUN: | FileCheck %s

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx90a -x hip \
// RUN:   -fsyntax-only -ast-dump=json -ast-dump-filter=zz %s \
// RUN:   "-multi-target-aux-invocation=-std=c++17 %{hip_device} %{host_aux_triple} -target-cpu gfx90a" \
// RUN:   "-multi-target-aux-invocation=-std=c++17 %{hip_device} %{host_aux_triple} -target-cpu gfx942" \
// RUN: | FileCheck -check-prefix=JSON %s

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx90a -x hip \
// RUN:   -fsyntax-only -ast-dump -ast-dump-filter=ZzTile %s \
// RUN:   "-multi-target-aux-invocation=-std=c++17 %{hip_device} %{host_aux_triple} -target-cpu gfx90a" \
// RUN:   "-multi-target-aux-invocation=-std=c++17 %{hip_device} %{host_aux_triple} -target-cpu gfx942" \
// RUN: | FileCheck -check-prefix=KEY %s

// A compilation for one target prints none of it.
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -x hip -fsyntax-only -ast-dump \
// RUN:   -ast-dump-filter=zz %s | FileCheck -check-prefix=SINGLE %s
// SINGLE-NOT: targets=
// SINGLE-NOT: target-key=
// SINGLE-NOT: -side=

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#if defined(__HIP_DEVICE_COMPILE__)
__host__ __device__ constexpr int zz_width() { return 64; }
#else
__host__ __device__ constexpr int zz_width() { return 1; }
#endif

template <int N> struct ZzTile {
#if defined(__gfx942__)
  float data[2 * N];
#else
  float data[N];
#endif
};

__global__ void zz_kernel(int *out) { out[0] = zz_width() + sizeof(ZzTile<4>); }

__host__ int zz_side() { return 1; }
__device__ int zz_side() { return 2; }
__host__ __device__ int zz_which() { return zz_side(); }

template <class T> __host__ __device__ T zz_scale(T x) { return x * zz_width(); }
__global__ void zz_scale_kernel(int *out) { out[0] = zz_scale(1); }
int zz_on_host() { return zz_scale(1); }

// CHECK-LABEL: Dumping zz_width:
// CHECK-NEXT: FunctionDecl {{.*}} zz_width 'int ()' {{.*}} targets={host}{{$}}
// CHECK-LABEL: Dumping zz_width:
// CHECK-NEXT: FunctionDecl {{.*}} zz_width 'int ()' {{.*}} targets={gfx90a,gfx942}{{$}}

// The first parse of device-only code is no target's. The host's copy is
// parsed as the host sees it.
// CHECK-LABEL: Dumping zz_kernel:
// CHECK-NEXT: FunctionDecl {{.*}} zz_kernel 'void (int *)' {{.*}} redundant{{$}}
// CHECK-LABEL: Dumping zz_kernel:
// CHECK-NEXT: FunctionDecl {{.*}} zz_kernel 'void (int *)' {{.*}} targets={host}{{$}}
// CHECK: DeclRefExpr {{.*}} Function {{.*}} 'zz_width' 'int ()' targets={host}{{$}}
// CHECK-LABEL: Dumping zz_kernel:
// CHECK-NEXT: FunctionDecl {{.*}} zz_kernel 'void (int *)' {{.*}} targets={gfx90a}{{$}}
// CHECK: DeclRefExpr {{.*}} Function {{.*}} 'zz_width' 'int ()' targets={gfx90a,gfx942}{{$}}
// CHECK-LABEL: Dumping zz_kernel:
// CHECK-NEXT: FunctionDecl {{.*}} zz_kernel 'void (int *)' {{.*}} targets={gfx942}{{$}}

// CHECK-LABEL: Dumping zz_side:
// CHECK-NEXT: FunctionDecl [[HOST_SIDE:0x[0-9a-f]+]] {{.*}} zz_side
// CHECK-LABEL: Dumping zz_side:
// CHECK-NEXT: FunctionDecl [[DEVICE_SIDE:0x[0-9a-f]+]] {{.*}} zz_side
// CHECK-LABEL: Dumping zz_which:
// CHECK: CallExpr {{.*}} 'int' host-side=Function [[HOST_SIDE]] 'zz_side' 'int ()' device-side=Function [[DEVICE_SIDE]] 'zz_side' 'int ()'{{$}}

// CHECK-LABEL: Dumping zz_scale:
// CHECK: FunctionDecl {{.*}} zz_scale 'int (int)' implicit_instantiation {{.*}} target-key=gfx90a{{$}}
// CHECK: targets={gfx942}: CompoundStmt
// CHECK: FunctionDecl {{.*}} zz_scale 'int (int)' implicit_instantiation {{.*}} targets={host} target-key=host{{$}}

// KEY-LABEL: Dumping ZzTile:
// KEY: ClassTemplateSpecializationDecl {{.*}} struct ZzTile definition {{.*}} targets={host,gfx90a} target-key=host{{$}}
// KEY: FieldDecl {{.*}} data 'float[4]'
// KEY: ClassTemplateSpecializationDecl {{.*}} struct ZzTile definition {{.*}} targets={host,gfx90a} target-key=gfx90a{{$}}
// KEY-LABEL: Dumping ZzTile:
// KEY-NEXT: ClassTemplateDecl {{.*}} ZzTile external-linkage redundant{{$}}
// KEY-LABEL: Dumping ZzTile:
// KEY: ClassTemplateSpecializationDecl {{.*}} struct ZzTile definition {{.*}} targets={gfx942} target-key=gfx942{{$}}
// KEY: FieldDecl {{.*}} data 'float[8]'

// JSON: "targets": [
// JSON-NEXT: "host"
// JSON-NEXT: ]
// JSON: "targets": [
// JSON-NEXT: "gfx90a",
// JSON-NEXT: "gfx942"
// JSON-NEXT: ]
// JSON: "hostSideCallee": {
// JSON: "name": "zz_side"
// JSON: "deviceSideCallee": {
// JSON: "name": "zz_side"
// JSON: "targetBodies": [
// JSON-NEXT: [
// JSON-NEXT: "gfx942"
// JSON-NEXT: ]
// JSON-NEXT: ],
// JSON-NEXT: "targetKey": "gfx90a"
// JSON: "targetKey": "host"
