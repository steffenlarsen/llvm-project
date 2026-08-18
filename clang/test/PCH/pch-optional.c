// -fpch-optional makes an implicit PCH a best-effort optimization: when it is
// missing or does not match the current invocation, compile without it instead
// of failing. -Rpch-build reports which happened.
//
// Exercised without any offload toolchain; the HIP driver plumbing that uses
// these options is covered by test/Driver/hip-pch.hip.

// RUN: rm -rf %t && mkdir -p %t/cache

/// Build a PCH that only matches -O0 compilations.
// RUN: %clang_cc1 -O0 -emit-pch -o %t/cache/llvmcache-a.pch \
// RUN:   %S/Inputs/pch-optional.h

/// A matching entry is found and used.
// RUN: %clang_cc1 -O0 -include-pch %t/cache -fpch-optional -Rpch-build \
// RUN:   -fsyntax-only %s 2>&1 | FileCheck -check-prefix=USED %s
// USED: remark: using precompiled header '{{.*}}llvmcache-a.pch'

/// No entry matches -O2, so the headers are parsed instead. Not an error.
// RUN: %clang_cc1 -O2 -include-pch %t/cache -fpch-optional -Rpch-build \
// RUN:   -include %S/Inputs/pch-optional.h -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck -check-prefix=SKIPPED %s
// SKIPPED: remark: not using a precompiled header: no entry in '{{.*}}' matches this compilation

/// Without -fpch-optional the same situation is a hard error.
// RUN: not %clang_cc1 -O2 -include-pch %t/cache -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck -check-prefix=HARD %s
// HARD: error: no suitable precompiled header file found in directory

/// A path that does not exist is tolerated too.
// RUN: %clang_cc1 -O0 -include-pch %t/nonexistent -fpch-optional -Rpch-build \
// RUN:   -include %S/Inputs/pch-optional.h -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck -check-prefix=MISSING %s
// MISSING: remark: not using a precompiled header: '{{.*}}nonexistent' is missing or does not match

/// An entry is keyed on the invocation, not on the contents of the header it
/// was built from, so editing that header leaves the cached name unchanged.
/// The stale entry must be rejected and rebuilt rather than adopted, otherwise
/// -fpch-optional turns into a hard error in the middle of the PCH load.
// RUN: rm -rf %t/auto && mkdir -p %t/auto/cache
// RUN: echo 'int from_prefix_v1(void);' > %t/auto/prefix.h
// RUN: echo 'int use(void) { return from_prefix_v1(); }' > %t/auto/tu.c
// RUN: %clang_cc1 -include-pch %t/auto/cache -fpch-optional \
// RUN:   -fpch-auto-generate=%t/auto/prefix.h -Rpch-build -fsyntax-only \
// RUN:   %t/auto/tu.c 2>&1 | FileCheck -check-prefix=AUTOGEN %s
// AUTOGEN: remark: building precompiled header

/// Same flags, so the same cache entry name; only the header changed. The
/// replacement differs in length as well as content, so the check does not
/// depend on the one-second granularity of the recorded timestamp.
// RUN: echo 'int from_prefix_second_version(void);' > %t/auto/prefix.h
// RUN: echo 'int use(void) { return from_prefix_second_version(); }' > %t/auto/tu.c
// RUN: %clang_cc1 -include-pch %t/auto/cache -fpch-optional \
// RUN:   -fpch-auto-generate=%t/auto/prefix.h -Rpch-build -fsyntax-only \
// RUN:   %t/auto/tu.c 2>&1 | FileCheck -check-prefix=STALE %s
// STALE-NOT: error:
// STALE: remark: building precompiled header

/// Entries are keyed on the invocation's context hash, which covers the
/// target. Two targets must therefore get separate entries, and neither may
/// adopt the other's -- this is what makes one shared cache directory safe for
/// a host plus several device compilations.
// RUN: rm -rf %t/tgt && mkdir -p %t/tgt/cache
// RUN: echo 'int shared_decl(void);' > %t/tgt/prefix.h
// RUN: echo 'int use(void) { return shared_decl(); }' > %t/tgt/tu.c
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -include-pch %t/tgt/cache \
// RUN:   -fpch-optional -fpch-auto-generate=%t/tgt/prefix.h -Rpch-build \
// RUN:   -fsyntax-only %t/tgt/tu.c 2>&1 | FileCheck -check-prefix=TGT1 %s
// TGT1: remark: building precompiled header

/// A different target does not reuse the first entry; it builds its own.
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -include-pch %t/tgt/cache \
// RUN:   -fpch-optional -fpch-auto-generate=%t/tgt/prefix.h -Rpch-build \
// RUN:   -fsyntax-only %t/tgt/tu.c 2>&1 | FileCheck -check-prefix=TGT2 %s
// TGT2: remark: building precompiled header
// RUN: ls %t/tgt/cache/llvmcache-*.pch | count 2

/// Both entries are now warm: each target reuses its own.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -include-pch %t/tgt/cache \
// RUN:   -fpch-optional -fpch-auto-generate=%t/tgt/prefix.h -Rpch-build \
// RUN:   -fsyntax-only %t/tgt/tu.c 2>&1 | FileCheck -check-prefix=TGTWARM %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -include-pch %t/tgt/cache \
// RUN:   -fpch-optional -fpch-auto-generate=%t/tgt/prefix.h -Rpch-build \
// RUN:   -fsyntax-only %t/tgt/tu.c 2>&1 | FileCheck -check-prefix=TGTWARM %s
// TGTWARM: remark: using precompiled header
// RUN: ls %t/tgt/cache/llvmcache-*.pch | count 2

/// -fpch-cache-policy= is honoured by the frontend, not merely forwarded by
/// the driver. An aggressive policy evicts the existing entries; the current
/// invocation then rebuilds its own, so exactly one is left.
// RUN: ls %t/tgt/cache/llvmcache-*.pch | count 2
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -include-pch %t/tgt/cache \
// RUN:   -fpch-optional -fpch-auto-generate=%t/tgt/prefix.h -Rpch-build \
// RUN:   -fpch-cache-policy=prune_interval=0s:cache_size_bytes=1 \
// RUN:   -fsyntax-only %t/tgt/tu.c 2>&1 | FileCheck -check-prefix=PRUNED %s
// PRUNED: remark: building precompiled header
// RUN: ls %t/tgt/cache/llvmcache-*.pch | count 1

/// Errors in the nominated header are reported rather than silently costing
/// the cache.
// RUN: rm -rf %t/bad && mkdir -p %t/bad/cache
// RUN: echo 'this is not valid C' > %t/bad/prefix.h
// RUN: echo 'int standalone(void) { return 0; }' > %t/bad/tu.c
// RUN: %clang_cc1 -include-pch %t/bad/cache -fpch-optional \
// RUN:   -fpch-auto-generate=%t/bad/prefix.h -Rpch-build -fsyntax-only \
// RUN:   %t/bad/tu.c 2>&1 | FileCheck -check-prefix=BADHEADER %s
// BADHEADER: remark: not using a precompiled header: could not build '{{.*}}': unknown type name 'this'
// BADHEADER-NOT: error:

/// The cache directory is created on first use, so a fresh checkout does not
/// need one to exist.
// RUN: rm -rf %t/fresh
// RUN: echo 'int fresh_decl(void);' > %t/fresh-prefix.h
// RUN: echo 'int use(void) { return fresh_decl(); }' > %t/fresh-tu.c
// RUN: %clang_cc1 -include-pch %t/fresh -fpch-optional \
// RUN:   -fpch-auto-generate=%t/fresh-prefix.h -Rpch-build -fsyntax-only \
// RUN:   %t/fresh-tu.c 2>&1 | FileCheck -check-prefix=FRESHDIR %s
// FRESHDIR: remark: building precompiled header
// RUN: ls %t/fresh/llvmcache-*.pch | count 1

/// A nominated header that does not exist is not an error either; there is
/// simply nothing to precompile.
// RUN: rm -rf %t/noheader && mkdir -p %t/noheader/cache
// RUN: %clang_cc1 -include-pch %t/noheader/cache -fpch-optional \
// RUN:   -fpch-auto-generate=%t/noheader/absent.h -Rpch-build -fsyntax-only \
// RUN:   %t/bad/tu.c 2>&1 | FileCheck -check-prefix=NOHEADER %s
// NOHEADER: remark: not using a precompiled header: no entry in '{{.*}}' matches
// NOHEADER-NOT: error:

/// Remarks are off unless requested.
// RUN: %clang_cc1 -O2 -include-pch %t/cache -fpch-optional \
// RUN:   -include %S/Inputs/pch-optional.h -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck -check-prefix=QUIET --allow-empty %s
// QUIET-NOT: remark

int use_it(void) { return from_pch; }
