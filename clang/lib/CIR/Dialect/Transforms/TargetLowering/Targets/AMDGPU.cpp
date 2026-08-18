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

  /// AMDGPU implements every scope CIR can name bar the NVIDIA-specific
  /// cluster scope, so they are kept rather than widened to system scope as
  /// the default does.
  ///
  /// Widening is sound -- system is the strongest scope -- but it is not free:
  /// a `seq_cst` global atomic at workgroup scope is a bare `global_atomic_*`,
  /// while the same atomic at system scope is bracketed by `buffer_wbl2`,
  /// `buffer_invl2` and `buffer_wbinvl1_vol` on gfx90a. Widening also loses
  /// the scope a user asked for through `__builtin_amdgcn_atomic_*` or
  /// `__builtin_amdgcn_fence`.
  cir::SyncScopeKind
  convertSyncScope(cir::SyncScopeKind syncScope) const override {
    // Cluster scope has no AMDGPU equivalent. System scope is the sound
    // over-approximation.
    if (syncScope == cir::SyncScopeKind::Cluster ||
        syncScope == cir::SyncScopeKind::HIPCluster)
      return cir::SyncScopeKind::System;
    return syncScope;
  }
};

} // namespace

std::unique_ptr<TargetLoweringInfo> createAMDGPUTargetLoweringInfo() {
  return std::make_unique<AMDGPUTargetLoweringInfo>();
}

} // namespace cir
