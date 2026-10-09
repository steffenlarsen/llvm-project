// Every target of an integrated HIP compilation shares one AST, so only a
// precompiled header written by a compilation for the same targets can be used:
// one written for a single target is rejected, as is one for other targets, and
// one written for several targets cannot be used for a single one. Only the
// primary target's precompiled header is used; the other targets' are not. The
// driver's transparent PCH support therefore leaves a single precompiled header
// file unused (see Driver/hip-header-pch.hip for directories of them).
//
// REQUIRES: amdgpu-registered-target

// RUN: rm -rf %t && split-file %s %t
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -emit-pch -o %t/pre.h.pch %t/pre.h

// Without -fintegrated-hip-device-codegen, -include picks up pre.h.pch.
// RUN: %clang -### --target=x86_64-linux-gnu --offload-arch=gfx90a \
// RUN:   -nogpuinc -nogpulib -include %t/pre.h -c %t/use.hip -o %t/use.o 2>&1 \
// RUN: | FileCheck -check-prefix=SEPARATE %s
// SEPARATE: "-include-pch" "{{[^"]*}}pre.h.pch"

// With it, the header is included as written, by the host job and by every
// device invocation.
// RUN: %clang -### --target=x86_64-linux-gnu --offload-arch=gfx90a \
// RUN:   --offload-arch=gfx942 -fintegrated-hip-device-codegen \
// RUN:   -nogpuinc -nogpulib -include %t/pre.h -c %t/use.hip -o %t/use.o 2>&1 \
// RUN: | FileCheck -check-prefix=INTEGRATED %s
// INTEGRATED: "-include" "{{[^"]*}}pre.h"
// INTEGRATED-NOT: -include-pch

// RUN: %clang --target=x86_64-linux-gnu --offload-arch=gfx90a \
// RUN:   --offload-arch=gfx942 -fintegrated-hip-device-codegen \
// RUN:   -nogpuinc -nogpulib -include %t/pre.h -fsyntax-only %t/use.hip

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip-header -emit-pch -o %t/multi.pch \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-record %t/pre.h

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -fsyntax-only -include-pch %t/multi.pch \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip -include-pch %t/device.pch" \
// RUN:   -mllvm -multi-target-record %t/use.hip

// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -fsyntax-only -include-pch %t/pre.h.pch \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-record %t/use.hip 2>&1 \
// RUN: | FileCheck -check-prefix=SINGLE-IN-MULTI %s
// SINGLE-IN-MULTI: error: one AST shared by several targets was disabled in precompiled file '{{[^']*}}pre.h.pch' but is currently enabled

// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -fsyntax-only -include-pch %t/multi.pch %t/use.hip 2>&1 \
// RUN: | FileCheck -check-prefix=MULTI-IN-SINGLE %s
// MULTI-IN-SINGLE: error: one AST shared by several targets was enabled in precompiled file '{{[^']*}}multi.pch' but is currently disabled

// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx942 -x hip -fsyntax-only -include-pch %t/multi.pch \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-record %t/use.hip 2>&1 \
// RUN: | FileCheck -check-prefix=OTHER-TARGETS %s
// OTHER-TARGETS: error: precompiled file '{{[^']*}}multi.pch' was built for the targets 'amdgcn-amd-amdhsa:gfx90a::{{[0-9a-f]+}}', but this compilation is for 'amdgcn-amd-amdhsa:gfx942::{{[0-9a-f]+}}'
// OTHER-TARGETS-NOT: error:

// The same targets do not suffice: each target resumes with the macros it had
// at the end of the header, so it has to have started out with the same ones.
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -fsyntax-only -include-pch %t/multi.pch \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip -DDEVICE_ONLY" \
// RUN:   -mllvm -multi-target-record %t/use.hip 2>&1 \
// RUN: | FileCheck -check-prefix=OTHER-MACROS %s
// OTHER-MACROS: error: precompiled file '{{[^']*}}multi.pch' was built for the targets 'amdgcn-amd-amdhsa:gfx90a::[[HASH:[0-9a-f]+]]', but this compilation is for 'amdgcn-amd-amdhsa:gfx90a::{{[0-9a-f]+}}'

// An optional precompiled header, as -fhip-pch requests, is built for all of
// the targets on first use, and used from then on.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -fsyntax-only -include %t/pre.h \
// RUN:   -include-pch %t/cache -fpch-optional -fpch-auto-generate=%t/pre.h -Rpch-build \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip -include %t/pre.h -include-pch %t/cache -fpch-optional -fpch-auto-generate=%t/pre.h" \
// RUN:   -mllvm -multi-target-record %t/use.hip 2>&1 | FileCheck -check-prefix=BUILD %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -fsyntax-only -include %t/pre.h \
// RUN:   -include-pch %t/cache -fpch-optional -fpch-auto-generate=%t/pre.h -Rpch-build \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip -include %t/pre.h -include-pch %t/cache -fpch-optional -fpch-auto-generate=%t/pre.h" \
// RUN:   -mllvm -multi-target-record %t/use.hip 2>&1 | FileCheck -check-prefix=USE %s
// BUILD: remark: building precompiled header
// BUILD-NOT: error:
// USE: remark: using precompiled header
// USE-NOT: error:

//--- pre.h
#ifndef PRE_H
#define PRE_H
#define __device__ __attribute__((device))
__device__ inline int twice(int x) { return 2 * x; }
#endif

//--- use.hip
__device__ int f(int x) { return twice(x); }
