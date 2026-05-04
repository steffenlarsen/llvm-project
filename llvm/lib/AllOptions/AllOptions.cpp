//===- AllOptions.cpp - Register all LLVM clv2 option registries ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/AnalysisOptionsRegistration.h"
#include "llvm/AsmParser/AsmParserOptionsRegistration.h"
#include "llvm/Bitcode/BitcodeOptionsRegistration.h"
#include "llvm/CGData/CGDataOptionsRegistration.h"
#include "llvm/CodeGen/CodeGenOptionsRegistration.h"
#include "llvm/Config/Targets.h"
#include "llvm/Frontend/OpenMP/OpenMPOptionsRegistration.h"
#include "llvm/IR/IROptionsRegistration.h"
#include "llvm/LTO/LTOOptionsRegistration.h"
#include "llvm/MC/MCOptionsRegistration.h"
#include "llvm/Object/ObjectOptionsRegistration.h"
#include "llvm/Passes/PassesOptionsRegistration.h"
#include "llvm/ProfileData/ProfileDataOptionsRegistration.h"
#include "llvm/Remarks/RemarksOptionsRegistration.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/PluginLoader.h"
#include "llvm/Support/SupportOptionsOptInfos.h"
#include "llvm/Transforms/AggressiveInstCombine/AggressiveInstCombineOptionsRegistration.h"
#include "llvm/Transforms/Coroutines/CoroutinesOptionsRegistration.h"
#include "llvm/Transforms/IPO/IPOOptionsRegistration.h"
#include "llvm/Transforms/InstCombine/InstCombineOptionsRegistration.h"
#include "llvm/Transforms/Instrumentation/InstrumentationOptionsRegistration.h"
#include "llvm/Transforms/ObjCARC/ObjCARCOptionsRegistration.h"
#include "llvm/Transforms/Scalar/ScalarOptionsRegistration.h"
#include "llvm/Transforms/Utils/UtilsOptionsRegistration.h"
#include "llvm/Transforms/Vectorize/VectorizeOptionsRegistration.h"
#ifdef LINK_POLLY_INTO_TOOLS
#include "polly/PollyOptionsOptInfos.h"
#endif
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/MC/MCTargetOptionsCommandFlags.h"
#include "llvm/Support/SupportOptions.h"

namespace llvm {

// Defined in Targets/<Target>.cpp -- one TU per target to bound peak compile
// memory; see the comment there.
void registerAArch64OptionsInAll(clv2::OptionParser &P);
void registerAMDGPUOptionsInAll(clv2::OptionParser &P);
void registerARMOptionsInAll(clv2::OptionParser &P);
void registerBPFOptionsInAll(clv2::OptionParser &P);
void registerHexagonOptionsInAll(clv2::OptionParser &P);
void registerLanaiOptionsInAll(clv2::OptionParser &P);
void registerLoongArchOptionsInAll(clv2::OptionParser &P);
void registerMipsOptionsInAll(clv2::OptionParser &P);
void registerMSP430OptionsInAll(clv2::OptionParser &P);
void registerNVPTXOptionsInAll(clv2::OptionParser &P);
void registerPowerPCOptionsInAll(clv2::OptionParser &P);
void registerRISCVOptionsInAll(clv2::OptionParser &P);
void registerSparcOptionsInAll(clv2::OptionParser &P);
void registerSPIRVOptionsInAll(clv2::OptionParser &P);
void registerSystemZOptionsInAll(clv2::OptionParser &P);
void registerWebAssemblyOptionsInAll(clv2::OptionParser &P);
void registerX86OptionsInAll(clv2::OptionParser &P);
void registerXCoreOptionsInAll(clv2::OptionParser &P);
#if LLVM_HAS_ARC_TARGET
void registerARCOptionsInAll(clv2::OptionParser &P);
#endif
#if LLVM_HAS_CSKY_TARGET
void registerCSKYOptionsInAll(clv2::OptionParser &P);
#endif
#if LLVM_HAS_M68K_TARGET
void registerM68kOptionsInAll(clv2::OptionParser &P);
#endif

void RegisterCoreLLVMOptions(clv2::OptionParser &P) {
  using namespace clv2;
  P.add<&SupportOptsReg, support::applySupportOptions>();
  registerIROptsOptions(P);
  registerRemarksOptsOptions(P);
  registerPassesOptsOptions(P);
  registerBitcodeOptsOptions(P);
}

void RegisterCommonLLVMOptions(clv2::OptionParser &P) {
  using namespace clv2;
  // Core
  P.add<&SupportOptsReg, support::applySupportOptions>();
  registerIROptsOptions(P);
  registerRemarksOptsOptions(P);
  registerPassesOptsOptions(P);
  registerBitcodeOptsOptions(P);
  // Analysis + Transforms
  registerAnalysisOptsOptions(P);
  registerScalarOptsOptions(P);
  registerVectorizeOptsOptions(P);
  registerTransformUtilsOptsOptions(P);
  registerIPOOptsOptions(P);
  registerInstCombineOptsOptions(P);
  registerAggressiveInstCombineOptsOptions(P);
  registerInstrumentationOptsOptions(P);
  registerCoroutinesOptsOptions(P);
  registerObjCARCOptsOptions(P);
  // Other common libraries
  registerMCOptsOptions(P);
  registerObjectOptsOptions(P);
  registerProfileDataOptsOptions(P);
  registerLTOOptsOptions(P);
  registerCGDataOptsOptions(P);
  registerOMPOptsOptions(P);
  P.enableGlobalDynamicEntries();
}

void RegisterAllLLVMOptions(clv2::OptionParser &P) {
  using namespace clv2;
  // -load was reachable from any cl:: parse because the registry was global.
  // Registration is explicit now, so tools that take LLVM options through
  // -mllvm (lld, clang) have to be given it here or plugin loading silently
  // stops working for them.
  registerPluginLoaderOption();
  P.add<&SupportOptsReg, support::applySupportOptions>();
  registerIROptsOptions(P);
  registerMCOptsOptions(P);
  registerRemarksOptsOptions(P);
  registerPassesOptsOptions(P);
  registerAnalysisOptsOptions(P);
  registerObjectOptsOptions(P);
  registerBitcodeOptsOptions(P);
  registerProfileDataOptsOptions(P);
  registerScalarOptsOptions(P);
  registerVectorizeOptsOptions(P);
  registerTransformUtilsOptsOptions(P);
  registerIPOOptsOptions(P);
  registerInstCombineOptsOptions(P);
  registerAggressiveInstCombineOptsOptions(P);
  registerInstrumentationOptsOptions(P);
  registerCoroutinesOptsOptions(P);
  registerObjCARCOptsOptions(P);
  registerLTOOptsOptions(P);
  registerCGDataOptsOptions(P);
  registerOMPOptsOptions(P);
  registerCGPassAsmPrintOptions(P);
  registerCGPassCore1Options(P);
  registerCGPassCore2Options(P);
  registerCGPassGISelOptions(P);
  registerCGPassMachine1Options(P);
  registerCGPassMachine2Options(P);
  registerCGPassAllocOptions(P);
  registerCGPassSched1Options(P);
  registerCGPassSched2Options(P);
  registerCGPassSelDAGOptions(P);
  registerCGOptsOptions(P);
  registerAsmParserOptsOptions(P);
#if LLVM_HAS_ARC_TARGET
  registerARCOptionsInAll(P);
#endif
#if LLVM_HAS_CSKY_TARGET
  registerCSKYOptionsInAll(P);
#endif
#if LLVM_HAS_M68K_TARGET
  registerM68kOptionsInAll(P);
#endif
  registerAArch64OptionsInAll(P);
  registerAMDGPUOptionsInAll(P);
  registerARMOptionsInAll(P);
  registerBPFOptionsInAll(P);
  registerHexagonOptionsInAll(P);
  registerLanaiOptionsInAll(P);
  registerLoongArchOptionsInAll(P);
  registerMipsOptionsInAll(P);
  registerMSP430OptionsInAll(P);
  registerNVPTXOptionsInAll(P);
  registerPowerPCOptionsInAll(P);
  registerRISCVOptionsInAll(P);
  registerSparcOptionsInAll(P);
  registerSPIRVOptionsInAll(P);
  registerSystemZOptionsInAll(P);
  registerWebAssemblyOptionsInAll(P);
  registerX86OptionsInAll(P);
  registerXCoreOptionsInAll(P);
#ifdef LINK_POLLY_INTO_TOOLS
  P.add<&PollyOptsReg, polly_opts::applyPollyOptions>();
#endif
  P.enableGlobalDynamicEntries();
}

void RegisterCommonLLVMOptionsHidden(clv2::OptionParser &P) {
  RegisterAllLLVMOptions(P);
  P.hideAllDynamicEntries();
}

} // namespace llvm
