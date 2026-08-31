// With -fintegrated-hip-device-codegen, the host compilation preprocesses its
// source once for every target itself. Preprocessing in a job of its own would
// preprocess it for the host alone.

// -save-temps keeps what the compilation preprocessed for each target, under
// the names the separate preprocessing jobs have without the flag, instead of
// adding such a job.
// RUN: %clang -### --target=x86_64-linux-gnu -fno-gpu-rdc --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen --offload-arch=gfx900 --offload-arch=gfx942 \
// RUN:   -nogpuinc -nogpulib -save-temps -c %s -o %t.o 2>&1 \
// RUN: | FileCheck -check-prefix=SAVE-TEMPS %s
// SAVE-TEMPS-NOT: "-E"
// SAVE-TEMPS: "-cc1" "-triple" "x86_64-unknown-linux-gnu"{{.*}} "-emit-llvm-bc"
// SAVE-TEMPS-SAME: "-multi-target-preprocessed-output=1:hip-integrated-device-codegen-preprocessing-host-x86_64-unknown-linux-gnu.hipi"
// SAVE-TEMPS-SAME: "-multi-target-preprocessed-output=2:hip-integrated-device-codegen-preprocessing-hip-amdgpu9.00-amd-amdhsa-gfx900.hipi"
// SAVE-TEMPS-SAME: "-multi-target-preprocessed-output=3:hip-integrated-device-codegen-preprocessing-hip-amdgpu9.42-amd-amdhsa-gfx942.hipi"
// SAVE-TEMPS-SAME: "-x" "hip" "{{[^"]*}}hip-integrated-device-codegen-preprocessing.hip"
// SAVE-TEMPS-NOT: "-E"

// The same names as the preprocessing jobs of a compilation without the flag.
// RUN: %clang -### --target=x86_64-linux-gnu -fno-gpu-rdc --offload-new-driver \
// RUN:   --offload-arch=gfx900 --offload-arch=gfx942 \
// RUN:   -nogpuinc -nogpulib -save-temps -c %s -o %t.o 2>&1 \
// RUN: | FileCheck -check-prefix=SEPARATE %s
// SEPARATE-DAG: "-E"{{.*}} "-o" "hip-integrated-device-codegen-preprocessing-hip-amdgpu9.00-amd-amdhsa-gfx900.hipi"
// SEPARATE-DAG: "-E"{{.*}} "-o" "hip-integrated-device-codegen-preprocessing-hip-amdgpu9.42-amd-amdhsa-gfx942.hipi"
// SEPARATE-DAG: "-E"{{.*}} "-o" "hip-integrated-device-codegen-preprocessing-host-x86_64-unknown-linux-gnu.hipi"

// A separate preprocessing job cannot be asked for.
// RUN: not %clang -### --target=x86_64-linux-gnu -fno-gpu-rdc --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen --offload-arch=gfx900 --offload-arch=gfx942 \
// RUN:   -nogpuinc -nogpulib -no-integrated-cpp -c %s -o %t.o 2>&1 \
// RUN: | FileCheck -check-prefix=NO-INTEGRATED-CPP %s
// NO-INTEGRATED-CPP: error: option '-fintegrated-hip-device-codegen' cannot be specified with '-no-integrated-cpp'

// Preprocessed input is compiled for the host alone, as without the flag.
// RUN: %clang -### --target=x86_64-linux-gnu -fno-gpu-rdc --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen --offload-arch=gfx900 --offload-arch=gfx942 \
// RUN:   -nogpuinc -nogpulib -x hip-cpp-output -c %s -o %t.o 2>&1 \
// RUN: | FileCheck -check-prefix=PREPROCESSED %s
// PREPROCESSED: warning: no device code is generated for preprocessed input '{{[^']*}}hip-integrated-device-codegen-preprocessing.hip'; '-fintegrated-hip-device-codegen' compiles source for every target [-Wintegrated-hip-device-codegen]
// PREPROCESSED-NOT: multi-target
// PREPROCESSED: "-cc1" "-triple" "x86_64-unknown-linux-gnu"
// PREPROCESSED-NOT: multi-target
// PREPROCESSED-NOT: "-triple" "amdgpu

__attribute__((global)) void kernel() {}
