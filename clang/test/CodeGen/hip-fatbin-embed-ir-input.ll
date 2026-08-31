; A HIP host module generated before its fatbin existed declares it. The
; backend defines it from -foffload-include-binary.

; RUN: printf 'FATBIN' > %t.hipfb
; RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -x ir -emit-llvm \
; RUN:   -foffload-include-binary %t.hipfb %s -o - | FileCheck %s

; CHECK: @[[FATBIN:[^ ]+]] = private constant [6 x i8] c"FATBIN", section ".hip_fatbin", align 4096
; CHECK: @__hip_fatbin_wrapper = internal constant { i32, i32, ptr, ptr } { i32 1212764230, i32 1, ptr @[[FATBIN]], ptr null }

target triple = "x86_64-unknown-linux-gnu"

@__hip_fatbin_0123 = external constant i8, section ".hip_fatbin"
@__hip_fatbin_wrapper = internal constant { i32, i32, ptr, ptr } { i32 1212764230, i32 1, ptr @__hip_fatbin_0123, ptr null }, section ".hipFatBinSegment", align 8
@llvm.compiler.used = appending global [1 x ptr] [ptr @__hip_fatbin_wrapper], section "llvm.metadata"
