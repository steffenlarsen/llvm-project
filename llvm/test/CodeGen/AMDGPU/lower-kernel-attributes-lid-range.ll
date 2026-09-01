; RUN: opt -passes=amdgpu-lower-kernel-attributes -mtriple=amdgcn-amd-amdhsa -mcpu=gfx90a -S < %s | FileCheck %s

; llvm.amdgcn.workitem.id.x carries range(i32 0, 1024) from its declaration.
; makeLIDRangeMetadata narrows that from amdgpu-flat-work-group-size, but its
; only IR-level caller was AMDGPUCodeGenPrepare, in the codegen pipeline -- so
; the whole middle end reasoned with the full range. Narrowing it here makes it
; available to InstCombine and the unroller.

; A tightened flat-work-group-size narrows the range.
; CHECK-LABEL: @bounded(
; CHECK: call range(i32 0, 32) i32 @llvm.amdgcn.workitem.id.x()
define amdgpu_kernel void @bounded(ptr addrspace(1) %o) #0 {
  %id = call i32 @llvm.amdgcn.workitem.id.x()
  store i32 %id, ptr addrspace(1) %o
  ret void
}

; Without one there is nothing to say, and stamping the default bound on would
; be no information at all.
; CHECK-LABEL: @unbounded(
; CHECK: call i32 @llvm.amdgcn.workitem.id.x()
; CHECK-NOT: call range{{.*}}@llvm.amdgcn.workitem.id.x()
define amdgpu_kernel void @unbounded(ptr addrspace(1) %o) {
  %id = call i32 @llvm.amdgcn.workitem.id.x()
  store i32 %id, ptr addrspace(1) %o
  ret void
}

declare i32 @llvm.amdgcn.workitem.id.x()
attributes #0 = { "amdgpu-flat-work-group-size"="1,32" }
