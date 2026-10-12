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
// MERGE: "-cc1"{{.*}} "-aux-triple" "amdgpu-amd-amdhsa"{{.*}} "-emit-cir"{{.*}} "-o" "[[HOST_CIR:[^"]+\.cir]]"
// Device TU -> CIR.
// MERGE: "-cc1"{{.*}} "-emit-cir"{{.*}} "-fcuda-is-device"{{.*}} "-target-cpu" "gfx90a"{{.*}} "-o" "[[DEV_CIR:[^"]+\.cir]]"
// Both CIR modules are forwarded as inputs to -combine; output is the container.
// MERGE: "{{.*}}cir-offload-merge{{(\.exe)?}}" "-combine" "-targets=host-x86_64-unknown-linux-gnu,hip-amdgpu9.0a-amd-amdhsa-unknown-gfx90a" "-output=[[CONTAINER:[^"]+\.cir]]" "-input=[[HOST_CIR]]" "-input=[[DEV_CIR]]"
// The container is split back into one output per target.
// MERGE: "{{.*}}cir-offload-merge{{(\.exe)?}}" "-split" "-targets=host-x86_64-unknown-linux-gnu,hip-amdgpu9.0a-amd-amdhsa-unknown-gfx90a" "-input=[[CONTAINER]]" "-output=[[HOST_SPLIT:[^"]+\.cir]]" "-output=[[DEV_SPLIT:[^"]+\.cir]]"
// Device module: CIR -> LLVM bitcode.
// MERGE: "-cc1"{{.*}} "-emit-llvm-bc"{{.*}} "-fcuda-is-device"{{.*}} "-target-cpu" "gfx90a"{{.*}} "-o" "[[DEV_BC:[^"]+\.bc]]"{{.*}} "-x" "cir" "[[DEV_SPLIT]]"
// Bitcode -> packaged offload binary.
// MERGE: "{{.*}}llvm-offload-binary{{(\.exe)?}}" "-o" "[[DEV_IMG:[^"]+]]"{{.*}} "--image=file=[[DEV_BC]],triple=amdgpu9.0a-amd-amdhsa,arch=gfx90a,kind=hip"
// Packaged image -> HIP fat binary.
// MERGE: "{{.*}}clang-linker-wrapper{{(\.exe)?}}"{{.*}} "--emit-fatbin-only"{{.*}} "-o" "[[DEV_FATBIN:[^"]+\.hipfb]]"
// Host module: CIR -> object, embedding the device fat binary.
// MERGE: "-cc1"{{.*}} "-emit-obj"{{.*}} "-foffload-include-binary" "[[DEV_FATBIN]]"{{.*}} "-x" "cir" "[[HOST_SPLIT]]"

// RUN: %clang -### -target x86_64-unknown-linux-gnu -x hip -fclangir \
// RUN:   --offload-arch=gfx90a --offload-arch=gfx942 \
// RUN:   -nogpuinc -nogpulib --clangir-offload-merge -c %s 2>&1 \
// RUN: | FileCheck %s --check-prefix=MULTI

// MULTI: "{{.*}}cir-offload-merge{{(\.exe)?}}" "-split" "-targets=host-x86_64-unknown-linux-gnu,hip-amdgpu9.0a-amd-amdhsa-unknown-gfx90a,hip-amdgpu9.42-amd-amdhsa-unknown-gfx942"{{.*}} "-output=[[MULTI_HOST_SPLIT:[^"]+\.cir]]" "-output=[[MULTI_DEV90A_SPLIT:[^"]+\.cir]]" "-output=[[MULTI_DEV942_SPLIT:[^"]+\.cir]]"
// MULTI: "-cc1"{{.*}} "-emit-llvm-bc"{{.*}} "-target-cpu" "gfx90a"{{.*}} "-o" "[[MULTI_DEV90A_BC:[^"]+\.bc]]"{{.*}} "-x" "cir" "[[MULTI_DEV90A_SPLIT]]"
// MULTI: "-cc1"{{.*}} "-emit-llvm-bc"{{.*}} "-o" "[[MULTI_DEV942_BC:[^"]+\.bc]]"{{.*}} "-x" "cir" "[[MULTI_DEV942_SPLIT]]"
// One packaged offload binary holds both per-arch images.
// MULTI: "{{.*}}llvm-offload-binary{{(\.exe)?}}" "-o" "{{[^"]+}}"{{.*}} "--image=file=[[MULTI_DEV90A_BC]],triple=amdgpu9.0a-amd-amdhsa,arch=gfx90a,kind=hip" "--image=file=[[MULTI_DEV942_BC]],triple=amdgpu9.42-amd-amdhsa,arch=gfx942,kind=hip"
// MULTI: "{{.*}}clang-linker-wrapper{{(\.exe)?}}"{{.*}} "--emit-fatbin-only"{{.*}} "-o" "[[MULTI_FATBIN:[^"]+\.hipfb]]"
// MULTI: "-cc1"{{.*}} "-emit-obj"{{.*}} "-foffload-include-binary" "[[MULTI_FATBIN]]"{{.*}} "-x" "cir" "[[MULTI_HOST_SPLIT]]"

// With -save-temps every phase is a job of its own. The preprocessors are
// built for their side, the fat binary is embedded by the host backend, and
// the host assembler gets the host assembly alone. The split's outputs are
// named apart from the CIR compiled before the merge, so both are kept.
// RUN: %clang -### -target x86_64-unknown-linux-gnu -x hip -fclangir \
// RUN:   --offload-arch=gfx90a -nogpuinc -nogpulib --clangir-offload-merge \
// RUN:   -save-temps -c %s 2>&1 \
// RUN: | FileCheck %s --check-prefix=SAVE-TEMPS
// SAVE-TEMPS: "-cc1" "-triple" "x86_64-unknown-linux-gnu" "-aux-triple" "amdgpu-amd-amdhsa"{{.*}} "-E"
// SAVE-TEMPS: "-cc1"{{.*}} "-emit-cir"{{.*}} "-o" "[[ST_HOST_CIR:[^"]+-host-x86_64-unknown-linux-gnu\.cir]]"
// SAVE-TEMPS: "-cc1" "-triple" "amdgpu9.0a-amd-amdhsa"{{.*}} "-E"{{.*}} "-fcuda-is-device"
// SAVE-TEMPS: "-cc1"{{.*}} "-emit-cir"{{.*}} "-fcuda-is-device"{{.*}} "-o" "[[ST_DEV_CIR:[^"]+-gfx90a\.cir]]"
// SAVE-TEMPS: "{{.*}}cir-offload-merge{{(\.exe)?}}" "-combine"{{.*}} "-input=[[ST_HOST_CIR]]" "-input=[[ST_DEV_CIR]]"
// SAVE-TEMPS: "{{.*}}cir-offload-merge{{(\.exe)?}}" "-split"{{.*}} "-output=[[ST_HOST_SPLIT:[^"]+-host-x86_64-unknown-linux-gnu-split\.cir]]" "-output=[[ST_DEV_SPLIT:[^"]+-split-gfx90a\.cir]]"
// SAVE-TEMPS: "-cc1"{{.*}} "-emit-llvm-bc"{{.*}} "-x" "cir" "[[ST_DEV_SPLIT]]"
// SAVE-TEMPS: "{{.*}}clang-linker-wrapper{{(\.exe)?}}"{{.*}} "-o" "[[ST_FATBIN:[^"]+\.hipfb]]"
// SAVE-TEMPS: "-cc1" "-triple" "x86_64-unknown-linux-gnu"{{.*}} "-S"{{.*}} "-foffload-include-binary" "[[ST_FATBIN]]"{{.*}} "-o" "[[ST_HOST_ASM:[^"]+\.s]]" "-x" "cir" "[[ST_HOST_SPLIT]]"
// SAVE-TEMPS: "-cc1as"{{.*}} "[[ST_HOST_ASM]]"{{$}}

// Without --clangir-offload-merge the normal pipeline runs: no merge/split.
// RUN: %clang -### -target x86_64-unknown-linux-gnu -x hip -fclangir \
// RUN:   --offload-arch=gfx90a -nogpuinc -nogpulib \
// RUN:   -c %s 2>&1 | FileCheck %s --check-prefix=NO-MERGE
// NO-MERGE-NOT: "-combine"
// NO-MERGE-NOT: "-split"
// NO-MERGE-NOT: "-x" "cir"

__global__ void kernel() {}
void host() { kernel<<<1, 1>>>(); }
