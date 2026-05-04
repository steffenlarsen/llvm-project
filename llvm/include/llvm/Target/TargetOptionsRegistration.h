//===- TargetOptionsRegistration.h - clv2 target option registration -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Registration entry points for the per-target clv2 option registries.
//
// A tool that wants a target's options no longer includes that target's
// generated <X>OptionsOptInfos.h just to name its registry in
// P.add<&XOptsReg>(). Those headers are large and nothing outside
// lib/Target/<X> reads their symbols -- they were public only so registration
// could name the registry.
//
// These are defined in the per-target CodeGen libraries, so only tools that
// already link every target (llc, opt) can use them.  RegisterAllLLVMOptions
// deliberately still includes the headers directly: LLVMAllOptions is linked by
// clang tools that do *not* link target CodeGen, and making them do so to reach
// a registration function would cost far more binary size than it saves.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_TARGETOPTIONSREGISTRATION_H
#define LLVM_TARGET_TARGETOPTIONSREGISTRATION_H

namespace llvm {
namespace clv2 {
class OptionParser;
} // namespace clv2

void registerAArch64Options(clv2::OptionParser &P);
void registerAMDGPUOptions(clv2::OptionParser &P);
void registerARMOptions(clv2::OptionParser &P);
void registerBPFOptions(clv2::OptionParser &P);
void registerHexagonOptions(clv2::OptionParser &P);
void registerLanaiOptions(clv2::OptionParser &P);
void registerLoongArchOptions(clv2::OptionParser &P);
void registerMipsOptions(clv2::OptionParser &P);
void registerMSP430Options(clv2::OptionParser &P);
void registerNVPTXOptions(clv2::OptionParser &P);
void registerPowerPCOptions(clv2::OptionParser &P);
void registerRISCVOptions(clv2::OptionParser &P);
void registerSparcOptions(clv2::OptionParser &P);
void registerSPIRVOptions(clv2::OptionParser &P);
void registerSystemZOptions(clv2::OptionParser &P);
void registerWebAssemblyOptions(clv2::OptionParser &P);
void registerX86Options(clv2::OptionParser &P);
void registerXCoreOptions(clv2::OptionParser &P);

/// As above, but also installs the bridge that copies parsed values into
/// AMDGPU's legacy globals.  llc and opt register AMDGPU this way while
/// RegisterAllLLVMOptions does not; that difference predates this header.
void registerAMDGPUOptionsWithBridge(clv2::OptionParser &P);

} // namespace llvm

#endif // LLVM_TARGET_TARGETOPTIONSREGISTRATION_H
