// Driver pipeline for an OpenMP offloading compilation with ClangIR offload
// merge enabled, compiled all the way to an object.

// REQUIRES: cir-support

// RUN: %clang -### --target=x86_64-unknown-linux-gnu -fopenmp \
// RUN:   --offload-arch=gfx90a -nogpulib -fclangir --clangir-offload-merge \
// RUN:   -c %s 2>&1 | FileCheck %s --check-prefix=MERGE

// Host and device translation units are lowered to CIR without the host IR,
// combined into one cir.offload.container and split back. The host module is
// then lowered to bitcode, which the device lowering takes as its host IR, as
// it does without the merge. The device image is packaged and embedded into
// the host object, which the host backend builds from the host bitcode.

// MERGE: "-cc1" "-triple" "x86_64-unknown-linux-gnu"{{.*}} "-emit-cir"{{.*}} "-o" "[[HOST_CIR:[^"]+\.cir]]" "-x" "c"
// MERGE: "-cc1" "-triple" "amdgpu9.0a-amd-amdhsa"{{.*}} "-emit-cir"{{.*}} "-fopenmp-is-target-device"
// MERGE-NOT: "-fopenmp-host-ir-file-path"
// MERGE-SAME: "-o" "[[DEV_CIR:[^"]+\.cir]]" "-x" "c"
// MERGE: "{{.*}}cir-offload-merge{{(\.exe)?}}" "-combine" "-targets=host-x86_64-unknown-linux-gnu,openmp-amdgpu9.0a-amd-amdhsa-unknown-gfx90a" "-output=[[CONTAINER:[^"]+\.cir]]" "-input=[[HOST_CIR]]" "-input=[[DEV_CIR]]"
// MERGE: "{{.*}}cir-offload-merge{{(\.exe)?}}" "-split" "-targets=host-x86_64-unknown-linux-gnu,openmp-amdgpu9.0a-amd-amdhsa-unknown-gfx90a" "-input=[[CONTAINER]]" "-output=[[HOST_SPLIT:[^"]+\.cir]]" "-output=[[DEV_SPLIT:[^"]+\.cir]]"
// MERGE: "-cc1" "-triple" "x86_64-unknown-linux-gnu"{{.*}} "-emit-llvm-bc"{{.*}} "-o" "[[HOST_BC:[^"]+\.bc]]" "-x" "cir" "[[HOST_SPLIT]]"
// MERGE: "-cc1" "-triple" "amdgpu9.0a-amd-amdhsa"{{.*}} "-fopenmp-is-target-device" "-fopenmp-host-ir-file-path" "[[HOST_BC]]"{{.*}} "-o" "[[DEV_OBJ:[^"]+]]" "-x" "cir" "[[DEV_SPLIT]]"
// MERGE: "{{.*}}llvm-offload-binary{{(\.exe)?}}" "-o" "[[DEV_IMG:[^"]+]]" "--image=file=[[DEV_OBJ]],triple=amdgpu9.0a-amd-amdhsa,arch=gfx90a,kind=openmp"
// MERGE: "-cc1" "-triple" "x86_64-unknown-linux-gnu"{{.*}} "-emit-obj"{{.*}} "-fembed-offload-object=[[DEV_IMG]]"{{.*}} "-x" "ir" "[[HOST_BC]]"

// Two variants of one arch share a triple, so the arch tells their modules
// apart.
// RUN: %clang -### --target=x86_64-unknown-linux-gnu -fopenmp \
// RUN:   --offload-arch=gfx90a:xnack+ --offload-arch=gfx90a:xnack- -nogpulib \
// RUN:   -fclangir --clangir-offload-merge -c %s 2>&1 \
// RUN: | FileCheck %s --check-prefix=MULTI

// MULTI: "{{.*}}cir-offload-merge{{(\.exe)?}}" "-split" "-targets=host-x86_64-unknown-linux-gnu,openmp-amdgpu9.0a-amd-amdhsa-unknown-gfx90a:xnack+,openmp-amdgpu9.0a-amd-amdhsa-unknown-gfx90a:xnack-"

// Without --clangir-offload-merge the normal pipeline runs: no merge/split.
// RUN: %clang -### --target=x86_64-unknown-linux-gnu -fopenmp \
// RUN:   --offload-arch=gfx90a -nogpulib -fclangir -c %s 2>&1 \
// RUN: | FileCheck %s --check-prefix=NO-MERGE
// NO-MERGE-NOT: "-combine"
// NO-MERGE-NOT: "-split"
// NO-MERGE-NOT: "-x" "cir"

void f(void) {}
