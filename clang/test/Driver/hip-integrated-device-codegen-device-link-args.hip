// With -fintegrated-hip-device-codegen, the host compilation links the device
// code itself, so it is given the options the device link job would forward to
// the device compilation and link, -O among them.

// RUN: %clang -### --target=x86_64-linux-gnu -fno-gpu-rdc --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen --offload-arch=gfx900 --offload-arch=gfx942 \
// RUN:   -nogpuinc -nogpulib -O3 -g -mllvm -unroll-threshold=100 -v -c %s 2>&1 \
// RUN: | FileCheck -check-prefix=ARGS %s
// ARGS: "-cc1" "-triple" "x86_64-unknown-linux-gnu"
// ARGS-SAME: "-mllvm" "-multi-target-device-compiler-arg=-O3"
// ARGS-SAME: "-mllvm" "-multi-target-device-compiler-arg=-g"
// ARGS-SAME: "-mllvm" "-multi-target-device-linker-arg=-mllvm"
// ARGS-SAME: "-mllvm" "-multi-target-device-linker-arg=-unroll-threshold=100"
// ARGS-NOT: -multi-target-device-compiler-arg=-v

// RUN: %clang -### --target=x86_64-linux-gnu -fno-gpu-rdc --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen --offload-arch=gfx900 --offload-arch=gfx942 \
// RUN:   -nogpuinc -nogpulib -c %s 2>&1 \
// RUN: | FileCheck -check-prefix=NONE %s
// NONE-NOT: -multi-target-device-

// With -fgpu-rdc the device code is linked by the link job, which already
// forwards them.
// RUN: %clang -### --target=x86_64-linux-gnu -fgpu-rdc --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen --offload-arch=gfx900 --offload-arch=gfx942 \
// RUN:   -nogpuinc -nogpulib -O3 %s 2>&1 \
// RUN: | FileCheck -check-prefix=RDC %s
// RDC-NOT: -multi-target-device-
// RDC: clang-linker-wrapper{{.*}} "--device-compiler=amdgpu-amd-amdhsa=-O3"
