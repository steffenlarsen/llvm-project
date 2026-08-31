# RFC: Single-Frontend Multi-Target Codegen for HIP (`-fintegrated-hip-device-codegen`)

## Summary

This proposal adds a driver flag, `-fintegrated-hip-device-codegen`, that lets a HIP
compilation targeting multiple GPU architectures share a single frontend pass (lexing,
parsing, and semantic analysis) across all requested `--offload-arch=` values, instead of
running the frontend once per architecture. Code generation still happens once per
architecture, but it operates on one shared AST rather than one AST per arch.

The change is purely a compile-time optimization. It introduces no new source-level
capability: a HIP translation unit that compiles today via repeated
`--offload-arch=` invocations compiles to the same object code under this flag, just
faster. There is no change to the language accepted, to diagnostics for ordinary
single-target compiles, or to the object code produced for architectures that do not
diverge in source.

## Motivation

Compiling a HIP translation unit for N GPU architectures normally means running the full
clang frontend N times: N token streams, N parses, N `Sema` passes, N sets of template
instantiations. For projects with heavy device-side template use (matrix-multiply and
attention kernels built from templated tile types, for example), the parse and semantic
analysis cost is duplicated N times even though the vast majority of the translation
unit's declarations are architecture-independent.

The goal is to do that shared work once. Only the code that is genuinely
architecture-specific — content inside `#if defined(__gfxNNN__)`-style regions, or a
template specialization that only resolves differently under host vs. device or between
device architectures — needs architecture-specific treatment. Everything else should be
parsed, resolved, and instantiated exactly once and reused for every architecture's
code generation.

### Non-goals

* **Not a new source-level feature.** No new syntax, attributes, or diagnostics are
  introduced. Ordinary `--offload-arch=X --offload-arch=Y` compiles without this flag are
  completely unaffected — none of this machinery is reachable without it.
* **Not a general multi-target C++ frontend.** The mechanism is scoped to what HIP device
  code compiled from a real GPU-kernel corpus actually needs: `#if`/`#elif`/`#else`
  divergence keyed on architecture macros, and template instantiations that pick a
  different specialization or member per architecture. It does not attempt to support
  arbitrary preprocessor conditionals unrelated to target selection, nor does it attempt
  full symmetry between `if constexpr`/`static_assert`-based branching and `#if`-based
  branching — the corpus this was validated against is exclusively `#if`-based.
* **Not PCH/serialization-ready, and not wired into external tooling.** Precompiled
  headers, clangd, clang-tidy, and libclang do not currently know about multi-target
  declarations. This is scoped out for now (see Limitations).

## Design overview

The change spans five areas of the compiler, in roughly the order data flows through
them.

### 1. Driver

`clang/lib/Driver/ToolChains/Clang.cpp` derives the flags needed to enable multi-target
compilation directly from the `--offload-arch=` arguments already given for a HIP
compile — no new user-facing flags beyond `-fintegrated-hip-device-codegen` itself are
required. It computes the aux-target list and emits the corresponding `-mllvm
-multi-target-*` options to the single `-cc1` invocation that replaces what would
otherwise be N separate `-cc1` invocations (one per architecture) feeding into HIP's
usual fat-binary bundling step.

### 2. Lexing and parsing: token-stream widening

`clang/lib/Lex/TokenStreamMerge.cpp` and `clang/lib/Parse/ParseAST.cpp` implement the
mechanism that lets one parse pass see all architectures' code. Architecture-conditioned
regions (`#if defined(__gfx942__)` / `#elif defined(__gfx900__)` / `#else`, and similar)
are recorded once per architecture during preprocessing, then aligned and merged into a
single token stream in which shared regions appear once and divergent regions are
retained as separate, tagged alternatives. `clang/lib/Parse/TargetAlternationReconciler.h`
handles the recursive case — divergent regions nested inside a namespace, class body, or
`extern "C"` block, not just at file scope.

Where a declaration's shape is too different between architectures to represent as one
parsed node (for example, a class template whose entire body differs per architecture),
the parser retains multiple tagged redeclarations rather than forcing a merge; where the
declarations are equivalent, they are merged into one to avoid needless duplication in
the AST.

### 3. AST: target tagging

`clang/include/clang/AST/DeclBase.h` adds a `TargetVariant` field to `Decl`, an integer
tag identifying which architecture (or the shared/host case) a particular
redeclaration belongs to. Variant `0` means "not target-specific" (the common case for
the bulk of any translation unit). Nonzero variants identify one specific real target;
numbering runs host/primary first, then one slot per auxiliary (device) architecture.

`clang/lib/AST/DeclTemplate.cpp` extends `ClassTemplateSpecializationDecl::Profile()` to
fold the active target into a specialization's identity when the specialization's
governing template has target-tagged partial specializations, value members, or (see
below) a target-tagged primary template — so the same instantiation request
(`Template<Args...>`) can correctly produce distinct specializations under different
architectures, without changing the identity of any specialization request in an
ordinary, non-multi-target compile.

### 4. Sema: visibility and instantiation redirection

This is the largest and most involved part of the change (`clang/lib/Sema/SemaLookup.cpp`,
`clang/lib/Sema/SemaTemplateInstantiate.cpp`,
`clang/lib/Sema/SemaTemplateInstantiateDecl.cpp`).

**Visibility filtering.** `isVisibleForTarget` (`SemaLookup.cpp`) is the central predicate
that decides, given a target-tagged declaration and the architecture currently being
resolved for, whether that declaration should be visible to lookup. It is consulted by
ordinary unqualified/qualified lookup, by argument-dependent lookup, and by
partial-specialization pattern selection. When resolving for a genuinely device-only
context, the enclosing function's own CUDA/HIP target classification is used to pick a
concrete architecture rather than falling back to a fixed default — necessary because a
single shared caller can be compiled from a Sema pass that has no single fixed target of
its own (its body will be re-emitted per architecture later, at code generation time).

**Instantiation-time redirection.** A class template or function whose body was written
once but references something target-divergent (a differently-typed template argument
alias, a static value member whose value differs per architecture, a callee that only
resolves to the right overload under one architecture) is elaborated by Sema exactly
once. `Sema::InstantiateDivergentCalleesInBody` walks that already-elaborated body,
finds references of this kind, and for each real architecture re-resolves the reference
under that architecture's context, recording the correct per-architecture answer in a
passive side table on `ASTContext` (`TargetVariantDeclRedirects` and
`TargetVariantConstantValues`) for code generation to consult. This same walk also
covers attribute arguments (for example, `__launch_bounds__`'s thread-count expression)
that reference target-divergent callees.

### 5. CodeGen: per-architecture replay ("AuxGen")

`clang/lib/CodeGen/CodeGenAction.cpp` drives code generation once per real architecture
over the single shared AST. For a declaration whose Sema resolution was correct for
every architecture already, generation is unchanged. Where Sema's instantiation-redirect
side tables recorded a different answer for a given architecture, the relevant CodeGen
entry points (`CGExpr.cpp`'s callee and constant-emission paths) consult those tables
before falling back to the value baked in by the original elaboration. CodeGen itself
never re-runs Sema or re-triggers instantiation — it only performs passive table lookups
against what Sema already computed.

### Where the change lives

Diff against the prior commit, by area:

| Area | Files changed |
|---|---|
| `include/clang` (headers) | 17 |
| `lib/Sema` | 14 |
| `lib/CodeGen` | 10 |
| `lib/AST` | 9 |
| `lib/Parse` | 6 |
| `lib/Frontend` | 4 |
| `lib/Lex` | 3 |
| `lib/Driver` | 2 |
| `test/CodeGenHIP` | ~24 target-specific lit tests, plus shared infrastructure tests |
| `unittests/Lex`, `unittests/AST` | 2 each |

103 files changed, +12138/-172 lines. The center of gravity is Sema (visibility and
instantiation machinery) and AST (declaration tagging and specialization identity), with
CodeGen (per-architecture replay) and Parse (token-stream widening) as the next largest
pieces — matching the description above.

## Correctness verification

Three layers of verification were used, from cheapest/most-automatable to most
expensive/most direct:

1. **Compiler-level regression tests.** Unit tests for the token-stream merge machinery,
   and lit tests under `clang/test/CodeGenHIP/multi-target-*.hip` exercising each
   mechanism above in isolation (partial-specialization visibility, value-member
   redirection, alias-member redirection, static-method redirection, argument-dependent
   lookup filtering, attribute-argument redirection, and more). These run alongside the
   rest of the existing Clang/Sema/Parser/Driver test suites and `AllClangUnitTests`.
2. **Inertness on ordinary compiles.** A CTMark instruction-count and byte-identical
   object-file comparison, gating that none of this machinery changes output for a
   compile that does not use `-fintegrated-hip-device-codegen`. All of the new code
   paths are additive and guarded behind the feature being enabled; this gate confirms
   that guard is effective.
3. **Real-corpus and real-hardware validation**, against `llama.cpp`'s `ggml-hip`
   backend (154 real HIP source files, one of the more template-heavy real-world HIP
   codebases available):
   - A 65-translation-unit sweep comparing the combined-frontend build's per-architecture
     IR against IR from ordinary independent per-architecture compiles, file by file.
   - A full, from-scratch CMake build of `llama.cpp` for two real architectures
     (`gfx90a`, `gfx942`), producing a genuine dual-architecture `libggml-hip.so`
     (confirmed via inspection of the embedded offload bundles).
   - Execution on physical AMD Instinct MI210 (`gfx90a`) hardware: a 110-chunk
     perplexity run and a 40-token greedy-decode run, each compared byte-for-byte
     against the same model run through a traditionally-built (one frontend pass per
     architecture) binary. Both matched exactly.

## Performance

Measurements below compare a combined-frontend build against a traditional
(`--offload-arch=` invoked once per architecture, today's existing behavior) build of
the same `llama.cpp` checkout, same compiler binary, same CMake configuration except for
the flag under test. The `ggml-hip` target (the only target affected by this change) was
rebuilt from a clean state three times per configuration; variance between repeated runs
of the same configuration was under 0.1%.

| Architectures | CPU-time delta | Wall-clock delta |
|---|---|---|
| 1 (`gfx942`) | -13.8% | -5.1% |
| 2 (`gfx90a`, `gfx942`) | -16.3% | -5.7% |
| 3 (`gfx90a`, `gfx942`, `gfx908`) | -16.15% | **+3.9%** |

CPU-time (total work performed, summed across all parallel compile workers) is the more
direct measurement of the mechanism being changed, since it is insensitive to link-step
tail latency and scheduling effects at high parallelism. The two- and three-architecture
CPU-time results are consistent with each other and with the mechanism's premise: sharing
the frontend pass avoids repeating it per architecture, and that saving does not erode as
architecture count grows in this range.

The three-architecture wall-clock result is a genuine, reproducible reversal (the
combined build is slower in wall-clock despite doing less total work), not measurement
noise — repeated trials in both directions were tight (well under 0.1% variance) in both
configurations. This has not been root-caused. Plausible explanations that have not been
investigated include the per-architecture code-generation replay becoming
serialization-bound as architecture count grows, or a difference in how the two build
shapes interact with the build system's parallelism at this corpus size. This should be
treated as an open item, not a resolved one, before drawing conclusions about scaling
behavior beyond two architectures.

Whole-build (673-target) wall-clock is nearly unchanged between configurations, as
expected — the large majority of targets in a full `llama.cpp` build are ordinary host
compiles unaffected by this flag.

## Known limitations and open risks

* **Argument-dependent lookup coverage is not fully audited.** Ordinary and qualified
  lookup are filtered through `isVisibleForTarget` at every call site. Argument-dependent
  lookup gathers its own candidate set independently and was found to bypass that
  filtering in one specific case (multiple architecture-tagged overloads of the same
  free function becoming simultaneously visible, producing a spurious "ambiguous call").
  A fix was applied at the one call site (`AddArgumentDependentLookupCandidates`) where
  this was observed, and it resolves the concrete case tested. Whether other
  argument-dependent-lookup call shapes can hit the same gap has not been exhaustively
  determined; this should be treated as a structural risk in the ADL path generally,
  not a fully closed issue.
* **Only one real target architecture has been execution-tested.** IR- and byte-level
  comparisons cover every architecture in the corpus, but actual execution on real
  hardware was only possible for `gfx90a` (a single physical GPU was available). The
  `gfx942` device code embedded in the same binary has not been execution-verified,
  only compared at the IR/object level against independently-compiled reference output.
* **Three-architecture wall-clock regression is unexplained** (see Performance above).
* **PCH/serialization is not supported.** Multi-target declarations are not currently
  serializable; this flag is only usable for from-source, non-PCH compiles.
* **External tooling (clangd, clang-tidy, libclang) does not understand multi-target
  declarations.** Using this flag with these tools is untested and not a supported
  configuration.
* **Target-variant tagging is scoped to what the validated corpus needed.** The
  mechanism is proven for function and class-template declarations reached through the
  patterns found in `ggml-hip`. Other declaration kinds (for example, target-divergent
  namespace-scope variables or typedefs outside a template) were not encountered in the
  validation corpus and have not been separately exercised.

## Alternatives considered

* **Do nothing — keep invoking the frontend once per architecture.** This already works
  today with no code changes, and is strictly simpler. It was rejected only because the
  duplicated frontend cost is the specific problem this proposal addresses; for
  architecture counts and corpora where frontend cost is not a bottleneck, this remains
  the simpler and better-supported option.
* **PCH-based sharing instead of a single combined pass.** Precompiled headers already
  reduce duplicated parsing cost for content that is architecture-independent and stable
  across compiles. This proposal does not conflict with that approach but does not rely
  on it either; PCH support for multi-target declarations is left as future work.

## Open questions

* Is the scope of ADL-visibility filtering (one fixed chokepoint) sufficient, or does
  the general risk described above warrant a broader audit or a different enforcement
  point (for example, filtering at the point ADL's result set is unioned with ordinary
  lookup's, rather than only inside candidate gathering)?
* What is the right long-term posture on tooling support (clangd/clang-tidy/libclang)
  given this flag is intended for build-time use, not interactive editing?
* Does the three-architecture wall-clock regression indicate a real scaling limit that
  should shape guidance on how many architectures this flag is recommended for, pending
  root-causing?
