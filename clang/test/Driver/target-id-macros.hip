// REQUIRES: x86-registered-target
// REQUIRES: amdgpu-registered-target

// RUN: %clang -E -dM -target x86_64-linux-gnu --cuda-device-only \
// RUN:   --offload-arch=gfx908:xnack+:sramecc- -nogpuinc -nogpulib \
// RUN:   -o - %s 2>&1 | FileCheck %s

// CHECK-DAG: #define __amdgcn_processor__ "gfx908"
// CHECK-DAG: #define __amdgcn_feature_xnack__ 1
// CHECK-DAG: #define __amdgcn_feature_sramecc__ 0
// CHECK-DAG: #define __amdgcn_target_id__ "gfx908:sramecc-:xnack+"

// The aux (device) target's subarch-identity macro must not leak into the
// HIP host compile pass -- it is only meaningful on the device side.
// RUN: %clang -E -dM -target x86_64-linux-gnu --cuda-host-only \
// RUN:   --offload-arch=gfx90a -nogpuinc -nogpulib \
// RUN:   -o - %s 2>&1 | FileCheck --check-prefix=HOST %s
// RUN: %clang -E -dM -target x86_64-linux-gnu --cuda-device-only \
// RUN:   --offload-arch=gfx90a -nogpuinc -nogpulib \
// RUN:   -o - %s 2>&1 | FileCheck --check-prefix=DEVICE %s

// HOST-NOT: #define __gfx90a__
// DEVICE-DAG: #define __gfx90a__ 1
