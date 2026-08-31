// With -fintegrated-hip-device-codegen, the device link job forwards options
// to the device compilation and link, -O among them, as without the flag.

// RUN: %clang -### --target=x86_64-linux-gnu -fno-gpu-rdc --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen --offload-arch=gfx900 --offload-arch=gfx942 \
// RUN:   -nogpuinc -nogpulib -O3 -g -mllvm -unroll-threshold=100 -v -c %s 2>&1 \
// RUN: | FileCheck -check-prefix=ARGS %s
// ARGS: clang-linker-wrapper"
// ARGS-SAME: "--device-compiler=amdgpu-amd-amdhsa=-O3"
// ARGS-SAME: "--device-compiler=amdgpu-amd-amdhsa=-g"
// ARGS-SAME: "--device-linker=amdgpu-amd-amdhsa=-mllvm" "--device-linker=amdgpu-amd-amdhsa=-unroll-threshold=100"
// ARGS-SAME: "--emit-fatbin-only"
// ARGS-NOT: "--device-compiler=amdgpu-amd-amdhsa=-v"

// RUN: %clang -### --target=x86_64-linux-gnu -fno-gpu-rdc --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen --offload-arch=gfx900 --offload-arch=gfx942 \
// RUN:   -nogpuinc -nogpulib -c %s 2>&1 \
// RUN: | FileCheck -check-prefix=NONE %s
// NONE: clang-linker-wrapper"
// NONE-NOT: "--device-compiler=amdgpu-amd-amdhsa=-g"

// With -fgpu-rdc the device code is linked by the link job.
// RUN: %clang -### --target=x86_64-linux-gnu -fgpu-rdc --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen --offload-arch=gfx900 --offload-arch=gfx942 \
// RUN:   -nogpuinc -nogpulib -O3 %s 2>&1 \
// RUN: | FileCheck -check-prefix=RDC %s
// RDC: clang-linker-wrapper{{.*}} "--device-compiler=amdgpu-amd-amdhsa=-O3"
