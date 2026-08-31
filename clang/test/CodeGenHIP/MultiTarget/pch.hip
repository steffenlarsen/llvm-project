// A precompiled header holds one target's AST, but every target of an
// integrated HIP compilation shares one AST. The driver's transparent PCH
// support therefore includes the header as written, and an explicit
// precompiled header for any of the targets is rejected.
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

// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -fsyntax-only -include-pch %t/pre.h.pch \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a" \
// RUN:   %t/use.hip 2>&1 | FileCheck -check-prefix=HOST-PCH %s
// HOST-PCH: error: precompiled header '{{[^']*}}pre.h.pch' cannot be used when compiling for multiple targets at once

// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -fsyntax-only -include %t/pre.h \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a -include-pch %t/device.pch" \
// RUN:   %t/use.hip 2>&1 | FileCheck -check-prefix=DEVICE-PCH %s
// DEVICE-PCH: error: precompiled header '{{[^']*}}device.pch' cannot be used when compiling for multiple targets at once

// A compilation for the host alone ignores the flag, so it uses the
// precompiled header as without it.
// RUN: %clang -### --target=x86_64-linux-gnu --offload-arch=gfx90a \
// RUN:   -fintegrated-hip-device-codegen --offload-host-only \
// RUN:   -nogpuinc -nogpulib -include %t/pre.h -c %t/use.hip -o %t/use.o 2>&1 \
// RUN: | FileCheck -check-prefix=HOST-ONLY %s
// HOST-ONLY: warning: '-fintegrated-hip-device-codegen' is ignored with '--offload-host-only'
// HOST-ONLY: "-include-pch" "{{[^"]*}}pre.h.pch"

// Neither a precompiled header nor a module is written for several targets.
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -emit-pch -o %t/multi.pch %t/pre.h \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a" \
// RUN:   2>&1 | FileCheck -check-prefix=EMIT-PCH %s
// EMIT-PCH: error: cannot write a precompiled header when compiling for multiple targets at once yet
// RUN: not %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx90a -x hip \
// RUN:   -emit-module-interface -o %t/m.pcm %t/m.hip \
// RUN:   "-multi-target-aux-invocation=-std=c++20 %{hip_device} %{host_aux_triple} -target-cpu gfx90a" \
// RUN:   2>&1 | FileCheck -check-prefix=EMIT-MODULE %s
// EMIT-MODULE: error: cannot write a module when compiling for multiple targets at once yet

//--- pre.h
#define __device__ __attribute__((device))
__device__ inline int twice(int x) { return 2 * x; }

//--- use.hip
__device__ int f(int x) { return twice(x); }

//--- m.hip
export module m;
