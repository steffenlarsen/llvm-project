// With -fintegrated-hip-device-codegen, the device link job is given
// --offload-jobs= as without the flag.

// RUN: %clang -### --target=x86_64-linux-gnu -fno-gpu-rdc --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen --offload-arch=gfx900 --offload-arch=gfx942 \
// RUN:   -nogpuinc -nogpulib --offload-jobs=4 -c %s 2>&1 \
// RUN: | FileCheck -check-prefix=FIXED %s
// FIXED: clang-linker-wrapper{{.*}} "--emit-fatbin-only"{{.*}} "--wrapper-jobs=4"

// RUN: %clang -### --target=x86_64-linux-gnu -fno-gpu-rdc --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen --offload-arch=gfx900 --offload-arch=gfx942 \
// RUN:   -nogpuinc -nogpulib --offload-jobs=jobserver -c %s 2>&1 \
// RUN: | FileCheck -check-prefix=JOBSERVER %s
// JOBSERVER: clang-linker-wrapper{{.*}} "--emit-fatbin-only"{{.*}} "--wrapper-jobs=jobserver"

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
// NONE-NOT: --wrapper-jobs

// With -fgpu-rdc the device code is linked by the link job.
// RUN: %clang -### --target=x86_64-linux-gnu -fgpu-rdc --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen --offload-arch=gfx900 --offload-arch=gfx942 \
// RUN:   -nogpuinc -nogpulib --offload-jobs=4 %s 2>&1 \
// RUN: | FileCheck -check-prefix=RDC %s
// RDC: clang-linker-wrapper{{.*}} "--wrapper-jobs=4"
