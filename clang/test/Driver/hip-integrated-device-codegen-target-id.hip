// With -fintegrated-hip-device-codegen, each device target's code is packaged
// under the target ID it is bound to, as the offload packager names it. Two
// target IDs of one processor are two targets.

// RUN: %clang -### --target=x86_64-linux-gnu -x hip -nogpuinc -nogpulib \
// RUN:   --offload-arch=gfx90a:xnack+ --offload-arch=gfx90a:xnack- \
// RUN:   --offload-arch=gfx942 -fintegrated-hip-device-codegen -c %s 2>&1 \
// RUN: | FileCheck %s
// CHECK: "-cc1" "-triple" "x86_64-unknown-linux-gnu"
// CHECK-SAME: "-multi-target-aux-target-id=2:gfx90a:xnack+"
// CHECK-SAME: "-multi-target-aux-target-id=3:gfx90a:xnack-"
// CHECK-SAME: "-multi-target-aux-target-id=4:gfx942"
