// Precompiled-header cache for the HIP runtime wrapper.

// By default nothing is requested.
// RUN: %clang -### -x hip -nogpulib --rocm-path=%S/Inputs/rocm --offload-arch=gfx906 %s 2>&1 \
// RUN:   | FileCheck -check-prefix=OFF %s
// OFF-NOT: "-include-pch"
// OFF-NOT: "-fpch-optional"

// -fno-hip-pch is likewise inert.
// RUN: %clang -### -x hip -nogpulib --rocm-path=%S/Inputs/rocm --offload-arch=gfx906 \
// RUN:   -fno-hip-pch %s 2>&1 | FileCheck -check-prefix=OFF %s

// -fno-hip-pch wins over an explicit directory: it is the escape hatch, so it
// has to work whatever else is on the command line.
// RUN: %clang -### -x hip -nogpulib --rocm-path=%S/Inputs/rocm --offload-arch=gfx906 \
// RUN:   -fhip-pch-dir=/tmp/hip-pch-dir -fno-hip-pch %s 2>&1 \
// RUN:   | FileCheck -check-prefix=OFF %s

// Order does not matter; the negative flag still wins.
// RUN: %clang -### -x hip -nogpulib --rocm-path=%S/Inputs/rocm --offload-arch=gfx906 \
// RUN:   -fno-hip-pch -fhip-pch-dir=/tmp/hip-pch-dir %s 2>&1 \
// RUN:   | FileCheck -check-prefix=OFF %s

// A later -fhip-pch re-enables it, per the usual last-flag-wins rule.
// RUN: %clang -### -x hip -nogpulib --rocm-path=%S/Inputs/rocm --offload-arch=gfx906 \
// RUN:   -fhip-pch-dir=/tmp/hip-pch-dir -fno-hip-pch -fhip-pch %s 2>&1 \
// RUN:   | FileCheck -check-prefix=DIR %s

/// An explicit directory is passed through to both host and device jobs, along
/// with the wrapper header to precompile if the cache holds nothing usable.
// RUN: %clang -### -x hip -nogpulib --rocm-path=%S/Inputs/rocm --offload-arch=gfx906 \
// RUN:   -fhip-pch-dir=/tmp/hip-pch-dir %s 2>&1 | FileCheck -check-prefix=DIR %s
// DIR: "-cc1"{{.*}} "-fcuda-is-device"
// DIR-SAME: "-include-pch" "/tmp/hip-pch-dir"
// DIR-SAME: "-fpch-optional"
// DIR-SAME: "-fpch-auto-generate={{.*}}__clang_hip_runtime_wrapper.h"
// DIR: "-cc1"
// DIR-SAME: "-include-pch" "/tmp/hip-pch-dir"
// DIR-SAME: "-fpch-optional"

/// -fhip-pch with no directory uses a default location.
// RUN: env CLANG_HIP_PCH_CACHE_PATH=/tmp/hip-pch-env %clang -### -x hip \
// RUN:   -nogpulib --rocm-path=%S/Inputs/rocm --offload-arch=gfx906 -fhip-pch %s 2>&1 \
// RUN:   | FileCheck -check-prefix=ENV %s
// ENV: "-include-pch" "/tmp/hip-pch-env"

/// An explicit directory wins over the environment.
// RUN: env CLANG_HIP_PCH_CACHE_PATH=/tmp/hip-pch-env %clang -### -x hip \
// RUN:   -nogpulib --rocm-path=%S/Inputs/rocm --offload-arch=gfx906 -fhip-pch \
// RUN:   -fhip-pch-dir=/tmp/hip-pch-dir %s 2>&1 \
// RUN:   | FileCheck -check-prefix=EXPLICIT %s
// EXPLICIT: "-include-pch" "/tmp/hip-pch-dir"
// EXPLICIT-NOT: hip-pch-env

/// A project may nominate its own prefix header instead of the wrapper.
// RUN: %clang -### -x hip -nogpulib --rocm-path=%S/Inputs/rocm --offload-arch=gfx906 \
// RUN:   -fhip-pch-dir=/tmp/hip-pch-dir -fpch-auto-generate=/tmp/prefix.cuh \
// RUN:   %s 2>&1 | FileCheck -check-prefix=PREFIX %s
// PREFIX: "-fpch-auto-generate=/tmp/prefix.cuh"
// PREFIX-NOT: "-fpch-auto-generate={{.*}}__clang_hip_runtime_wrapper.h"

/// The cache pruning policy is forwarded to -cc1.
// RUN: %clang -### -x hip -nogpulib --rocm-path=%S/Inputs/rocm --offload-arch=gfx906 \
// RUN:   -fhip-pch-dir=/tmp/hip-pch-dir -fpch-cache-policy=cache_size_bytes=1g \
// RUN:   %s 2>&1 | FileCheck -check-prefix=POLICY %s
// POLICY: "-fpch-cache-policy=cache_size_bytes=1g"

__global__ void k() {}
