; REQUIRES: asserts
; RUN: llc -mtriple=amdgcn-amd-amdhsa -mcpu=gfx1030 -debug-only=machine-scheduler -filetype=null < %s 2>&1 | FileCheck %s

; The VGPR pressure limit follows each function's occupancy, which LDS use
; lowers here. Functions that reserve the same registers must still not share
; the limit computed for the previous one.

; CHECK-LABEL: no_lds_first:%bb.0
; CHECK: VGPR_32 Limit 64 Actual
; CHECK-LABEL: with_lds:%bb.0
; CHECK: VGPR_32 Limit 128 Actual
; CHECK-LABEL: no_lds_last:%bb.0
; CHECK: VGPR_32 Limit 64 Actual

define amdgpu_kernel void @no_lds_first(ptr addrspace(1) %p) {
  %v = load <128 x float>, ptr addrspace(1) %p
  %m = fmul <128 x float> %v, %v
  store <128 x float> %m, ptr addrspace(1) %p
  ret void
}

define amdgpu_kernel void @with_lds(ptr addrspace(1) %p) #0 {
  %v = load <128 x float>, ptr addrspace(1) %p
  %m = fmul <128 x float> %v, %v
  store <128 x float> %m, ptr addrspace(1) %p
  ret void
}

define amdgpu_kernel void @no_lds_last(ptr addrspace(1) %p) {
  %v = load <128 x float>, ptr addrspace(1) %p
  %m = fmul <128 x float> %v, %v
  store <128 x float> %m, ptr addrspace(1) %p
  ret void
}

attributes #0 = { "amdgpu-lds-size"="33792" }
