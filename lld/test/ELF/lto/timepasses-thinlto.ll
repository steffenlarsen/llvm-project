; REQUIRES: x86
;; The ThinLTO backends of a.o and b.o run concurrently, each timing its own
;; passes. Their timings are merged and reported once at the end of LTO.
; RUN: rm -rf %t && split-file %s %t && cd %t
; RUN: opt -module-summary a.ll -o a.o
; RUN: opt -module-summary b.ll -o b.o
; RUN: env LLD_IN_TEST=0 ld.lld a.o b.o -o out.so -shared --thinlto-jobs=2 \
; RUN:   -mllvm -time-passes 2>&1 | FileCheck %s

; CHECK:     Pass execution timing report
; CHECK-NOT: Pass execution timing report
; CHECK:     Instruction Selection and Scheduling
; CHECK-NOT: Instruction Selection and Scheduling

;--- a.ll
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define i32 @a(i32 %x, i32 %y) {
  %m = mul i32 %x, %y
  %r = add i32 %m, 3
  ret i32 %r
}

;--- b.ll
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define i32 @b(i32 %x, i32 %y) {
  %m = mul i32 %x, %y
  %r = sub i32 %m, 3
  ret i32 %r
}
