// A local variable's initializer is a LambdaExpr (not a plain call), and the
// lambda's own body declares its own locals while referencing a
// #if-widened, target-divergent callee. This routes through
// rewriteDivergentCalleesInVariableInitializer's Var-mode
// DivergentValueCalleeRewriter (Var == the lambda-valued variable), not the
// whole-body-clone Var==nullptr mode that
// multi-target-wrapper-clone-local-vars.hip exercises. TransformLambdaExpr
// pushes CurContext to the lambda's freshly synthesized call operator before
// transforming its body, so the lambda's own locals need cloning into that
// new call operator even in Var-mode, or later references to them fail with
// err_reference_to_local_in_enclosing_lambda_expression.
//
// REQUIRES: amdgpu-registered-target

// Ground truth: each arch compiled completely on its own.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx900 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX900 %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1100 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX1100 %s

// One primary (gfx942) cc1 job, with AuxGen configured for gfx900 and gfx1100
// and the full merge/reparse pipeline on.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o %t.primary.ll %s \
// RUN:   -mllvm -multi-target-codegen \
// RUN:   -mllvm -multi-target-aux-target=2:gfx900: \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx900.bc \
// RUN:   -mllvm -multi-target-aux-target=3:gfx1100: \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx1100.bc \
// RUN:   -mllvm -multi-target-record \
// RUN:   -mllvm -parse-all-target-alternatives \
// RUN:   -mllvm -widen-target-alternatives \
// RUN:   -mllvm -allow-target-variant-decls \
// RUN:   -mllvm -reparse-divergent-users \
// RUN:   -mllvm -merge-equivalent-variants
// RUN: FileCheck -check-prefix=GFX942 %s < %t.primary.ll
// RUN: llvm-dis -o - %t.gfx900.bc | FileCheck -check-prefix=GFX900 %s
// RUN: llvm-dis -o - %t.gfx1100.bc | FileCheck -check-prefix=GFX1100 %s

#if defined(__gfx942__)
static __attribute__((device)) inline int leaf(int x) { return x + 100; }
#elif defined(__gfx900__)
static __attribute__((device)) inline int leaf(int x) { return x + 200; }
#elif defined(__gfx1100__)
static __attribute__((device)) inline int leaf(int x) { return x + 300; }
#endif

// A local variable (gather_tile) whose own initializer is a lambda literal,
// itself calling the target-divergent leaf() and declaring its own locals
// (global_j, val) that reference each other and the lambda's captured
// parameter -- mirrors ggml-cuda/mmf.cuh's gather_tile shape, which is
// declared directly inside a __global__ function template (mul_mat_f).
// TransformLambdaExpr grants the rebuilt call operator implicit CUDA
// device+host attributes (Sema::SetLambdaAttrsForCUDA) regardless of what
// kind of function encloses the lambda, so this is device-callable the same
// way it would be inside a plain __device__ function template.
template <typename T>
__attribute__((global)) void kernel_tmpl(T *out, T x) {
  int base = (int)x;
  auto gather_tile = [&](int y) {
    int global_j = base + y;
    int val = leaf(global_j);
    int doubled = val * 2;
    return doubled;
  };
  int result = gather_tile((int)x);
  out[0] = (T)result;
}

template __attribute__((global)) void kernel_tmpl<int>(int *, int);

// GFX900: define{{.*}} @_ZL4leafi{{.*}}
// GFX900: add nsw i32 %{{.*}}, 200
// GFX1100: define{{.*}} @_ZL4leafi{{.*}}
// GFX1100: add nsw i32 %{{.*}}, 300
// GFX942: define{{.*}} @_ZL4leafi{{.*}}
// GFX942: add nsw i32 %{{.*}}, 100
