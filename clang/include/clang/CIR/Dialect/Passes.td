//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_CIR_DIALECT_PASSES_TD
#define CLANG_CIR_DIALECT_PASSES_TD

include "mlir/Pass/PassBase.td"

def CIRCanonicalize : Pass<"cir-canonicalize"> {
  let summary = "Performs CIR canonicalization";
  let description = [{
    Perform canonicalizations on CIR and removes some redundant operations.

    This pass performs basic cleanup and canonicalization transformations that
    are not intended to affect CIR-to-source fidelity and high-level code
    analysis passes. Example transformations performed in this pass include
    empty scope cleanup, trivial `try` cleanup, redundant branch cleanup, etc.
    Those more "heavyweight" transformations and those transformations that
    could significantly affect CIR-to-source fidelity are performed in the
    `cir-simplify` pass.
  }];

  let constructor = "mlir::createCIRCanonicalizePass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def CIRSimplify : Pass<"cir-simplify"> {
  let summary = "Performs CIR simplification and code optimization";
  let description = [{
    The pass performs semantics-preserving code simplifications and optimizations
    on CIR while maintaining strict program correctness.

    Unlike the `cir-canonicalize` pass, these transformations may reduce the IR's
    structural similarity to the original source code as a trade-off for improved
    code quality. This can affect debugging fidelity by altering intermediate
    representations of folded expressions, hoisted operations, and other
    optimized constructs.

    Example transformations include ternary expression folding and code hoisting
    while preserving program semantics.
  }];
  let constructor = "mlir::createCIRSimplifyPass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def HoistAllocas : Pass<"cir-hoist-allocas"> {
  let summary = "Hoist allocas to the entry of the function";
  let description = [{
    This pass hoist all non-dynamic allocas to the entry of the function.
    This is helpful for later code generation.
  }];
  let constructor = "mlir::createHoistAllocasPass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def CIRFlattenCFG : Pass<"cir-flatten-cfg"> {
  let summary = "Produces flatten CFG";
  let description = [{
    This pass transforms CIR by inlining all the nested regions. Thus,
    the following conditions are true after the pass applied:
    - there are no nested regions in any function body
    - all the blocks in a function belong to the parent region
    In other words, this pass removes such CIR operations like IfOp, LoopOp,
    ScopeOp and etc. and produces a flat CIR.
  }];
  let constructor = "mlir::createCIRFlattenCFGPass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def GotoSolver : Pass<"cir-goto-solver"> {
  let summary = "Replaces goto operations with branches";
  let description = [{
    This pass transforms CIR and replaces goto-s with branch
    operations to the proper blocks.
  }];
  let constructor = "mlir::createGotoSolverPass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def CXXABILowering : Pass<"cir-cxxabi-lowering", "mlir::ModuleOp"> {
  let summary = "Lower CIR according to C++ ABI requirements";
  let description = [{
    This pass lowers CIR operations and types that represent high-level C/C++
    constructs to a more fundamental form according to the target ABI
    requirements.

    See the description of the `TargetLowering` pass for the difference between
    this pass and the `TargetLowering` pass.
  }];
  let constructor = "mlir::createCXXABILoweringPass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def TargetLowering : Pass<"cir-target-lowering", "mlir::ModuleOp"> {
  let summary = "Lower CIR to a target-specific form";
  let description = [{
    This pass lowers CIR operations from a target-agnostic form to a
    target-specific form without considering ABI requirements.

    CIR has three passes in its lowering pipeline that transform input CIR
    according to target-specific requirements, scheduled in the pipeline by the
    following order:

      1. The `TargetLowering` pass.
      2. The `CXXABILowering` pass.
      3. The `CallConvLowering` pass.

    The `TargetLowering` pass acts more like a legalization pass. It ensures
    every operation in CIR conforms to the target's constraints. An example
    would be the handling of synchronization scopes of atomic operations. The
    x86 family of targets only support a system-wide synchronization scope, thus
    any atomic operations with a different synchronization scope would be
    transformed to use the system-wide scope in this pass.

    The `CXXABILowering` pass and the `CallConvLowering` pass transform the
    CIR according to the target's ABI requirements. The former handles all
    ABI-related lowering except for calling convention handling, which is
    handled specifically in the latter. Example transformations that the
    `CXXABILowering` pass could make include:

      - Replace C/C++ types that have an ABI-defined layout with more
        fundamental types corresponding to the ABI requirements. For example,
        the layout of the pointer-to-data-member type in C++ is ABI-defined,
        thus the `CXXABILowering` pass would replace all occurrences of this
        type according to the ABI requirements. With a typical target ABI, it
        will be replaced by the `ptrdiff_t` type since this is the type most
        ABIs use for the layout of the pointer-to-data-member type.
      - Replace CIR operations that have ABI-dependent implementations with more
        fundamental operations. For example, the `dynamic_cast` operator in C++
        is implemented in an ABI-dependent way, typically by calling into a
        library function provided by the implementation. The `CXXABILowering`
        pass would thus replace all `cir.dyn_cast` operations with corresponding
        library function calls.

    The `CallConvLowering` pass is dedicated to handle calling conventions. It
    rewrites function signatures according to calling convention requirements,
    and updates function body and call sites accordingly. For example, when
    passing a large struct by value in C/C++, most ABIs require passing a
    pointer to a copy of the struct instead. Thus, the `CallConvLowering` pass
    would rewrite function signatures that take a value of a large struct type
    to take a pointer to the struct type instead, and update the function body
    and the call sites accordingly.
  }];
  let constructor = "mlir::createTargetLoweringPass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def CIREHABILowering : Pass<"cir-eh-abi-lowering", "mlir::ModuleOp"> {
  let summary = "Lower flattened CIR EH operations to target-specific ABI form";
  let description = [{
    This pass lowers the ABI-agnostic exception handling operations produced by
    CFG flattening into an ABI-specific form. Currently only the Itanium C++
    ABI is implemented.

    For the Itanium ABI, the pass performs the following transformations:
      - Replaces `cir.eh.initiate` with `cir.eh.inflight_exception`
      - Replaces `cir.eh.dispatch` with `cir.eh.typeid` + comparison chains
      - Removes `cir.begin_cleanup` and `cir.end_cleanup` operations
      - Replaces `cir.begin_catch` with a call to `__cxa_begin_catch`
      - Replaces `cir.end_catch` with a call to `__cxa_end_catch`
      - Replaces `cir.resume` with `cir.resume.flat`
      - Sets the personality function attribute on functions that require EH

    If a non-Itanium ABI is specified, the pass emits a diagnostic indicating
    that the target is not yet implemented.
  }];
  let constructor = "mlir::createCIREHABILoweringPass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def LoweringPrepare : Pass<"cir-lowering-prepare"> {
  let summary = "Lower to more fine-grained CIR operations before lowering to "
    "other dialects";
  let description = [{
    This pass does preparation work for lowering to other dialects. For example,
    it may expand the global variable initialziation in a more ABI-friendly form.
  }];
  let constructor = "mlir::createLoweringPreparePass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def CUDARegisterModule : Pass<"cir-cuda-register-module", "mlir::ModuleOp"> {
  let summary = "Build CUDA module registration runtime calls";
  let description = [{
    This pass emits the CUDA/HIP module constructor, destructor, fatbin globals,
    and kernel/device variable registration calls from serialized CIR CUDA
    registration attributes.
  }];
  let constructor = "mlir::createCUDARegisterModulePass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def MaterializeASTFacts : Pass<"cir-materialize-ast-facts", "mlir::ModuleOp"> {
  let summary = "Cache AST-derived facts as CIR attributes";
  let description = [{
    Walks the module and, for any `cir.global` whose `ast` attribute is an
    AST-backed `ASTVarDeclAttr`, replaces it with a `StaticLocalInfoAttr`
    that carries the same facts (isLocalVarDecl, TLSKind, isInline,
    TemplateSpecializationKind) as plain fields. After this pass runs,
    downstream passes such as `cir-lowering-prepare` can query the
    `ASTVarDeclInterface` without a live `clang::ASTContext`.

    Intended to run while the AST is still reachable, i.e. immediately after
    CIRGen and before any step that may serialize CIR for later consumption
    by a separate tool invocation.
  }];
  let constructor = "mlir::createMaterializeASTFactsPass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def OffloadDeadKernelElimination
    : Pass<"cir-offload-dead-kernel-elimination", "cir::OffloadContainerOp"> {
  let summary = "Remove device kernels that can never be launched";
  let description = [{
    Runs over a `cir.offload.container` and deletes device kernels that are
    provably unreachable, using the host<->device correspondence from
    `KernelBindingTable`.

    A kernel is removed when it is not launchable outside this container:

      - a host stub with internal linkage and no launch left (its device
        kernels die with it), or
      - a device kernel with no host stub at all -- a static/anonymous kernel
        never launched in this TU, since an externally-linked kernel always
        emits an external stub -- provided device code does not still
        reference it (dynamic parallelism / address-taken).

    Externally-linked kernels are always preserved: another TU may launch them
    through their external host stub. Scoped to the non-RDC pipeline.

    Dynamic parallelism follows NVIDIA's terminology: kernel A launches kernel B
    from device code, so B has no host stub -- yet B is live and must be kept.
    https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/dynamic-parallelism.html
  }];
  let constructor = "mlir::createOffloadDeadKernelEliminationPass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def IdiomRecognizer : Pass<"cir-idiom-recognizer", "mlir::ModuleOp"> {
  let summary = "Raise calls to C/C++ libraries to CIR operations";
  let description = [{
    This pass recognizes idiomatic C++ usage and captures information about C++
    standard library containers, library functions calls, and types into CIR
    operations, attributes and types.
  }];
  let constructor = "mlir::createIdiomRecognizerPass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def LibOpt : Pass<"cir-lib-opt"> {
  let summary = "Optimize C/C++ library calls";
  let description = [{
    This pass applies transformations on C/C++ standard library idioms,
    such as library function calls and structs raised to CIR operations
     using the `cir-idiom-recognize` pass.
  }];

  let constructor = "mlir::createLibOptPass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def CallConvLowering : Pass<"cir-call-conv-lowering", "mlir::ModuleOp"> {
  let summary = "Lower CIR function signatures and call sites to match target ABI";
  let description = [{
    This pass rewrites `cir.func` signatures and `cir.call` sites to match the
    target ABI's calling convention requirements (extension attributes, struct
    coercion, sret/byval indirect passing, struct flattening, etc.).

    Requirements on the input IR:

    - All C++-specific CIR types must already be lowered to ABI-relevant
      shapes.  In particular, member pointers and method types must have
      been rewritten by `CXXABILowering` so that the pass sees only the
      final ABI representation of each value.
    - The module must carry a `dlti.dl_spec` attribute so the classifier
      can query type sizes and alignments.

    Two driver modes select how each function's classification is computed:

    - `target=<name>` selects an ABI target.  Currently only `"test"` (the
      MLIR test target in `mlir/lib/ABI/Targets/Test/`) is supported.  Real
      targets (x86_64, AArch64, ...) will be added once the LLVM ABI library
      ships them.
    - `classification-attr=<name>` reads a `DictionaryAttr` named `<name>`
      from each `cir.func` and parses it via the test-target injection
      helper.  Used by tests to inject arbitrary classifications without
      depending on a real ABI target.

    Exactly one of the two options must be set.
  }];
  let constructor = "mlir::createCallConvLoweringPass()";
  let dependentDialects = ["cir::CIRDialect"];
  let options = [
    Option<"target", "target", "cir::CallConvTarget",
           /*default=*/"cir::CallConvTarget::None",
           "Target whose ABI rules drive classification",
           [{::llvm::cl::values(
             clEnumValN(cir::CallConvTarget::Test, "test",
                        "MLIR test ABI target"),
             clEnumValN(cir::CallConvTarget::X86_64, "x86_64",
                        "x86_64 System V")
           )}]>,
    Option<"classificationAttr", "classification-attr", "std::string",
           /*default=*/"\"\"",
           "Function attribute name carrying a pre-built FunctionClassification "
           "DictionaryAttr (alternative to target=, used by tests)">,
    Option<"x86AvxAbiLevel", "x86-avx-abi-level", "llvm::abi::X86AVXABILevel",
           /*default=*/"llvm::abi::X86AVXABILevel::None",
           "AVX ABI level for the x86_64 target",
           [{::llvm::cl::values(
             clEnumValN(llvm::abi::X86AVXABILevel::None, "none", "No AVX"),
             clEnumValN(llvm::abi::X86AVXABILevel::AVX, "avx", "AVX"),
             clEnumValN(llvm::abi::X86AVXABILevel::AVX512, "avx512", "AVX512")
           )}]>,
  ];
}

def OffloadKernelArgConstantPropagation
    : Pass<"cir-offload-kernel-arg-const-prop", "cir::OffloadContainerOp"> {
  let summary = "Propagate constant launch arguments into device kernels";
  let description = [{
    Runs over a `cir.offload.container`. When every launch of a kernel passes
    the same constant for a given argument, the uses of that argument inside
    the device kernel are replaced by the constant.

    Only device code is rewritten. The kernel signature, the host argument
    buffer and the registration of the kernel are all left alone, so the
    argument simply becomes unused on the device side.

    Rewriting the kernel in place requires every launch of it to be visible.
    That holds when the host stub has internal linkage, so no other TU can call
    it, and every reference to the stub outside its own body is a direct call:
    a stub whose address escapes may be called indirectly with arguments this
    pass cannot see. The stub always references itself, since it passes its own
    address to `cudaLaunchKernel`. When a launch could be hidden, a clone of the
    kernel is specialised instead and the visible launches are retargeted at it,
    leaving the original to serve the rest.

    Requires the launch arguments to already be constants at the call site,
    which for anything but a literal means `mem2reg` and `sccp` have run.
  }];
  let constructor = "mlir::createOffloadKernelArgConstantPropagationPass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def OffloadSpecializeLaunchWrappers
    : Pass<"cir-offload-specialize-launch-wrappers", "cir::OffloadContainerOp"> {
  let summary = "Clone kernel-launch wrappers per constant kernel argument";
  let description = [{
    Runs over a `cir.offload.container`. A helper that takes the kernel as a
    parameter and launches it -- the `launch(kernel, grid, block, args...)`
    idiom -- launches through an indirect `cir.call`, which carries no
    `cu.kernel_name` and so is invisible to every other offload pass.

    This clones such a helper once per kernel its callers pass, pins the kernel
    parameter in the clone, and rewrites the indirect launch into the direct,
    tagged call a source-level `<<<>>>` produces. The launch is then an
    ordinary binding-table entry that the specialising passes can act on.

    Runs before the passes that consume launch sites, and iterates: pinning an
    outer wrapper's parameter is what exposes an inner one.

    Call sites that do not pass a recognisable kernel keep calling the
    original, which is left untouched.
  }];
  let constructor = "mlir::createOffloadSpecializeLaunchWrappersPass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def OffloadSpecializeConstantArgs
    : Pass<"cir-offload-specialize-constant-args", "cir::OffloadContainerOp"> {
  let summary = "Clone launch helpers per constant scalar argument";
  let description = [{
    Runs over a `cir.offload.container`. Launch arguments are often literals one
    call frame above the launch: a thin entry point forwards 1 for the higher
    dimensions into a general helper that does the launch, so at the launch site
    the extent is a block argument and looks like a runtime value.

    This clones such a helper once per distinct set of integer constants its
    callers pass, pins them in the clone, and retargets those callers. The
    literals then sit at the launch site, where kernel-argument constant
    propagation can bake them into the kernel. Only parameters that actually
    reach a launch are pinned.

    No guard is needed: the values are compile-time constants, so the retargeted
    callers are exactly the ones a clone is valid for, and any caller that does
    not pass constants keeps the original. A helper all of whose callers moved
    to clones is erased, so that its abandoned launch site cannot defeat the
    unanimity rule downstream.

    Runs before the passes that consume launch sites.
  }];
  let constructor = "mlir::createOffloadSpecializeConstantArgsPass()";
  let dependentDialects = ["cir::CIRDialect"];
}


def OffloadPropagatePointerFacts
    : Pass<"cir-offload-propagate-pointer-facts", "cir::OffloadContainerOp"> {
  let summary = "State pointer alignment, noalias and access mode on kernel arguments";
  let description = [{
    Runs over a `cir.offload.container`. A kernel's pointer parameters carry no
    information about how they are aligned, whether they can overlap, or how the
    kernel reaches memory through them -- but the host knows, because it
    allocated the buffers and wrote the launch.

    Traces each pointer launch argument back to the allocation it came from and,
    when every visible launch agrees, states on the kernel parameter:

      * `llvm.align`, since every HIP allocator returns 256-byte aligned memory;
      * `llvm.noalias`, when the parameter comes from a different allocation
        than every other pointer parameter at every launch;
      * `llvm.readonly` / `llvm.writeonly`, from how the kernel body uses it.

    These are real LLVM parameter attributes: alias analysis, the vectoriser and
    load/store widening all consume them.

    Applies to the kernel in place when every launch is visible, on a
    specialised clone otherwise.
  }];
  let constructor = "mlir::createOffloadPropagatePointerFactsPass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def OffloadPropagateBlockShape
    : Pass<"cir-offload-propagate-block-shape", "cir::OffloadContainerOp"> {
  let summary = "Propagate the launch block shape into device kernels";
  let description = [{
    Runs over a `cir.offload.container`. When every launch of a kernel uses the
    same constant block dimension, reads of `blockDim` for that dimension
    inside the device kernel are replaced by the constant.

    A `blockDim` read is a call here rather than an operation, and is matched
    both as the accessor the HIP/CUDA headers define and as the device-library
    entry point it inlines to, so the pass gives the same answer on either side
    of inlining.

    Turning `blockDim.x` into a literal is what lets the rest of the pipeline
    fold the index arithmetic built on top of it, which is where the payoff
    is; the load itself is cheap.

    Soundness follows the same rule as constant argument propagation: in place
    when every launch is visible, on a specialised clone otherwise.
  }];
  let constructor = "mlir::createOffloadPropagateBlockShapePass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def OffloadTightenLaunchBounds
    : Pass<"cir-offload-tighten-launch-bounds", "cir::OffloadContainerOp"> {
  let summary = "Narrow a kernel's promised maximum block size";
  let description = [{
    Runs over a `cir.offload.container`. When every launch of a kernel uses a
    constant block shape, the `cir.amdgpu-flat-work-group-size` maximum on the
    device kernel is lowered to the largest block any of those launches
    produces.

    The attribute is a promise to the backend about how many threads a block
    can hold, and a smaller promise lets it give each thread more registers.
    CIRGen sets the maximum to `--gpu-max-threads-per-block` (1024 by default)
    for any kernel without `__launch_bounds__`, which is usually far above what
    the program actually launches.

    The bound has to cover every launch that reaches the kernel, so a single
    launch with an unresolved block dimension suppresses the whole kernel
    rather than yielding a bound derived from the readable launches. The
    minimum is left as CIRGen set it, since that half of the attribute is the
    user's statement rather than this pass's.

    Only AMDGPU kernels carry the attribute; kernels for other targets are
    left alone.

    Soundness follows the same rule as constant argument propagation: in place
    when every launch is visible, on a specialised clone otherwise.
  }];
  let constructor = "mlir::createOffloadTightenLaunchBoundsPass()";
  let dependentDialects = ["cir::CIRDialect"];
}

def OffloadEliminateCoveredGuards
    : Pass<"cir-offload-eliminate-covered-guards", "cir::OffloadContainerOp"> {
  let summary = "Prove tail guards dead when the launch covers the range exactly";
  let description = [{
    Runs over a `cir.offload.container`. A kernel that covers `n` elements in
    blocks of `d` is launched with `gridDim = ceil(n/d)` and carries tail
    guards -- `if (index >= n) return;` -- for the final, partially populated
    block. When `n % d == 0` that block is full and every such guard is dead.

    The fact needed is `d*blockIdx.x + rest < n`, which relates two runtime
    unknowns. LLVM's value analyses are per-value interval domains and cannot
    use it, so the proof is done here and the comparison folded outright rather
    than described to the backend with an assume.

    A clone is specialised with the guards folded away and the launches are
    dispatched to it behind a runtime `n % d == 0`, so the copy is only entered
    where its premise holds and the original keeps serving every other case.

    When the index arithmetic is evaluated in fewer bits than the comparison,
    the dispatch also requires `n` to be small enough that the narrow product
    cannot wrap, since the proof assumes it does not.
  }];
  let constructor = "mlir::createOffloadEliminateCoveredGuardsPass()";
  let dependentDialects = ["cir::CIRDialect"];
}


#endif // CLANG_CIR_DIALECT_PASSES_TD
