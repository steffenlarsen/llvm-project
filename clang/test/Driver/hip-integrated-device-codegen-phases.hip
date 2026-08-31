// Tests for the flag '-fintegrated-hip-device-codegen', which collapses the
// new-offloading-driver HIP pipeline's per-arch device "clang" jobs,
// "Offload::Packager" job, and "Offload::Linker" job into a single host
// "clang" job at the Action/Job-graph level.

// Baseline: today's real new-offloading-driver pipeline for non-RDC HIP with two archs --
// two device "clang" jobs, one "Offload::Packager" job, one "Offload::Linker" job, and one
// host "clang" job.
//
// RUN: %clang -ccc-print-bindings --target=x86_64-linux-gnu -fno-gpu-rdc --offload-new-driver \
// RUN:   --offload-arch=gfx900 --offload-arch=gfx942 -nogpuinc -nogpulib -c %s -o %t.o 2>&1 \
// RUN: | FileCheck -check-prefix=BASELINE %s
// BASELINE: # "amdgpu9.00-amd-amdhsa" - "clang", inputs: ["{{.*}}"], output: "[[GFX900:.+o]]"
// BASELINE: # "amdgpu9.42-amd-amdhsa" - "clang", inputs: ["{{.*}}"], output: "[[GFX942:.+o]]"
// BASELINE: # "amdgpu-amd-amdhsa" - "Offload::Packager", inputs: ["[[GFX900]]", "[[GFX942]]"], output: "[[PKG:.+]]"
// BASELINE: # "amdgpu-amd-amdhsa" - "Offload::Linker", inputs: ["[[PKG]]"], output: "[[HIPFB:.+hipfb]]"
// BASELINE: # "x86_64-unknown-linux-gnu" - "clang", inputs: ["{{.*}}", "[[HIPFB]]"], output: "{{.*o}}"

// Under the new flag: exactly one "clang" job (the host triple), and no
// "Offload::Packager"/"Offload::Linker" job anywhere in the graph.
//
// RUN: %clang -ccc-print-bindings --target=x86_64-linux-gnu -fno-gpu-rdc --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen \
// RUN:   --offload-arch=gfx900 --offload-arch=gfx942 -nogpuinc -nogpulib -c %s -o %t.o 2>&1 \
// RUN: | FileCheck -check-prefix=COLLAPSED %s
// COLLAPSED: # "x86_64-unknown-linux-gnu" - "clang", inputs: ["{{.*}}"], output: "{{.*o}}"
// COLLAPSED-NOT: amdgpu
// COLLAPSED-NOT: Offload::Packager
// COLLAPSED-NOT: Offload::Linker

// Same check via -ccc-print-phases: exactly one "compiler" phase (host), no device
// compiler/backend/offload phases at all.
//
// RUN: %clang -x hip --target=x86_64-linux-gnu -fno-gpu-rdc --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen -ccc-print-phases \
// RUN:   --offload-arch=gfx900 --offload-arch=gfx942 -nogpuinc -nogpulib -c %s -o %t.o 2>&1 \
// RUN: | FileCheck -check-prefix=PHASES %s
// PHASES: input, "{{.*}}", hip, (host-hip)
// PHASES: preprocessor, {{.*}}, hip-cpp-output, (host-hip)
// PHASES: compiler, {{.*}}, ir, (host-hip)
// PHASES: backend, {{.*}}, assembler, (host-hip)
// PHASES: assembler, {{.*}}, object, (host-hip)
// PHASES-NOT: device-hip

// Under -### with the new flag: exactly one "-cc1" invocation, carrying the host triple and
// no packager/linker-wrapper subprocess anywhere in the constructed command line.
//
// RUN: %clang -### --target=x86_64-linux-gnu -fno-gpu-rdc --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen \
// RUN:   --offload-arch=gfx900 --offload-arch=gfx942 -nogpuinc -nogpulib -c %s -o %t.o 2>&1 \
// RUN: | FileCheck -check-prefix=DASHHASH %s
// DASHHASH-COUNT-1: "-cc1"
// DASHHASH-NOT: "-cc1"
// DASHHASH-NOT: clang-offload-packager
// DASHHASH-NOT: clang-linker-wrapper

// The single collapsed job must also enable the multi-target decl
// reconciliation pipeline -- without it, host and device copies of the same
// declaration (e.g. a header included once directly and once transitively)
// are reported as hard redefinitions instead of being reconciled.
//
// RUN: %clang -### --target=x86_64-linux-gnu -fno-gpu-rdc --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen \
// RUN:   --offload-arch=gfx900 -nogpuinc -nogpulib -c %s -o %t.o 2>&1 \
// RUN: | FileCheck -check-prefix=MLLVM-FLAGS %s
// MLLVM-FLAGS: "-mllvm" "-multi-target-codegen"
// MLLVM-FLAGS: "-mllvm" "-multi-target-record"
// MLLVM-FLAGS: "-mllvm" "-parse-all-target-alternatives"
// MLLVM-FLAGS: "-mllvm" "-widen-target-alternatives"
// MLLVM-FLAGS: "-mllvm" "-allow-target-variant-decls"
// MLLVM-FLAGS: "-mllvm" "-reparse-divergent-users"
// MLLVM-FLAGS: "-mllvm" "-merge-equivalent-variants"

// Each arch's device cc1 arguments, as the device job would have been built
// without the flag, reach the single job in --offload-arch order, minus the
// job's "-o <output>" and input file.
//
// RUN: %clang -### --target=x86_64-linux-gnu -fno-gpu-rdc --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen \
// RUN:   --offload-arch=gfx900 --offload-arch=gfx942 -nogpuinc -nogpulib -c %s -o %t.o 2>&1 \
// RUN: | FileCheck -check-prefix=AUX-INVOCATION %s
// AUX-INVOCATION: "-multi-target-aux-invocation=-triple amdgpu9.00-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu {{[^"]*}}-fcuda-is-device {{[^"]*}}-target-cpu gfx900 {{[^"]*}}-x hip"
// AUX-INVOCATION-SAME: "-multi-target-aux-invocation=-triple amdgpu9.42-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu {{[^"]*}}-fcuda-is-device {{[^"]*}}-target-cpu gfx942 {{[^"]*}}-x hip"

// Each arch's device libraries stay in its aux invocation. Its -mllvm options
// are process-wide, so they also reach the single job, once.
//
// RUN: %clang -### --target=x86_64-linux-gnu -fno-gpu-rdc --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen --rocm-path=%S/Inputs/rocm \
// RUN:   --offload-arch=gfx900 --offload-arch=gfx908 -nogpuinc -c %s -o %t.o 2>&1 \
// RUN: | FileCheck -check-prefix=DEVICE-LIBS --implicit-check-not=aux-link-bitcode %s
// DEVICE-LIBS: "-multi-target-aux-invocation={{[^"]*}}-mlink-builtin-bitcode {{[^"]*}}oclc_isa_version_900.bc
// DEVICE-LIBS-SAME: "-multi-target-aux-invocation={{[^"]*}}-mlink-builtin-bitcode {{[^"]*}}oclc_isa_version_908.bc
//
// RUN: %clang -### --target=x86_64-linux-gnu -fno-gpu-rdc --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen \
// RUN:   --offload-arch=gfx900 --offload-arch=gfx908 -nogpuinc -nogpulib -c %s -o %t.o 2>&1 \
// RUN: | FileCheck -check-prefix=DEVICE-MLLVM %s
// DEVICE-MLLVM-COUNT-1: "-mllvm" "-amdgpu-internalize-symbols"
// DEVICE-MLLVM-NOT: "-mllvm" "-amdgpu-internalize-symbols"

// An -mllvm option cannot take different values for different targets.
//
// RUN: not %clang -### --target=x86_64-linux-gnu -fno-gpu-rdc --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen \
// RUN:   -Xarch_host -mllvm=-inline-threshold=1 -Xarch_device -mllvm=-inline-threshold=2 \
// RUN:   --offload-arch=gfx900 -nogpuinc -nogpulib -c %s -o %t.o 2>&1 \
// RUN: | FileCheck -check-prefix=MLLVM-CONFLICT %s
// MLLVM-CONFLICT: error: '-mllvm -inline-threshold=1' conflicts with '-mllvm -inline-threshold=2' for offload arch 'gfx900'; '-fintegrated-hip-device-codegen' requires one value for all targets

// With -fgpu-rdc the single job packages its device bitcode unlinked and
// embeds the package itself, as the host job embeds the packager job's output
// without the flag; the final link links it across translation units.
//
// RUN: %clang -ccc-print-bindings --target=x86_64-linux-gnu --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen -fgpu-rdc \
// RUN:   --offload-arch=gfx900 --offload-arch=gfx942 -nogpuinc -nogpulib -c %s -o %t.o 2>&1 \
// RUN: | FileCheck -check-prefix=RDC-COLLAPSED %s
// RDC-COLLAPSED: # "x86_64-unknown-linux-gnu" - "clang", inputs: ["{{.*}}"], output: "{{.*o}}"
// RDC-COLLAPSED-NOT: amdgpu
// RDC-COLLAPSED-NOT: Offload::Packager
// RDC-COLLAPSED-NOT: Offload::Linker
//
// RUN: %clang -ccc-print-bindings --target=x86_64-linux-gnu --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen -fgpu-rdc \
// RUN:   --offload-arch=gfx900 --offload-arch=gfx942 -nogpuinc -nogpulib %s -o %t 2>&1 \
// RUN: | FileCheck -check-prefix=RDC-LINK %s
// RDC-LINK: # "x86_64-unknown-linux-gnu" - "clang", inputs: ["{{.*}}"], output: "[[OBJ:.+o]]"
// RDC-LINK-NEXT: # "x86_64-unknown-linux-gnu" - "Offload::Linker", inputs: ["[[OBJ]]"], output: "{{.*}}"
//
// RUN: %clang -### --target=x86_64-linux-gnu --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen -fgpu-rdc \
// RUN:   --offload-arch=gfx900 --offload-arch=gfx942 -nogpuinc -nogpulib -c %s -o %t.o 2>&1 \
// RUN: | FileCheck -check-prefix=RDC-DASHHASH %s \
// RUN:     --implicit-check-not=amdgpu-internalize-symbols \
// RUN:     --implicit-check-not=multi-target-package-fatbin
// RDC-DASHHASH-COUNT-1: "-cc1"
// RDC-DASHHASH-SAME: "-fgpu-rdc"
// RDC-DASHHASH-SAME: "-multi-target-aux-invocation=-triple amdgpu9.00-amd-amdhsa {{[^"]*}}-fgpu-rdc{{[^"]*}}"
// RDC-DASHHASH-SAME: "-multi-target-aux-invocation=-triple amdgpu9.42-amd-amdhsa {{[^"]*}}-fgpu-rdc{{[^"]*}}"
// RDC-DASHHASH-SAME: "-mllvm" "-multi-target-package-offload=[[PKG:[^"]+]]" "-fembed-offload-object=[[PKG]]"

// The new flag conflicts with '--offload-device-only' and '--offload-via-llvm'
// -- each of these is diagnosed rather than silently falling back to the old pipeline (which
// would otherwise make the flag look like a silent no-op).
//
// RUN: not %clang -### --target=x86_64-linux-gnu --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen --offload-device-only \
// RUN:   --offload-arch=gfx900 -nogpuinc -nogpulib -c %s -o %t.o 2>&1 \
// RUN: | FileCheck -check-prefix=CONFLICT-DEVONLY %s
// CONFLICT-DEVONLY: error: option '-fintegrated-hip-device-codegen' cannot be specified with '--offload-device-only'

// RUN: not %clang -### --target=x86_64-linux-gnu --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen -foffload-via-llvm \
// RUN:   --offload-arch=gfx900 -nogpuinc -nogpulib -c %s -o %t.o 2>&1 \
// RUN: | FileCheck -check-prefix=CONFLICT-LLVM %s
// CONFLICT-LLVM: error: option '-fintegrated-hip-device-codegen' cannot be specified with '--offload-via-llvm'

// Sanity: the flag alone (no conflicting options) produces no diagnostic.
//
// RUN: %clang -### --target=x86_64-linux-gnu -fno-gpu-rdc --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen \
// RUN:   --offload-arch=gfx900 -nogpuinc -nogpulib -c %s -o %t.o 2>&1 \
// RUN: | FileCheck -check-prefix=NOERROR %s
// NOERROR-NOT: error:

__global__ void kernel(int *x) { *x = 1; }
