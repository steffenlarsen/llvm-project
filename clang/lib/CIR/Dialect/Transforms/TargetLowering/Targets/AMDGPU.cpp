//===- AMDGPU.cpp - Emit CIR for AMDGPU -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../TargetLoweringInfo.h"
#include "clang/CIR/Dialect/IR/CIROpsEnums.h"
#include "llvm/Support/AMDGPUAddrSpace.h"

namespace cir {

namespace {

// Address space mapping from:
// https://llvm.org/docs/AMDGPUUsage.html#address-spaces
//
// Indexed by cir::LangAddressSpace enum values.
constexpr unsigned AMDGPUAddrSpaceMap[] = {
    llvm::AMDGPUAS::FLAT_ADDRESS,     // Default
    llvm::AMDGPUAS::PRIVATE_ADDRESS,  // OffloadPrivate
    llvm::AMDGPUAS::LOCAL_ADDRESS,    // OffloadLocal
    llvm::AMDGPUAS::GLOBAL_ADDRESS,   // OffloadGlobal
    llvm::AMDGPUAS::CONSTANT_ADDRESS, // OffloadConstant
    llvm::AMDGPUAS::FLAT_ADDRESS,     // OffloadGeneric
    llvm::AMDGPUAS::GLOBAL_ADDRESS,   // OffloadGlobalDevice
    llvm::AMDGPUAS::GLOBAL_ADDRESS,   // OffloadGlobalHost
};

class AMDGPUTargetLoweringInfo : public TargetLoweringInfo {
public:
  unsigned getTargetAddrSpaceFromCIRAddrSpace(
      cir::LangAddressSpace addrSpace) const override {
    auto idx = static_cast<unsigned>(addrSpace);
    assert(idx < std::size(AMDGPUAddrSpaceMap) &&
           "Unknown CIR address space for AMDGPU target");
    return AMDGPUAddrSpaceMap[idx];
  }

  // Collapses HIP- and OpenCL-specific sync scopes onto the target-neutral
  // scopes AMDGPU actually distinguishes, mirroring OGCG's
  // clang::CodeGen::getAtomicScope (clang/lib/CodeGen/TargetInfo.h).
  cir::SyncScopeKind
  convertSyncScope(cir::SyncScopeKind syncScope) const override {
    switch (syncScope) {
    case cir::SyncScopeKind::SingleThread:
    case cir::SyncScopeKind::HIPSingleThread:
      return cir::SyncScopeKind::SingleThread;
    case cir::SyncScopeKind::Wavefront:
    case cir::SyncScopeKind::HIPWavefront:
    case cir::SyncScopeKind::OpenCLSubGroup:
      return cir::SyncScopeKind::Wavefront;
    case cir::SyncScopeKind::Workgroup:
    case cir::SyncScopeKind::HIPWorkgroup:
    case cir::SyncScopeKind::OpenCLWorkGroup:
      return cir::SyncScopeKind::Workgroup;
    case cir::SyncScopeKind::Cluster:
    case cir::SyncScopeKind::HIPCluster:
      return cir::SyncScopeKind::Cluster;
    case cir::SyncScopeKind::Device:
    case cir::SyncScopeKind::HIPAgent:
    case cir::SyncScopeKind::OpenCLDevice:
      return cir::SyncScopeKind::Device;
    case cir::SyncScopeKind::System:
    case cir::SyncScopeKind::HIPSystem:
    case cir::SyncScopeKind::OpenCLAllSVMDevices:
      return cir::SyncScopeKind::System;
    }
    llvm_unreachable("unhandled sync scope");
  }
};

} // namespace

std::unique_ptr<TargetLoweringInfo> createAMDGPUTargetLoweringInfo() {
  return std::make_unique<AMDGPUTargetLoweringInfo>();
}

} // namespace cir
