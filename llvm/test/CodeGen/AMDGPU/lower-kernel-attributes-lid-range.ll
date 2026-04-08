; RUN: opt -mtriple=amdgcn-amd-amdhsa -mcpu=gfx90a \
; RUN:     -passes=amdgpu-lower-kernel-attributes -S %s | FileCheck %s
; RUN: opt -mtriple=amdgcn-amd-amdhsa -mcpu=gfx90a \
; RUN:     -passes=amdgpu-lower-kernel-attributes,instcombine -S %s \
; RUN:   | FileCheck %s --check-prefix=FOLD

; The workitem id range follows from amdgpu-flat-work-group-size, but its only
; other IR-level caller is AMDGPUCodeGenPrepare, which runs in the codegen
; pipeline.  Narrowing it here makes the bound visible to the middle end.

; CHECK-LABEL: @narrow_from_flat_work_group_size(
; CHECK: call range(i32 0, 64) i32 @llvm.amdgcn.workitem.id.x()

; A comparison the narrowed range decides.
; FOLD-LABEL: @narrow_from_flat_work_group_size(
; FOLD-NOT: icmp
; FOLD: store i32 1
define amdgpu_kernel void @narrow_from_flat_work_group_size(ptr addrspace(1) %out) #0 {
  %tid = call i32 @llvm.amdgcn.workitem.id.x()
  %cmp = icmp ult i32 %tid, 64
  %sel = select i1 %cmp, i32 1, i32 0
  store i32 %sel, ptr addrspace(1) %out
  ret void
}

; All three dimensions are narrowed, each from its own component.
; CHECK-LABEL: @narrow_all_dims(
; CHECK: call range(i32 0, 8) i32 @llvm.amdgcn.workitem.id.x()
; CHECK: call range(i32 0, 8) i32 @llvm.amdgcn.workitem.id.y()
; CHECK: call range(i32 0, 8) i32 @llvm.amdgcn.workitem.id.z()
define amdgpu_kernel void @narrow_all_dims(ptr addrspace(1) %out) #1 {
  %x = call i32 @llvm.amdgcn.workitem.id.x()
  %y = call i32 @llvm.amdgcn.workitem.id.y()
  %z = call i32 @llvm.amdgcn.workitem.id.z()
  %a = add i32 %x, %y
  %b = add i32 %a, %z
  store i32 %b, ptr addrspace(1) %out
  ret void
}

; Without the attribute there is nothing to narrow to, so the declaration's
; default range must be left alone rather than replaced with something wider.
; CHECK-LABEL: @no_attribute(
; CHECK-NOT: call range(i32 0, 64)
define amdgpu_kernel void @no_attribute(ptr addrspace(1) %out) {
  %tid = call i32 @llvm.amdgcn.workitem.id.x()
  store i32 %tid, ptr addrspace(1) %out
  ret void
}

declare i32 @llvm.amdgcn.workitem.id.x()
declare i32 @llvm.amdgcn.workitem.id.y()
declare i32 @llvm.amdgcn.workitem.id.z()

attributes #0 = { "amdgpu-flat-work-group-size"="1,64" }
attributes #1 = { "amdgpu-flat-work-group-size"="1,8" }
