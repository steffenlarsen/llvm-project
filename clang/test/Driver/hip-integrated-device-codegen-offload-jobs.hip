// With -fintegrated-hip-device-codegen, the host compilation links the device
// code itself, so it is given --offload-jobs= as the device link job would be.

// RUN: %clang -### --target=x86_64-linux-gnu -fno-gpu-rdc --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen --offload-arch=gfx900 --offload-arch=gfx942 \
// RUN:   -nogpuinc -nogpulib --offload-jobs=4 -c %s 2>&1 \
// RUN: | FileCheck -check-prefix=FIXED %s
// FIXED: "-cc1" "-triple" "x86_64-unknown-linux-gnu"{{.*}} "-mllvm" "-multi-target-wrapper-jobs=4"

// RUN: %clang -### --target=x86_64-linux-gnu -fno-gpu-rdc --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen --offload-arch=gfx900 --offload-arch=gfx942 \
// RUN:   -nogpuinc -nogpulib --offload-jobs=jobserver -c %s 2>&1 \
// RUN: | FileCheck -check-prefix=JOBSERVER %s
// JOBSERVER: "-cc1" "-triple" "x86_64-unknown-linux-gnu"{{.*}} "-mllvm" "-multi-target-wrapper-jobs=jobserver"

// RUN: not %clang -### --target=x86_64-linux-gnu -fno-gpu-rdc --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen --offload-arch=gfx900 --offload-arch=gfx942 \
// RUN:   -nogpuinc -nogpulib --offload-jobs=0 -c %s 2>&1 \
// RUN: | FileCheck -check-prefix=INVALID %s
// INVALID: error: invalid integral value '0' in '--offload-jobs=0'
// INVALID-NOT: error:

// RUN: %clang -### --target=x86_64-linux-gnu -fno-gpu-rdc --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen --offload-arch=gfx900 --offload-arch=gfx942 \
// RUN:   -nogpuinc -nogpulib -c %s 2>&1 \
// RUN: | FileCheck -check-prefix=NONE %s
// NONE-NOT: -multi-target-wrapper-jobs

// With -fgpu-rdc the device code is linked by the link job, which already
// forwards --offload-jobs=.
// RUN: %clang -### --target=x86_64-linux-gnu -fgpu-rdc --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen --offload-arch=gfx900 --offload-arch=gfx942 \
// RUN:   -nogpuinc -nogpulib --offload-jobs=4 %s 2>&1 \
// RUN: | FileCheck -check-prefix=RDC %s
// RDC-NOT: -multi-target-wrapper-jobs
// RDC: clang-linker-wrapper{{.*}} "--wrapper-jobs=4"
