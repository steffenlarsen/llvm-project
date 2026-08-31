// Each -multi-target-aux-invocation is parsed as a cc1 command line, and each
// LangOptions field it sets differently from the primary invocation is an
// error by default, unless the field may differ per target.

// Host vs. device differences in fields that may differ per target are not
// diagnosed.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -fsyntax-only -pic-level 2 -pic-is-pie \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -pic-level 2 -fvisibility=hidden -fapply-global-visibility-to-externs -fno-threadsafe-statics -fhalf-no-semantic-interposition -x hip" \
// RUN:   %s 2>&1 | FileCheck --allow-empty --check-prefix=ALLOWED %s
// ALLOWED-NOT: warning:
// ALLOWED-NOT: error:

// An AST-shaping difference is diagnosed once per aux target, naming it by CPU.
//
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -fsyntax-only -fopenmp \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip" \
// RUN:   %s 2>&1 | FileCheck --check-prefix=MISMATCH %s
// MISMATCH: error: language option 'OpenMP' differs between the primary target and aux target 'gfx90a'; the shared AST uses the primary target's value [-Wmulti-target-option-mismatch]
// MISMATCH: error: language option 'OpenMPUseTLS' differs between the primary target and aux target 'gfx90a'
// MISMATCH: error: language option 'OpenMP' differs between the primary target and aux target 'gfx942'
// MISMATCH: error: language option 'OpenMPUseTLS' differs between the primary target and aux target 'gfx942'
// MISMATCH-NOT: error:

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -fsyntax-only -fopenmp -Wno-multi-target-option-mismatch \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   %s 2>&1 | FileCheck --allow-empty --check-prefix=ALLOWED %s

// -mllvm options are process-wide, so an aux invocation's must also be the
// primary's.
//
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -fsyntax-only \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -target-cpu gfx90a -fcuda-is-device -mllvm -inline-threshold=100 -x hip" \
// RUN:   %s 2>&1 | FileCheck --check-prefix=LLVM-ARG-MISSING %s
// LLVM-ARG-MISSING: error: '-mllvm -inline-threshold=100' of aux target 'gfx90a' must also be passed to the primary target
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -fsyntax-only -mllvm --inline-threshold=100 \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -target-cpu gfx90a -fcuda-is-device -mllvm -inline-threshold=100 -x hip" \
// RUN:   %s 2>&1 | FileCheck --allow-empty --check-prefix=ALLOWED %s

// An aux invocation that does not parse is an error.
//
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -fsyntax-only \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -not-a-cc1-flag -x hip" \
// RUN:   %s 2>&1 | FileCheck --check-prefix=INVALID %s
// INVALID: error: invalid multi-target aux invocation '-triple amdgcn-amd-amdhsa -not-a-cc1-flag -x hip': unknown argument: '-not-a-cc1-flag'
