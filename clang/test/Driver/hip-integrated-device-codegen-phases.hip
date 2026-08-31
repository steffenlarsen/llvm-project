// Tests for the Phase 4 prototype flag '-fintegrated-hip-device-codegen', which collapses
// the new-offloading-driver HIP pipeline's per-arch device "clang" jobs, "Offload::Packager"
// job, and "Offload::Linker" job into a single host "clang" job at the Action/Job-graph level.
// See /home/sholstla/.claude/plans/zippy-wishing-sundae.md, "Phase 4" for the full design.

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

// The new flag conflicts with '-fgpu-rdc', '--offload-device-only', and '--offload-via-llvm'
// -- each of these is diagnosed rather than silently falling back to the old pipeline (which
// would otherwise make the flag look like a silent no-op).
//
// RUN: not %clang -### --target=x86_64-linux-gnu --offload-new-driver \
// RUN:   -fintegrated-hip-device-codegen -fgpu-rdc \
// RUN:   --offload-arch=gfx900 -nogpuinc -nogpulib -c %s -o %t.o 2>&1 \
// RUN: | FileCheck -check-prefix=CONFLICT-RDC %s
// CONFLICT-RDC: error: option '-fintegrated-hip-device-codegen' cannot be specified with '-fgpu-rdc'

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
