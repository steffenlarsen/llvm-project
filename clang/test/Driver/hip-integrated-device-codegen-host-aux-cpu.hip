// The host compilation is not told a device's CPU, with or without
// -fintegrated-hip-device-codegen. The CPU would change the host's predefined
// macros, and each device's own invocation records its tokens.

// RUN: %clang -### --target=x86_64-linux-gnu -x hip --offload-arch=gfx900 \
// RUN:   --offload-arch=gfx942 -nogpuinc -nogpulib -c %s 2>&1 \
// RUN: | FileCheck %s
// RUN: %clang -### --target=x86_64-linux-gnu -x hip --offload-arch=gfx900 \
// RUN:   --offload-arch=gfx942 -nogpuinc -nogpulib \
// RUN:   -fintegrated-hip-device-codegen -c %s 2>&1 \
// RUN: | FileCheck %s
// CHECK: "-cc1" "-triple" "x86_64-unknown-linux-gnu"
// CHECK-NOT: "-aux-target-cpu"
// CHECK-SAME: {{$}}
