; RUN: opt < %s -disable-output "-passes=print<scalar-evolution>" -scalar-evolution-classify-expressions=0 2>&1 | FileCheck %s

; Test that llvm.assume constraining the loop bound to be at most the stride
; lets SCEV prove the backedge is taken at most 0 times.  Without the assume,
; SCEV conservatively returns CouldNotCompute because it cannot rule out IV
; overflow (the add has no nsw/nuw).

declare void @llvm.assume(i1 noundef)

; Signed less-than loop with assume(stride > 0) and assume(N <=u stride).
; CHECK-LABEL: 'slt_assume_n_le_stride'
; CHECK: Loop %loop: backedge-taken count is i32 0
; CHECK: Loop %loop: constant max backedge-taken count is i32 0
define void @slt_assume_n_le_stride(ptr %A, i32 %N, i32 %stride) {
entry:
  %pos = icmp sgt i32 %stride, 0
  call void @llvm.assume(i1 %pos)
  %bounded = icmp ule i32 %N, %stride
  call void @llvm.assume(i1 %bounded)
  %entry_cond = icmp slt i32 0, %N
  br i1 %entry_cond, label %loop, label %exit

loop:
  %iv = phi i32 [ 0, %entry ], [ %next, %loop ]
  %ptr = getelementptr i32, ptr %A, i32 %iv
  store i32 %iv, ptr %ptr
  %next = add i32 %iv, %stride
  %cmp = icmp slt i32 %next, %N
  br i1 %cmp, label %loop, label %exit

exit:
  ret void
}

; Unsigned less-than loop with assume(stride >u 0) and assume(N <=u stride).
; CHECK-LABEL: 'ult_assume_n_le_stride'
; CHECK: Loop %loop: backedge-taken count is i32 0
; CHECK: Loop %loop: constant max backedge-taken count is i32 0
define void @ult_assume_n_le_stride(ptr %A, i32 %N, i32 %stride) {
entry:
  %pos = icmp ugt i32 %stride, 0
  call void @llvm.assume(i1 %pos)
  %bounded = icmp ule i32 %N, %stride
  call void @llvm.assume(i1 %bounded)
  %entry_cond = icmp ult i32 0, %N
  br i1 %entry_cond, label %loop, label %exit

loop:
  %iv = phi i32 [ 0, %entry ], [ %next, %loop ]
  %ptr = getelementptr i32, ptr %A, i32 %iv
  store i32 %iv, ptr %ptr
  %next = add i32 %iv, %stride
  %cmp = icmp ult i32 %next, %N
  br i1 %cmp, label %loop, label %exit

exit:
  ret void
}

; Negative test: no assume bounding N, SCEV cannot compute trip count.
; CHECK-LABEL: 'slt_no_assume'
; CHECK: Loop %loop: Unpredictable backedge-taken count.
define void @slt_no_assume(ptr %A, i32 %N, i32 %stride) {
entry:
  %pos = icmp sgt i32 %stride, 0
  call void @llvm.assume(i1 %pos)
  %entry_cond = icmp slt i32 0, %N
  br i1 %entry_cond, label %loop, label %exit

loop:
  %iv = phi i32 [ 0, %entry ], [ %next, %loop ]
  %ptr = getelementptr i32, ptr %A, i32 %iv
  store i32 %iv, ptr %ptr
  %next = add i32 %iv, %stride
  %cmp = icmp slt i32 %next, %N
  br i1 %cmp, label %loop, label %exit

exit:
  ret void
}
