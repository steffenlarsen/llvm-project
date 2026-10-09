// A HIP header is precompiled into a directory: a file for the host and one
// for each device architecture, or, with -fintegrated-hip-device-codegen, one
// for all of them at once.

// RUN: rm -rf %t && mkdir -p %t
// RUN: touch %t/g.h

// RUN: %clang -### --target=x86_64-linux-gnu -x hip-header %t/g.h \
// RUN:   --offload-arch=gfx900 --offload-arch=gfx942 -nogpuinc -nogpulib \
// RUN:   -o %t/out.pch 2>&1 | FileCheck -check-prefix=SEPARATE %s
// SEPARATE: "-cc1" "-triple" "amdgpu{{[^"]*}}-amd-amdhsa" {{.*}}"-emit-pch"{{.*}} "-target-cpu" "gfx900"{{.*}} "-o" "{{[^"]*}}out.pch{{/|\\\\}}amdgpu-amd-amdhsa-gfx900.pch" "-x" "hip-header"
// SEPARATE: "-cc1" "-triple" "amdgpu{{[^"]*}}-amd-amdhsa" {{.*}}"-emit-pch"{{.*}} "-target-cpu" "gfx942"{{.*}} "-o" "{{[^"]*}}out.pch{{/|\\\\}}amdgpu-amd-amdhsa-gfx942.pch" "-x" "hip-header"
// SEPARATE: "-cc1" "-triple" "x86_64-unknown-linux-gnu" {{.*}}"-emit-pch"{{.*}} "-o" "{{[^"]*}}out.pch{{/|\\\\}}x86_64-unknown-linux-gnu.pch" "-x" "hip-header"
// SEPARATE-NOT: clang-linker-wrapper
// SEPARATE-NOT: llvm-offload-binary

// RUN: %clang -### --target=x86_64-linux-gnu -x hip-header %t/g.h \
// RUN:   --offload-arch=gfx900 --offload-arch=gfx942 -nogpuinc -nogpulib \
// RUN:   -fintegrated-hip-device-codegen -o %t/out.pch 2>&1 \
// RUN: | FileCheck -check-prefix=INTEGRATED %s
// INTEGRATED: "-cc1" "-triple" "x86_64-unknown-linux-gnu" {{.*}}"-emit-pch"
// INTEGRATED-NOT: "-multi-target-codegen"
// INTEGRATED-NOT: "-multi-target-aux-output
// INTEGRATED-SAME: "-mllvm" "-multi-target-record"
// INTEGRATED-SAME: "-multi-target-aux-invocation=
// INTEGRATED-SAME: "-o" "{{[^"]*}}out.pch{{/|\\\\}}x86_64-unknown-linux-gnu+gfx900+gfx942.pch" "-x" "hip-header"
// INTEGRATED-NOT: "-cc1"

// Without -o, the directory is named after the header.
// RUN: %clang -### --target=x86_64-linux-gnu -x hip-header %t/g.h \
// RUN:   --offload-arch=gfx900 -nogpuinc -nogpulib 2>&1 \
// RUN: | FileCheck -check-prefix=DEFAULT %s
// DEFAULT: "-o" "{{[^"]*}}g.h.gch{{/|\\\\}}amdgpu-amd-amdhsa-gfx900.pch"
// DEFAULT: "-o" "{{[^"]*}}g.h.gch{{/|\\\\}}x86_64-unknown-linux-gnu.pch"

// Including the header uses such a directory next to it if one of its files
// is for the compilation, and the header as written otherwise. Each target of
// a compilation looks for its own file...
// RUN: mkdir -p %t/g.h.pch
// RUN: %clang -### --target=x86_64-linux-gnu -x hip -c %s -include %t/g.h \
// RUN:   --offload-arch=gfx900 -nogpuinc -nogpulib 2>&1 \
// RUN: | FileCheck -check-prefix=USE-SEPARATE %s
// USE-SEPARATE: "-cc1" "-triple" "amdgpu{{[^"]*}}-amd-amdhsa" {{.*}}"-include-pch" "{{[^"]*}}g.h.pch" "-fpch-optional" "-include" "{{[^"]*}}g.h"
// USE-SEPARATE: "-cc1" "-triple" "x86_64-unknown-linux-gnu" {{.*}}"-include-pch" "{{[^"]*}}g.h.pch" "-fpch-optional" "-include" "{{[^"]*}}g.h"

// ...but in an integrated compilation, only the primary target does: the
// others resume from it.
// RUN: %clang -### --target=x86_64-linux-gnu -x hip -c %s -include %t/g.h \
// RUN:   --offload-arch=gfx900 -nogpuinc -nogpulib \
// RUN:   -fintegrated-hip-device-codegen 2>&1 \
// RUN: | FileCheck -check-prefix=USE-INTEGRATED %s
// USE-INTEGRATED: "-cc1" "-triple" "x86_64-unknown-linux-gnu" {{.*}}"-include-pch" "{{[^"]*}}g.h.pch" "-fpch-optional" "-include" "{{[^"]*}}g.h"
// USE-INTEGRATED-NOT: -multi-target-aux-invocation={{[^"]*}}-include-pch

// A single precompiled header holds one target's AST, which an integrated
// compilation cannot use.
// RUN: rm -rf %t/g.h.pch && touch %t/g.h.pch
// RUN: %clang -### --target=x86_64-linux-gnu -x hip -c %s -include %t/g.h \
// RUN:   --offload-arch=gfx900 -nogpuinc -nogpulib \
// RUN:   -fintegrated-hip-device-codegen 2>&1 \
// RUN: | FileCheck -check-prefix=SINGLE-FILE %s
// SINGLE-FILE-NOT: -include-pch
// SINGLE-FILE: "-include" "{{[^"]*}}g.h"
// SINGLE-FILE-NOT: -include-pch
