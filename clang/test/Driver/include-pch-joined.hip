// The joined -include-pch= spelling exists so the option can be carried by
// -Xarch_device / -Xarch_host, which reject options taking a separate
// argument. Without it an offload compilation cannot select a per-target PCH.

/// -Xarch_device reaches the device job only.
// RUN: %clang -### -x hip -nogpulib -nogpuinc --offload-arch=gfx906 \
// RUN:   --target=x86_64-unknown-linux-gnu -Xarch_device -include-pch=/tmp/dev.pch \
// RUN:   %s 2>&1 | FileCheck -check-prefix=DEVICE %s
// DEVICE: "-cc1"{{.*}} "-fcuda-is-device"
// DEVICE-SAME: "-include-pch" "/tmp/dev.pch"
// DEVICE: "-cc1"
// DEVICE-NOT: "-include-pch" "/tmp/dev.pch"

/// -Xarch_host reaches the host job only.
// RUN: %clang -### -x hip -nogpulib -nogpuinc --offload-arch=gfx906 \
// RUN:   --target=x86_64-unknown-linux-gnu -Xarch_host -include-pch=/tmp/host.pch \
// RUN:   %s 2>&1 | FileCheck -check-prefix=HOST %s
// HOST: "-cc1"{{.*}} "-fcuda-is-device"
// HOST-NOT: "-include-pch" "/tmp/host.pch"
// HOST: "-cc1"
// HOST-SAME: "-include-pch" "/tmp/host.pch"

/// The joined and separate spellings are the same option: it is translated to
/// the separate form for -cc1 either way.
// RUN: %clang -### -x hip -nogpulib -nogpuinc --offload-arch=gfx906 \
// RUN:   --target=x86_64-unknown-linux-gnu --include-pch=/tmp/both.pch %s 2>&1 \
// RUN:   | FileCheck -check-prefix=JOINED %s
// JOINED: "-include-pch" "/tmp/both.pch"
// JOINED-NOT: "-include-pch=/tmp/both.pch"

__global__ void k() {}
