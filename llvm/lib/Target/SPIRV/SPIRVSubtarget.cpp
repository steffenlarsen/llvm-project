//===-- SPIRVSubtarget.cpp - SPIR-V Subtarget Information ------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the SPIR-V specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "SPIRVSubtarget.h"

#include "MCTargetDesc/SPIRVBaseInfo.h"
#include "SPIRV.h"
#include "SPIRVCommandLine.h"
#include "SPIRVGlobalRegistry.h"
#include "SPIRVLegalizerInfo.h"
#include "SPIRVRegisterBankInfo.h"
#include "SPIRVTargetMachine.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Target/SPIRV/SPIRVOptionsOptInfos.h"

#include "llvm/TargetParser/Host.h"

using namespace llvm;

#define DEBUG_TYPE "spirv-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "SPIRVGenSubtargetInfo.inc"

static bool getTranslatorCompat(const Function &F) {
  return clv2::getOptValOrDefault<&clv2::SPIRV_TranslatorCompat>(
      F.getContext().getOptionsContext());
}

// Compare version numbers, but allow 0 to mean unspecified.
static bool isAtLeastVer(VersionTuple Target, VersionTuple VerToCompareTo) {
  return Target.empty() || Target >= VerToCompareTo;
}

namespace {
// Hand-written rather than declared in SPIRVOptions.td: the check needs
// SPIRVExtensionsParser, which is local to this backend.  Validating here
// rejects a bad list during the parse, with the option named and the parser's
// OnError policy respected.
static bool validateSPIRVExt(const std::string &Value, StringRef OptName,
                             clv2::detail::ParseDiag &Diag) {
  if (Value.empty())
    return true;
  ExtensionSet Tmp;
  std::string Error;
  if (!SPIRVExtensionsParser::parse(Value, Tmp, Error))
    return true;
  return clv2::detail::rejectOptionValue(OptName, Error, Diag);
}

static constexpr clv2::OptionInfo<std::string> OI_SPIRVExt{
    "spirv-ext", "Specify list of enabled SPIR-V extensions",
    clv2::Validate<std::string>{&validateSPIRVExt}};
static constexpr clv2::OptionsRegistry<&OI_SPIRVExt> SPIRVExtOptReg;
static const int RegisterSPIRVExtDynamic = [] {
  clv2::registerDynamicRegistry<&SPIRVExtOptReg>();
  return 0;
}();
} // namespace

SPIRVSubtarget::SPIRVSubtarget(const Triple &TT, const std::string &CPU,
                               const std::string &FS,
                               const SPIRVTargetMachine &TM)
    : SPIRVGenSubtargetInfo(TT, CPU, /*TuneCPU=*/CPU, FS,
                            TM.getOptionsContext()),
      PointerSize(TM.getPointerSizeInBits(/* AS= */ 0)),
      InstrInfo(initSubtargetDependencies(CPU, FS)), FrameLowering(*this),
      TLInfo(TM, *this), TargetTriple(TT) {
  setOptionsContext(TM.getOptionsContext());
  switch (TT.getSubArch()) {
  case Triple::SPIRVSubArch_v10:
    SPIRVVersion = VersionTuple(1, 0);
    break;
  case Triple::SPIRVSubArch_v11:
    SPIRVVersion = VersionTuple(1, 1);
    break;
  case Triple::SPIRVSubArch_v12:
    SPIRVVersion = VersionTuple(1, 2);
    break;
  case Triple::SPIRVSubArch_v13:
    SPIRVVersion = VersionTuple(1, 3);
    break;
  case Triple::SPIRVSubArch_v14:
    SPIRVVersion = VersionTuple(1, 4);
    break;
  case Triple::SPIRVSubArch_v15:
    SPIRVVersion = VersionTuple(1, 5);
    break;
  case Triple::SPIRVSubArch_v16:
    SPIRVVersion = VersionTuple(1, 6);
    break;
  default:
    if (TT.getVendor() == Triple::AMD)
      SPIRVVersion = VersionTuple(1, 6);
    else
      SPIRVVersion = VersionTuple(1, 4);
  }
  OpenCLVersion = VersionTuple(2, 2);

  // Set the environment based on the target triple.
  if (TargetTriple.getOS() == Triple::Vulkan)
    Env = Shader;
  else if (TargetTriple.getOS() == Triple::OpenCL ||
           TargetTriple.getVendor() == Triple::AMD ||
           TargetTriple.getOS() == Triple::ChipStar)
    Env = Kernel;
  else
    Env = Unknown;

  // Read CLI string from OptionsContext.
  const auto &Ctx = TM.getOptionsContext();
  std::string ExtStr =
      clv2::getOptValOr<&SPIRVExtOptReg, &OI_SPIRVExt>(Ctx, std::string{});

  // Parse into local set.  Already validated at parse time by
  // validateSPIRVExt, so a failure here is not reachable from the command
  // line; keep the parse for its side effect of filling LocalExts.
  ExtensionSet LocalExts;
  if (!ExtStr.empty()) {
    std::string Error;
    if (SPIRVExtensionsParser::parse(ExtStr, LocalExts, Error))
      report_fatal_error(Twine(Error), /*gen_crash_diag=*/false);
  }

  // Set the default extensions based on the target triple.
  if (TargetTriple.getVendor() == Triple::Intel) {
    LocalExts.insert(SPIRV::Extension::SPV_INTEL_function_pointers);
    LocalExts.insert(
        SPIRV::Extension::SPV_EXT_relaxed_printf_string_address_space);
  }
  if (TargetTriple.getVendor() == Triple::AMD)
    LocalExts = SPIRVExtensionsParser::getValidExtensions(TargetTriple);

  // The order of initialization is important.
  initAvailableExtensions(LocalExts);
  initAvailableExtInstSets();

  GR = std::make_unique<SPIRVGlobalRegistry>(TM.createDataLayout());
  CallLoweringInfo = std::make_unique<SPIRVCallLowering>(TLInfo, GR.get());
  InlineAsmInfo = std::make_unique<SPIRVInlineAsmLowering>(TLInfo);
  Legalizer = std::make_unique<SPIRVLegalizerInfo>(*this);
  RegBankInfo = std::make_unique<SPIRVRegisterBankInfo>();
  InstSelector.reset(createSPIRVInstructionSelector(TM, *this, *RegBankInfo));
}

SPIRVSubtarget &SPIRVSubtarget::initSubtargetDependencies(StringRef CPU,
                                                          StringRef FS) {
  ParseSubtargetFeatures(CPU, /*TuneCPU=*/CPU, FS);
  return *this;
}

bool SPIRVSubtarget::canUseExtension(SPIRV::Extension::Extension E) const {
  return AvailableExtensions.contains(E);
}

bool SPIRVSubtarget::canUseExtInstSet(
    SPIRV::InstructionSet::InstructionSet E) const {
  return AvailableExtInstSets.contains(E);
}

SPIRV::InstructionSet::InstructionSet
SPIRVSubtarget::getPreferredInstructionSet() const {
  if (isShader())
    return SPIRV::InstructionSet::GLSL_std_450;
  else
    return SPIRV::InstructionSet::OpenCL_std;
}

bool SPIRVSubtarget::isAtLeastSPIRVVer(VersionTuple VerToCompareTo) const {
  return isAtLeastVer(SPIRVVersion, VerToCompareTo);
}

bool SPIRVSubtarget::isAtLeastOpenCLVer(VersionTuple VerToCompareTo) const {
  if (isShader())
    return false;
  return isAtLeastVer(OpenCLVersion, VerToCompareTo);
}

// If the SPIR-V version is >= 1.4 we can call OpPtrEqual and OpPtrNotEqual.
// In SPIR-V Translator compatibility mode this feature is not available.
bool SPIRVSubtarget::canDirectlyComparePointers(const Function &F) const {
  return !getTranslatorCompat(F) &&
         isAtLeastVer(SPIRVVersion, VersionTuple(1, 4));
}

void SPIRVSubtarget::accountForAMDShaderTrinaryMinmax() {
  if (canUseExtension(
          SPIRV::Extension::SPV_AMD_shader_trinary_minmax_extension)) {
    AvailableExtInstSets.insert(
        SPIRV::InstructionSet::SPV_AMD_shader_trinary_minmax);
  }
}

// TODO: use command line args for this rather than just defaults.
// Must have called initAvailableExtensions first.
void SPIRVSubtarget::initAvailableExtInstSets() {
  AvailableExtInstSets.clear();
  if (isShader())
    AvailableExtInstSets.insert(SPIRV::InstructionSet::GLSL_std_450);
  else
    AvailableExtInstSets.insert(SPIRV::InstructionSet::OpenCL_std);

  // Handle extended instruction sets from extensions.
  accountForAMDShaderTrinaryMinmax();
}

void SPIRVSubtarget::setEnv(SPIRVEnvType E) {
  if (E == Unknown)
    report_fatal_error("Unknown environment is not allowed.");
  if (Env != Unknown && Env != E)
    report_fatal_error("Environment is already set to a different value.");
  if (Env == E)
    return;

  Env = E;

  // Reinitialize Env-dependent state aka ExtInstSet and legalizer info.
  initAvailableExtInstSets();
  Legalizer = std::make_unique<SPIRVLegalizerInfo>(*this);
}

void SPIRVSubtarget::resolveEnvFromModule(const Module &M) {
  *GR = SPIRVGlobalRegistry(M.getDataLayout());

  if (Env != Unknown) {
    assert(!(isKernel() && any_of(M,
                                  [](const Function &F) {
                                    return F.hasFnAttribute("hlsl.shader");
                                  })) &&
           "Module has hlsl.shader attributes but environment is Kernel");
    return;
  }

  bool HasShaderAttr = any_of(
      M, [](const Function &F) { return F.hasFnAttribute("hlsl.shader"); });

  if (!HasShaderAttr) {
    if (auto *MemModel = M.getNamedMetadata("spirv.MemoryModel")) {
      if (MemModel->getNumOperands() == 0)
        report_fatal_error("Invalid spirv.MemoryModel metadata");
      auto *MemMD = MemModel->getOperand(0);
      if (MemMD->getNumOperands() < 2)
        report_fatal_error("Invalid spirv.MemoryModel operand");
      unsigned MemModelVal =
          mdconst::extract<ConstantInt>(MemMD->getOperand(1))->getZExtValue();
      switch (MemModelVal) {
      case SPIRV::MemoryModel::Simple:
      case SPIRV::MemoryModel::GLSL450:
        HasShaderAttr = true;
        break;
      case SPIRV::MemoryModel::VulkanKHR:
        HasShaderAttr = true;
        AvailableExtensions.insert(
            SPIRV::Extension::SPV_KHR_vulkan_memory_model);
        break;
      case SPIRV::MemoryModel::OpenCL:
        break;
      default:
        report_fatal_error(
            "Unknown memory model in spirv.MemoryModel metadata");
      }
    }
  }

  setEnv(HasShaderAttr ? Shader : Kernel);
}

// Set available extensions after SPIRVSubtarget is created.
void SPIRVSubtarget::initAvailableExtensions(
    const ExtensionSet &AllowedExtIds) {
  AvailableExtensions.clear();
  const ExtensionSet &ValidExtensions =
      SPIRVExtensionsParser::getValidExtensions(TargetTriple);

  for (const auto &Ext : AllowedExtIds) {
    if (ValidExtensions.count(Ext))
      AvailableExtensions.insert(Ext);
  }

  accountForAMDShaderTrinaryMinmax();
}
