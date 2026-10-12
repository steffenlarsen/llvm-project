// Driver pipeline for a CUDA compilation with ClangIR offload merge enabled,
// compiled all the way to an object.

// REQUIRES: cir-support

// RUN: %clang -### -target x86_64-unknown-linux-gnu -x cuda -fclangir \
// RUN:   --cuda-gpu-arch=sm_80 -nocudainc -nocudalib \
// RUN:   --clangir-offload-merge -c %s 2>&1 \
// RUN: | FileCheck %s --check-prefix=MERGE

// Host and device translation units are first lowered to serialized CIR, then
// combined into one cir.offload.container and split back. Each module resumes
// the backend from -x cir: the device module is lowered to PTX, assembled by
// ptxas, packaged into a CUDA fatbinary, and that fatbinary is embedded into
// the host object. The capture variables track that every file is routed to the
// right consumer.

// Host TU -> CIR. The host compile must carry the CUDA aux-triple (i.e. present
// as a CUDA host compile) so it sees __global__, the runtime API, etc.; the
// merge path used to drop this because the host action sits below the split
// barrier and never received the host offload kind.
// MERGE: "-cc1"{{.*}} "-aux-triple" "nvptx64-nvidia-cuda"{{.*}} "-emit-cir"{{.*}} "-o" "[[HOST_CIR:[^"]+\.cir]]"
// Device TU -> CIR.
// MERGE: "-cc1"{{.*}} "-emit-cir"{{.*}} "-fcuda-is-device"{{.*}} "-o" "[[DEV_CIR:[^"]+\.cir]]"
// Both CIR modules are forwarded as inputs to -combine; output is the container.
// MERGE: "{{.*}}cir-offload-merge{{(\.exe)?}}" "-combine" "-targets=host-x86_64-unknown-linux-gnu,cuda-nvptx64-nvidia-cuda-unknown-sm_80" "-output=[[CONTAINER:[^"]+\.cir]]" "-input=[[HOST_CIR]]" "-input=[[DEV_CIR]]"
// The container is split back into one output per target.
// MERGE: "{{.*}}cir-offload-merge{{(\.exe)?}}" "-split" "-targets=host-x86_64-unknown-linux-gnu,cuda-nvptx64-nvidia-cuda-unknown-sm_80" "-input=[[CONTAINER]]" "-output=[[HOST_SPLIT:[^"]+\.cir]]" "-output=[[DEV_SPLIT:[^"]+\.cir]]"
// Device module: CIR -> PTX assembly.
// MERGE: "-cc1"{{.*}} "-S"{{.*}} "-fcuda-is-device"{{.*}} "-o" "[[DEV_PTX:[^"]+\.s]]"{{.*}} "-x" "cir" "[[DEV_SPLIT]]"
// PTX -> cubin.
// MERGE: "{{.*}}ptxas{{(\.exe)?}}"{{.*}} "--output-file" "[[DEV_CUBIN:[^"]+\.o]]"{{.*}} "[[DEV_PTX]]"
// cubin -> CUDA fatbinary.
// MERGE: "{{.*}}fatbinary{{(\.exe)?}}"{{.*}} "--create" "[[DEV_FATBIN:[^"]+\.fatbin]]"{{.*}} "--image3=kind=elf,sm=80,file=[[DEV_CUBIN]]"
// Host module: CIR -> object, embedding the device fatbinary.
// MERGE: "-cc1"{{.*}} "-emit-obj"{{.*}} "-foffload-include-binary" "[[DEV_FATBIN]]"{{.*}} "-x" "cir" "[[HOST_SPLIT]]"

// RUN: %clang -### -target x86_64-unknown-linux-gnu -x cuda -fclangir \
// RUN:   --cuda-gpu-arch=sm_60 --cuda-gpu-arch=sm_70 --cuda-gpu-arch=sm_80 \
// RUN:   --cuda-gpu-arch=sm_90 -nocudainc -nocudalib \
// RUN:   --clangir-offload-merge -c %s 2>&1 \
// RUN: | FileCheck %s --check-prefix=MULTI

// MULTI: "{{.*}}cir-offload-merge{{(\.exe)?}}" "-split" "-targets=host-x86_64-unknown-linux-gnu,cuda-nvptx64-nvidia-cuda-unknown-sm_60,cuda-nvptx64-nvidia-cuda-unknown-sm_70,cuda-nvptx64-nvidia-cuda-unknown-sm_80,cuda-nvptx64-nvidia-cuda-unknown-sm_90"{{.*}} "-output=[[MULTI_HOST_SPLIT:[^"]+\.cir]]" "-output=[[MULTI_DEV60_SPLIT:[^"]+\.cir]]" "-output=[[MULTI_DEV70_SPLIT:[^"]+\.cir]]" "-output=[[MULTI_DEV80_SPLIT:[^"]+\.cir]]" "-output=[[MULTI_DEV90_SPLIT:[^"]+\.cir]]"
// MULTI: "-cc1"{{.*}} "-target-cpu" "sm_60"{{.*}} "-o" "[[MULTI_DEV60_PTX:[^"]+\.s]]"{{.*}} "-x" "cir" "[[MULTI_DEV60_SPLIT]]"
// MULTI: "{{.*}}ptxas{{(\.exe)?}}"{{.*}} "--gpu-name" "sm_60" "--output-file" "[[MULTI_DEV60_CUBIN:[^"]+\.o]]" "[[MULTI_DEV60_PTX]]"
// MULTI: "-cc1"{{.*}} "-target-cpu" "sm_70"{{.*}} "-o" "[[MULTI_DEV70_PTX:[^"]+\.s]]"{{.*}} "-x" "cir" "[[MULTI_DEV70_SPLIT]]"
// MULTI: "{{.*}}ptxas{{(\.exe)?}}"{{.*}} "--gpu-name" "sm_70" "--output-file" "[[MULTI_DEV70_CUBIN:[^"]+\.o]]" "[[MULTI_DEV70_PTX]]"
// MULTI: "-cc1"{{.*}} "-target-cpu" "sm_80"{{.*}} "-o" "[[MULTI_DEV80_PTX:[^"]+\.s]]"{{.*}} "-x" "cir" "[[MULTI_DEV80_SPLIT]]"
// MULTI: "{{.*}}ptxas{{(\.exe)?}}"{{.*}} "--gpu-name" "sm_80" "--output-file" "[[MULTI_DEV80_CUBIN:[^"]+\.o]]" "[[MULTI_DEV80_PTX]]"
// MULTI: "-cc1"{{.*}} "-target-cpu" "sm_90"{{.*}} "-o" "[[MULTI_DEV90_PTX:[^"]+\.s]]"{{.*}} "-x" "cir" "[[MULTI_DEV90_SPLIT]]"
// MULTI: "{{.*}}ptxas{{(\.exe)?}}"{{.*}} "--gpu-name" "sm_90" "--output-file" "[[MULTI_DEV90_CUBIN:[^"]+\.o]]" "[[MULTI_DEV90_PTX]]"
// MULTI: "{{.*}}fatbinary{{(\.exe)?}}"{{.*}} "--create" "[[MULTI_FATBIN:[^"]+\.fatbin]]"{{.*}} "--image3=kind=elf,sm=60,file=[[MULTI_DEV60_CUBIN]]" "--image3=kind=elf,sm=70,file=[[MULTI_DEV70_CUBIN]]" "--image3=kind=elf,sm=80,file=[[MULTI_DEV80_CUBIN]]" "--image3=kind=elf,sm=90,file=[[MULTI_DEV90_CUBIN]]"
// MULTI: "-cc1"{{.*}} "-emit-obj"{{.*}} "-foffload-include-binary" "[[MULTI_FATBIN]]"{{.*}} "-x" "cir" "[[MULTI_HOST_SPLIT]]"

// With -save-temps every phase is a job of its own. The preprocessors are
// built for their side, the fatbinary is embedded by the host backend, and the
// host assembler gets the host assembly alone. The split's outputs are named
// apart from the CIR compiled before the merge, so both are kept.
// RUN: %clang -### -target x86_64-unknown-linux-gnu -x cuda -fclangir \
// RUN:   --cuda-gpu-arch=sm_80 -nocudainc -nocudalib --clangir-offload-merge \
// RUN:   -save-temps -c %s 2>&1 \
// RUN: | FileCheck %s --check-prefix=SAVE-TEMPS
// SAVE-TEMPS: "-cc1" "-triple" "x86_64-unknown-linux-gnu" "-aux-triple" "nvptx64-nvidia-cuda"{{.*}} "-E"
// SAVE-TEMPS: "-cc1"{{.*}} "-emit-cir"{{.*}} "-o" "[[ST_HOST_CIR:[^"]+-host-x86_64-unknown-linux-gnu\.cir]]"
// SAVE-TEMPS: "-cc1" "-triple" "nvptx64-nvidia-cuda"{{.*}} "-E"{{.*}} "-fcuda-is-device"
// SAVE-TEMPS: "-cc1"{{.*}} "-emit-cir"{{.*}} "-fcuda-is-device"{{.*}} "-o" "[[ST_DEV_CIR:[^"]+-sm_80\.cir]]"
// SAVE-TEMPS: "{{.*}}cir-offload-merge{{(\.exe)?}}" "-combine"{{.*}} "-input=[[ST_HOST_CIR]]" "-input=[[ST_DEV_CIR]]"
// SAVE-TEMPS: "{{.*}}cir-offload-merge{{(\.exe)?}}" "-split"{{.*}} "-output=[[ST_HOST_SPLIT:[^"]+-host-x86_64-unknown-linux-gnu-split\.cir]]" "-output=[[ST_DEV_SPLIT:[^"]+-split-sm_80\.cir]]"
// SAVE-TEMPS: "-cc1"{{.*}} "-S"{{.*}} "-fcuda-is-device"{{.*}} "-x" "cir" "[[ST_DEV_SPLIT]]"
// SAVE-TEMPS: "{{.*}}fatbinary{{(\.exe)?}}"{{.*}} "--create" "[[ST_FATBIN:[^"]+\.fatbin]]"
// SAVE-TEMPS: "-cc1" "-triple" "x86_64-unknown-linux-gnu"{{.*}} "-S"{{.*}} "-foffload-include-binary" "[[ST_FATBIN]]"{{.*}} "-o" "[[ST_HOST_ASM:[^"]+\.s]]" "-x" "cir" "[[ST_HOST_SPLIT]]"
// SAVE-TEMPS: "-cc1as"{{.*}} "[[ST_HOST_ASM]]"{{$}}

// With -S the device stops at PTX, which goes into the fatbinary without
// being assembled, and the host assembly embeds that fatbinary.
// RUN: %clang -### -target x86_64-unknown-linux-gnu -x cuda -fclangir \
// RUN:   --cuda-gpu-arch=sm_80 -nocudainc -nocudalib --clangir-offload-merge \
// RUN:   -S %s 2>&1 \
// RUN: | FileCheck %s --check-prefix=ASM --implicit-check-not=ptxas
// ASM: "-cc1"{{.*}} "-S"{{.*}} "-fcuda-is-device"{{.*}} "-o" "[[ASM_DEV_PTX:[^"]+\.s]]"{{.*}} "-x" "cir"
// ASM: "{{.*}}fatbinary{{(\.exe)?}}"{{.*}} "--create" "[[ASM_FATBIN:[^"]+\.fatbin]]"
// ASM: "-cc1" "-triple" "x86_64-unknown-linux-gnu"{{.*}} "-S"{{.*}} "-foffload-include-binary" "[[ASM_FATBIN]]"{{.*}} "-x" "cir"

// Without --clangir-offload-merge the normal pipeline runs: no merge/split.
// RUN: %clang -### -target x86_64-unknown-linux-gnu -x cuda -fclangir \
// RUN:   --cuda-gpu-arch=sm_80 -nocudainc -nocudalib -c %s 2>&1 \
// RUN: | FileCheck %s --check-prefix=NO-MERGE
// NO-MERGE-NOT: "-combine"
// NO-MERGE-NOT: "-split"
// NO-MERGE-NOT: "-x" "cir"

// -emit-cir stops at CIR; there is nothing to merge/split.
// RUN: %clang -### -target x86_64-unknown-linux-gnu -x cuda -fclangir \
// RUN:   --cuda-gpu-arch=sm_80 -nocudainc -nocudalib --clangir-offload-merge \
// RUN:   -emit-cir %s 2>&1 | FileCheck %s --check-prefix=EMIT-CIR
// EMIT-CIR-NOT: "-combine"
// EMIT-CIR-NOT: "-split"

__global__ void kernel() {}
void host() { kernel<<<1, 1>>>(); }
