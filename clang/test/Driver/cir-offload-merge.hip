// Driver pipeline for a HIP compilation with ClangIR offload merge enabled,
// compiled all the way to an object.

// REQUIRES: cir-support

// RUN: %clang -### -target x86_64-unknown-linux-gnu -x hip -fclangir \
// RUN:   --offload-arch=gfx90a -nogpuinc -nogpulib \
// RUN:   --clangir-offload-merge -c %s 2>&1 \
// RUN: | FileCheck %s --check-prefix=MERGE

// Host and device translation units are first lowered to serialized CIR, then
// combined into one cir.offload.container and split back. The device module
// resumes from -x cir as LLVM bitcode, the per-arch images are packaged with
// llvm-offload-binary, clang-linker-wrapper produces the HIP fat binary, and
// that fat binary is embedded into the host object. This is the new offload
// driver's HIP packaging (see the HIPNoRDC path in BuildOffloadingActions).

// Host TU -> CIR. The host compile carries the HIP aux-triple so it sees
// __global__, the runtime API, etc.
// MERGE: "-cc1"{{.*}} "-aux-triple" "amdgcn-amd-amdhsa"{{.*}} "-emit-cir"{{.*}} "-o" "[[HOST_CIR:[^"]+\.cir]]"
// Device TU -> CIR.
// MERGE: "-cc1"{{.*}} "-emit-cir"{{.*}} "-fcuda-is-device"{{.*}} "-target-cpu" "gfx90a"{{.*}} "-o" "[[DEV_CIR:[^"]+\.cir]]"
// Both CIR modules are forwarded as inputs to -combine; output is the container.
// MERGE: "{{.*}}cir-offload-merge{{(\.exe)?}}" "-combine" "-targets=host-x86_64-unknown-linux-gnu,hip-amdgcn-amd-amdhsa-unknown-gfx90a" "-output=[[CONTAINER:[^"]+\.cir]]" "-input=[[HOST_CIR]]" "-input=[[DEV_CIR]]"
// The container is split back into one output per target.
// MERGE: "{{.*}}cir-offload-merge{{(\.exe)?}}" "-split" "-targets=host-x86_64-unknown-linux-gnu,hip-amdgcn-amd-amdhsa-unknown-gfx90a" "-input=[[CONTAINER]]" "-output=[[HOST_SPLIT:[^"]+\.cir]]" "-output=[[DEV_SPLIT:[^"]+\.cir]]"
// Device module: CIR -> LLVM bitcode.
// MERGE: "-cc1"{{.*}} "-emit-llvm-bc"{{.*}} "-fcuda-is-device"{{.*}} "-target-cpu" "gfx90a"{{.*}} "-o" "[[DEV_BC:[^"]+\.bc]]"{{.*}} "-x" "cir" "[[DEV_SPLIT]]"
// Bitcode -> packaged offload binary.
// MERGE: "{{.*}}llvm-offload-binary{{(\.exe)?}}" "-o" "[[DEV_IMG:[^"]+]]"{{.*}} "--image=file=[[DEV_BC]],triple=amdgcn-amd-amdhsa,arch=gfx90a,kind=hip"
// Packaged image -> HIP fat binary.
// MERGE: "{{.*}}clang-linker-wrapper{{(\.exe)?}}"{{.*}} "--emit-fatbin-only"{{.*}} "-o" "[[DEV_FATBIN:[^"]+\.hipfb]]"
// Host module: CIR -> object, embedding the device fat binary.
// MERGE: "-cc1"{{.*}} "-emit-obj"{{.*}} "-fcuda-include-gpubinary" "[[DEV_FATBIN]]"{{.*}} "-x" "cir" "[[HOST_SPLIT]]"

// RUN: %clang -### -target x86_64-unknown-linux-gnu -x hip -fclangir \
// RUN:   --offload-arch=gfx90a --offload-arch=gfx942 \
// RUN:   -nogpuinc -nogpulib --clangir-offload-merge -c %s 2>&1 \
// RUN: | FileCheck %s --check-prefix=MULTI

// MULTI: "{{.*}}cir-offload-merge{{(\.exe)?}}" "-split" "-targets=host-x86_64-unknown-linux-gnu,hip-amdgcn-amd-amdhsa-unknown-gfx90a,hip-amdgcn-amd-amdhsa-unknown-gfx942"{{.*}} "-output=[[MULTI_HOST_SPLIT:[^"]+\.cir]]" "-output=[[MULTI_DEV90A_SPLIT:[^"]+\.cir]]" "-output=[[MULTI_DEV942_SPLIT:[^"]+\.cir]]"
// MULTI: "-cc1"{{.*}} "-emit-llvm-bc"{{.*}} "-target-cpu" "gfx90a"{{.*}} "-o" "[[MULTI_DEV90A_BC:[^"]+\.bc]]"{{.*}} "-x" "cir" "[[MULTI_DEV90A_SPLIT]]"
// MULTI: "-cc1"{{.*}} "-emit-llvm-bc"{{.*}} "-target-cpu" "gfx942"{{.*}} "-o" "[[MULTI_DEV942_BC:[^"]+\.bc]]"{{.*}} "-x" "cir" "[[MULTI_DEV942_SPLIT]]"
// One packaged offload binary holds both per-arch images.
// MULTI: "{{.*}}llvm-offload-binary{{(\.exe)?}}" "-o" "{{[^"]+}}"{{.*}} "--image=file=[[MULTI_DEV90A_BC]],triple=amdgcn-amd-amdhsa,arch=gfx90a,kind=hip" "--image=file=[[MULTI_DEV942_BC]],triple=amdgcn-amd-amdhsa,arch=gfx942,kind=hip"
// MULTI: "{{.*}}clang-linker-wrapper{{(\.exe)?}}"{{.*}} "--emit-fatbin-only"{{.*}} "-o" "[[MULTI_FATBIN:[^"]+\.hipfb]]"
// MULTI: "-cc1"{{.*}} "-emit-obj"{{.*}} "-fcuda-include-gpubinary" "[[MULTI_FATBIN]]"{{.*}} "-x" "cir" "[[MULTI_HOST_SPLIT]]"

// -Xclangir-offload-merge forwards its argument verbatim to the tool, so an
// offload pass can be turned off to attribute what it is worth without editing
// the driver. Both jobs run the same binary, so both get the argument.
// RUN: %clang -### -target x86_64-unknown-linux-gnu -x hip -fclangir \
// RUN:   --offload-arch=gfx90a -nogpuinc -nogpulib --clangir-offload-merge \
// RUN:   -Xclangir-offload-merge --no-offload-opts \
// RUN:   -Xclangir-offload-merge --no-module-opts -c %s 2>&1 \
// RUN: | FileCheck %s --check-prefix=PASSTHRU

// PASSTHRU: "{{.*}}cir-offload-merge{{(\.exe)?}}" "-combine" "-targets={{[^"]*}}" "--no-offload-opts" "--no-module-opts" "-output=
// PASSTHRU: "{{.*}}cir-offload-merge{{(\.exe)?}}" "-split" "-targets={{[^"]*}}" "--no-offload-opts" "--no-module-opts" "-input=

// Without --clangir-offload-merge the normal pipeline runs: no merge/split.
// RUN: %clang -### -target x86_64-unknown-linux-gnu -x hip -fclangir \
// RUN:   --offload-arch=gfx90a -nogpuinc -nogpulib \
// RUN:   -c %s 2>&1 | FileCheck %s --check-prefix=NO-MERGE
// NO-MERGE-NOT: "-combine"
// NO-MERGE-NOT: "-split"
// NO-MERGE-NOT: "-x" "cir"

__global__ void kernel() {}
void host() { kernel<<<1, 1>>>(); }
