//===-- AMDGPURegBankLegalizeRules.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// Definitions of RegBankLegalize Rules for all opcodes.
/// Implementation of container for all the Rules and search.
/// Fast search for most common case when Rule.Predicate checks LLT and
/// uniformity of register in operand 0.
//
//===----------------------------------------------------------------------===//

#include "AMDGPURegBankLegalizeRules.h"
#include "AMDGPUInstrInfo.h"
#include "GCNSubtarget.h"
#include "llvm/CodeGen/GlobalISel/GenericMachineInstrs.h"
#include "llvm/CodeGen/MachineUniformityAnalysis.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/Support/AMDGPUAddrSpace.h"

#define DEBUG_TYPE "amdgpu-reg-bank-legalize"

using namespace llvm;
using namespace AMDGPU;

bool AMDGPU::isAnyPtr(LLT Ty, unsigned Width) {
  return Ty.isPointer() && Ty.getSizeInBits() == Width;
}

RegBankLLTMapping::RegBankLLTMapping(
    std::initializer_list<RegBankLLTMappingApplyID> DstOpMappingList,
    std::initializer_list<RegBankLLTMappingApplyID> SrcOpMappingList,
    LoweringMethodID LoweringMethod)
    : DstOpMapping(DstOpMappingList), SrcOpMapping(SrcOpMappingList),
      LoweringMethod(LoweringMethod) {}

PredicateMapping::PredicateMapping(
    std::initializer_list<UniformityLLTOpPredicateID> OpList,
    std::function<bool(const MachineInstr &)> TestFunc)
    : OpUniformityAndTypes(OpList), TestFunc(TestFunc) {}

bool matchUniformityAndLLT(Register Reg, UniformityLLTOpPredicateID UniID,
                           const MachineUniformityInfo &MUI,
                           const MachineRegisterInfo &MRI) {
  switch (UniID) {
  case S1:
    return MRI.getType(Reg) == LLT::scalar(1);
  case S16:
    return MRI.getType(Reg) == LLT::scalar(16);
  case S32:
    return MRI.getType(Reg) == LLT::scalar(32);
  case S64:
    return MRI.getType(Reg) == LLT::scalar(64);
  case S128:
    return MRI.getType(Reg) == LLT::scalar(128);
  case P0:
    return MRI.getType(Reg) == LLT::pointer(0, 64);
  case P1:
    return MRI.getType(Reg) == LLT::pointer(1, 64);
  case P2:
    return MRI.getType(Reg) == LLT::pointer(2, 32);
  case P3:
    return MRI.getType(Reg) == LLT::pointer(3, 32);
  case P4:
    return MRI.getType(Reg) == LLT::pointer(4, 64);
  case P5:
    return MRI.getType(Reg) == LLT::pointer(5, 32);
  case P8:
    return MRI.getType(Reg) == LLT::pointer(8, 128);
  case Ptr32:
    return isAnyPtr(MRI.getType(Reg), 32);
  case Ptr64:
    return isAnyPtr(MRI.getType(Reg), 64);
  case Ptr128:
    return isAnyPtr(MRI.getType(Reg), 128);
  case V2S16:
    return MRI.getType(Reg) == LLT::fixed_vector(2, 16);
  case V2S32:
    return MRI.getType(Reg) == LLT::fixed_vector(2, 32);
  case V3S32:
    return MRI.getType(Reg) == LLT::fixed_vector(3, 32);
  case V4S32:
    return MRI.getType(Reg) == LLT::fixed_vector(4, 32);
  case B32:
    return MRI.getType(Reg).getSizeInBits() == 32;
  case B64:
    return MRI.getType(Reg).getSizeInBits() == 64;
  case B96:
    return MRI.getType(Reg).getSizeInBits() == 96;
  case B128:
    return MRI.getType(Reg).getSizeInBits() == 128;
  case B160:
    return MRI.getType(Reg).getSizeInBits() == 160;
  case B256:
    return MRI.getType(Reg).getSizeInBits() == 256;
  case B512:
    return MRI.getType(Reg).getSizeInBits() == 512;
  case DivAnyTy:
    return MUI.isDivergentAtDef(Reg);
  case UniS1:
    return MRI.getType(Reg) == LLT::scalar(1) && MUI.isUniformAtDef(Reg);
  case UniS16:
    return MRI.getType(Reg) == LLT::scalar(16) && MUI.isUniformAtDef(Reg);
  case UniS32:
    return MRI.getType(Reg) == LLT::scalar(32) && MUI.isUniformAtDef(Reg);
  case UniS64:
    return MRI.getType(Reg) == LLT::scalar(64) && MUI.isUniformAtDef(Reg);
  case UniS128:
    return MRI.getType(Reg) == LLT::scalar(128) && MUI.isUniformAtDef(Reg);
  case UniBF16:
    return MRI.getType(Reg).isBFloat16() && MUI.isUniformAtDef(Reg);
  case UniP0:
    return MRI.getType(Reg) == LLT::pointer(0, 64) && MUI.isUniformAtDef(Reg);
  case UniP1:
    return MRI.getType(Reg) == LLT::pointer(1, 64) && MUI.isUniformAtDef(Reg);
  case UniP2:
    return MRI.getType(Reg) == LLT::pointer(2, 32) && MUI.isUniformAtDef(Reg);
  case UniP3:
    return MRI.getType(Reg) == LLT::pointer(3, 32) && MUI.isUniformAtDef(Reg);
  case UniP4:
    return MRI.getType(Reg) == LLT::pointer(4, 64) && MUI.isUniformAtDef(Reg);
  case UniP5:
    return MRI.getType(Reg) == LLT::pointer(5, 32) && MUI.isUniformAtDef(Reg);
  case UniP6:
    return MRI.getType(Reg) == LLT::pointer(6, 32) && MUI.isUniformAtDef(Reg);
  case UniP8:
    return MRI.getType(Reg) == LLT::pointer(8, 128) && MUI.isUniformAtDef(Reg);
  case UniPtr32:
    return isAnyPtr(MRI.getType(Reg), 32) && MUI.isUniformAtDef(Reg);
  case UniPtr64:
    return isAnyPtr(MRI.getType(Reg), 64) && MUI.isUniformAtDef(Reg);
  case UniPtr128:
    return isAnyPtr(MRI.getType(Reg), 128) && MUI.isUniformAtDef(Reg);
  case UniV2S16:
    return MRI.getType(Reg) == LLT::fixed_vector(2, 16) &&
           MUI.isUniformAtDef(Reg);
  case UniV2S32:
    return MRI.getType(Reg) == LLT::fixed_vector(2, 32) &&
           MUI.isUniformAtDef(Reg);
  case UniV3S32:
    return MRI.getType(Reg) == LLT::fixed_vector(3, 32) &&
           MUI.isUniformAtDef(Reg);
  case UniV4S32:
    return MRI.getType(Reg) == LLT::fixed_vector(4, 32) &&
           MUI.isUniformAtDef(Reg);
  case UniV6S32:
    return MRI.getType(Reg) == LLT::fixed_vector(6, 32) &&
           MUI.isUniformAtDef(Reg);
  case UniV8S16:
    return MRI.getType(Reg) == LLT::fixed_vector(8, 16) &&
           MUI.isUniformAtDef(Reg);
  case UniV8S32:
    return MRI.getType(Reg) == LLT::fixed_vector(8, 32) &&
           MUI.isUniformAtDef(Reg);
  case UniV16S16:
    return MRI.getType(Reg) == LLT::fixed_vector(16, 16) &&
           MUI.isUniformAtDef(Reg);
  case UniV16S32:
    return MRI.getType(Reg) == LLT::fixed_vector(16, 32) &&
           MUI.isUniformAtDef(Reg);
  case UniV32S16:
    return MRI.getType(Reg) == LLT::fixed_vector(32, 16) &&
           MUI.isUniformAtDef(Reg);
  case UniV32S32:
    return MRI.getType(Reg) == LLT::fixed_vector(32, 32) &&
           MUI.isUniformAtDef(Reg);
  case UniV2S64:
    return MRI.getType(Reg) == LLT::fixed_vector(2, 64) &&
           MUI.isUniformAtDef(Reg);
  case UniB32:
    return MRI.getType(Reg).getSizeInBits() == 32 && MUI.isUniformAtDef(Reg);
  case UniB64:
    return MRI.getType(Reg).getSizeInBits() == 64 && MUI.isUniformAtDef(Reg);
  case UniB96:
    return MRI.getType(Reg).getSizeInBits() == 96 && MUI.isUniformAtDef(Reg);
  case UniB128:
    return MRI.getType(Reg).getSizeInBits() == 128 && MUI.isUniformAtDef(Reg);
  case UniB160:
    return MRI.getType(Reg).getSizeInBits() == 160 && MUI.isUniformAtDef(Reg);
  case UniB256:
    return MRI.getType(Reg).getSizeInBits() == 256 && MUI.isUniformAtDef(Reg);
  case UniB512:
    return MRI.getType(Reg).getSizeInBits() == 512 && MUI.isUniformAtDef(Reg);
  case UniBRC: {
    if (MUI.isDivergentAtDef(Reg))
      return false;
    // Check if there is SGPR register class of same size as the LLT.
    const SIRegisterInfo *TRI =
        static_cast<const SIRegisterInfo *>(MRI.getTargetRegisterInfo());
    // There is no 16 bit SGPR register class. Extra size check is required
    // since getSGPRClassForBitWidth returns SReg_32RegClass for Size 16.
    unsigned LLTSize = MRI.getType(Reg).getSizeInBits();
    return LLTSize >= 32 && TRI->getSGPRClassForBitWidth(LLTSize);
  }
  case DivS1:
    return MRI.getType(Reg) == LLT::scalar(1) && MUI.isDivergentAtDef(Reg);
  case DivS16:
    return MRI.getType(Reg) == LLT::scalar(16) && MUI.isDivergentAtDef(Reg);
  case DivS32:
    return MRI.getType(Reg) == LLT::scalar(32) && MUI.isDivergentAtDef(Reg);
  case DivS64:
    return MRI.getType(Reg) == LLT::scalar(64) && MUI.isDivergentAtDef(Reg);
  case DivS128:
    return MRI.getType(Reg) == LLT::scalar(128) && MUI.isDivergentAtDef(Reg);
  case DivP0:
    return MRI.getType(Reg) == LLT::pointer(0, 64) && MUI.isDivergentAtDef(Reg);
  case DivP1:
    return MRI.getType(Reg) == LLT::pointer(1, 64) && MUI.isDivergentAtDef(Reg);
  case DivP2:
    return MRI.getType(Reg) == LLT::pointer(2, 32) && MUI.isDivergentAtDef(Reg);
  case DivP3:
    return MRI.getType(Reg) == LLT::pointer(3, 32) && MUI.isDivergentAtDef(Reg);
  case DivP4:
    return MRI.getType(Reg) == LLT::pointer(4, 64) && MUI.isDivergentAtDef(Reg);
  case DivP5:
    return MRI.getType(Reg) == LLT::pointer(5, 32) && MUI.isDivergentAtDef(Reg);
  case DivPtr32:
    return isAnyPtr(MRI.getType(Reg), 32) && MUI.isDivergentAtDef(Reg);
  case DivPtr64:
    return isAnyPtr(MRI.getType(Reg), 64) && MUI.isDivergentAtDef(Reg);
  case DivPtr128:
    return isAnyPtr(MRI.getType(Reg), 128) && MUI.isDivergentAtDef(Reg);
  case DivV2S16:
    return MRI.getType(Reg) == LLT::fixed_vector(2, 16) &&
           MUI.isDivergentAtDef(Reg);
  case DivV2S32:
    return MRI.getType(Reg) == LLT::fixed_vector(2, 32) &&
           MUI.isDivergentAtDef(Reg);
  case DivV4S32:
    return MRI.getType(Reg) == LLT::fixed_vector(4, 32) &&
           MUI.isDivergentAtDef(Reg);
  case DivV2S64:
    return MRI.getType(Reg) == LLT::fixed_vector(2, 64) &&
           MUI.isDivergentAtDef(Reg);
  case DivV3S32:
    return MRI.getType(Reg) == LLT::fixed_vector(3, 32) &&
           MUI.isDivergentAtDef(Reg);
  case DivV4S16:
    return MRI.getType(Reg) == LLT::fixed_vector(4, 16) &&
           MUI.isDivergentAtDef(Reg);
  case DivV8S16:
    return MRI.getType(Reg) == LLT::fixed_vector(8, 16) &&
           MUI.isDivergentAtDef(Reg);
  case DivV8S32:
    return MRI.getType(Reg) == LLT::fixed_vector(8, 32) &&
           MUI.isDivergentAtDef(Reg);
  case DivV16S16:
    return MRI.getType(Reg) == LLT::fixed_vector(16, 16) &&
           MUI.isDivergentAtDef(Reg);
  case DivV16S32:
    return MRI.getType(Reg) == LLT::fixed_vector(16, 32) &&
           MUI.isDivergentAtDef(Reg);
  case DivV6S32:
    return MRI.getType(Reg) == LLT::fixed_vector(6, 32) &&
           MUI.isDivergentAtDef(Reg);
  case DivV32S16:
    return MRI.getType(Reg) == LLT::fixed_vector(32, 16) &&
           MUI.isDivergentAtDef(Reg);
  case DivV32S32:
    return MRI.getType(Reg) == LLT::fixed_vector(32, 32) &&
           MUI.isDivergentAtDef(Reg);
  case DivB32:
    return MRI.getType(Reg).getSizeInBits() == 32 && MUI.isDivergentAtDef(Reg);
  case DivB64:
    return MRI.getType(Reg).getSizeInBits() == 64 && MUI.isDivergentAtDef(Reg);
  case DivB96:
    return MRI.getType(Reg).getSizeInBits() == 96 && MUI.isDivergentAtDef(Reg);
  case DivB128:
    return MRI.getType(Reg).getSizeInBits() == 128 && MUI.isDivergentAtDef(Reg);
  case DivB160:
    return MRI.getType(Reg).getSizeInBits() == 160 && MUI.isDivergentAtDef(Reg);
  case DivB256:
    return MRI.getType(Reg).getSizeInBits() == 256 && MUI.isDivergentAtDef(Reg);
  case DivB512:
    return MRI.getType(Reg).getSizeInBits() == 512 && MUI.isDivergentAtDef(Reg);
  case DivBRC: {
    if (MUI.isUniformAtDef(Reg))
      return false;
    // Check if there is VGPR register class of same size as the LLT.
    const SIRegisterInfo *TRI =
        static_cast<const SIRegisterInfo *>(MRI.getTargetRegisterInfo());
    return TRI->getSGPRClassForBitWidth(MRI.getType(Reg).getSizeInBits());
  }
  case BRC: {
    // Check if there is SGPR and VGPR register class of same size as the LLT.
    const SIRegisterInfo *TRI =
        static_cast<const SIRegisterInfo *>(MRI.getTargetRegisterInfo());
    unsigned LLTSize = MRI.getType(Reg).getSizeInBits();
    return LLTSize >= 32 && TRI->getSGPRClassForBitWidth(LLTSize) &&
           TRI->getVGPRClassForBitWidth(LLTSize);
  }
  case _:
    return true;
  default:
    llvm_unreachable("missing matchUniformityAndLLT");
  }
}

bool PredicateMapping::match(const MachineInstr &MI,
                             const MachineUniformityInfo &MUI,
                             const MachineRegisterInfo &MRI) const {
  // Check LLT signature.
  for (unsigned i = 0; i < OpUniformityAndTypes.size(); ++i) {
    const MachineOperand &MO = MI.getOperand(i);
    if (OpUniformityAndTypes[i] == _) {
      assert((!MI.getOperand(i).isReg() ||
              !MI.getOperand(i).getReg().isVirtual()) &&
             "_ is for non-register and physical register operands only");
      continue;
    }

    // Remaining IDs check registers.
    if (!MO.isReg())
      return false;

    if (!matchUniformityAndLLT(MO.getReg(), OpUniformityAndTypes[i], MUI, MRI))
      return false;
  }

  // More complex check.
  if (TestFunc)
    return TestFunc(MI);

  return true;
}

SetOfRulesForOpcode::SetOfRulesForOpcode() = default;

SetOfRulesForOpcode::SetOfRulesForOpcode(FastRulesTypes FastTypes)
    : FastTypes(FastTypes) {}

UniformityLLTOpPredicateID LLTToId(LLT Ty) {
  if (Ty == LLT::scalar(16))
    return S16;
  if (Ty == LLT::scalar(32))
    return S32;
  if (Ty == LLT::scalar(64))
    return S64;
  if (Ty == LLT::fixed_vector(2, 16))
    return V2S16;
  if (Ty == LLT::fixed_vector(2, 32))
    return V2S32;
  if (Ty == LLT::fixed_vector(3, 32))
    return V3S32;
  if (Ty == LLT::fixed_vector(4, 32))
    return V4S32;
  return _;
}

UniformityLLTOpPredicateID LLTToBId(LLT Ty) {
  if (Ty == LLT::scalar(32) || Ty == LLT::fixed_vector(2, 16) ||
      isAnyPtr(Ty, 32))
    return B32;
  if (Ty == LLT::scalar(64) || Ty == LLT::fixed_vector(2, 32) ||
      Ty == LLT::fixed_vector(4, 16) || isAnyPtr(Ty, 64))
    return B64;
  if (Ty == LLT::fixed_vector(3, 32))
    return B96;
  if (Ty == LLT::fixed_vector(4, 32) || Ty == LLT::fixed_vector(2, 64) ||
      Ty == LLT::fixed_vector(8, 16) || isAnyPtr(Ty, 128))
    return B128;
  return _;
}

const RegBankLLTMapping *
SetOfRulesForOpcode::findMappingForMI(const MachineInstr &MI,
                                      const MachineRegisterInfo &MRI,
                                      const MachineUniformityInfo &MUI) const {
  // Search in "Fast Rules".
  // Note: if fast rules are enabled, RegBankLLTMapping must be added in each
  // slot that could "match fast Predicate". If not, InvalidMapping is
  // returned which results in failure, does not search "Slow Rules".
  if (FastTypes != NoFastRules) {
    Register Reg = MI.getOperand(0).getReg();
    int Slot;
    if (FastTypes == StandardB)
      Slot = getFastPredicateSlot(LLTToBId(MRI.getType(Reg)));
    else
      Slot = getFastPredicateSlot(LLTToId(MRI.getType(Reg)));

    if (Slot != -1)
      return MUI.isUniformAtDef(Reg) ? &Uni[Slot] : &Div[Slot];
  }

  // Slow search for more complex rules.
  for (const RegBankLegalizeRule &Rule : Rules) {
    if (Rule.Predicate.match(MI, MUI, MRI))
      return &Rule.OperandMapping;
  }

  return nullptr;
}

void SetOfRulesForOpcode::addRule(RegBankLegalizeRule Rule) {
  Rules.push_back(Rule);
}

void SetOfRulesForOpcode::addFastRuleDivergent(UniformityLLTOpPredicateID Ty,
                                               RegBankLLTMapping RuleApplyIDs) {
  int Slot = getFastPredicateSlot(Ty);
  assert(Slot != -1 && "Ty unsupported in this FastRulesTypes");
  Div[Slot] = std::move(RuleApplyIDs);
}

void SetOfRulesForOpcode::addFastRuleUniform(UniformityLLTOpPredicateID Ty,
                                             RegBankLLTMapping RuleApplyIDs) {
  int Slot = getFastPredicateSlot(Ty);
  assert(Slot != -1 && "Ty unsupported in this FastRulesTypes");
  Uni[Slot] = std::move(RuleApplyIDs);
}

int SetOfRulesForOpcode::getFastPredicateSlot(
    UniformityLLTOpPredicateID Ty) const {
  switch (FastTypes) {
  case Standard: {
    switch (Ty) {
    case S32:
      return 0;
    case S16:
      return 1;
    case S64:
      return 2;
    case V2S16:
      return 3;
    default:
      return -1;
    }
  }
  case StandardB: {
    switch (Ty) {
    case B32:
      return 0;
    case B64:
      return 1;
    case B96:
      return 2;
    case B128:
      return 3;
    default:
      return -1;
    }
  }
  case Vector: {
    switch (Ty) {
    case S32:
      return 0;
    case V2S32:
      return 1;
    case V3S32:
      return 2;
    case V4S32:
      return 3;
    default:
      return -1;
    }
  }
  default:
    return -1;
  }
}

RegBankLegalizeRules::RuleSetInitializer
RegBankLegalizeRules::addRulesForGOpcs(std::initializer_list<unsigned> OpcList,
                                       FastRulesTypes FastTypes) {
  return RuleSetInitializer(OpcList, GRulesAlias, GRules, Features, FastTypes);
}

RegBankLegalizeRules::RuleSetInitializer
RegBankLegalizeRules::addRulesForIOpcs(std::initializer_list<unsigned> OpcList,
                                       FastRulesTypes FastTypes) {
  return RuleSetInitializer(OpcList, IRulesAlias, IRules, Features, FastTypes);
}

const SetOfRulesForOpcode *
RegBankLegalizeRules::getRulesForOpc(MachineInstr &MI) const {
  unsigned Opc = MI.getOpcode();
  if (Opc == AMDGPU::G_INTRINSIC || Opc == AMDGPU::G_INTRINSIC_CONVERGENT ||
      Opc == AMDGPU::G_INTRINSIC_W_SIDE_EFFECTS ||
      Opc == AMDGPU::G_INTRINSIC_CONVERGENT_W_SIDE_EFFECTS) {
    unsigned IntrID = cast<GIntrinsic>(MI).getIntrinsicID();
    auto IRAIt = IRulesAlias.find(IntrID);
    if (IRAIt == IRulesAlias.end())
      return nullptr;
    return &IRules.at(IRAIt->second);
  }

  auto GRAIt = GRulesAlias.find(Opc);
  if (GRAIt == GRulesAlias.end())
    return nullptr;
  return &GRules.at(GRAIt->second);
}

// Predicates for rules that check more than operand types and uniformity.

static bool isSignedICmp(const MachineInstr &MI) {
  auto Pred = static_cast<CmpInst::Predicate>(MI.getOperand(1).getPredicate());
  return CmpInst::isSigned(Pred);
}

static bool isEqualityICmp(const MachineInstr &MI) {
  auto Pred = static_cast<CmpInst::Predicate>(MI.getOperand(1).getPredicate());
  return ICmpInst::isEquality(Pred);
}

static bool isAlign16(const MachineInstr &MI) {
  return (*MI.memoperands_begin())->getAlign() >= Align(16);
}

static bool isAlign4(const MachineInstr &MI) {
  return (*MI.memoperands_begin())->getAlign() >= Align(4);
}

static bool isAtomicMMO(const MachineInstr &MI) {
  return (*MI.memoperands_begin())->isAtomic();
}

static bool isUniMMO(const MachineInstr &MI) {
  return AMDGPU::isUniformMMO(*MI.memoperands_begin());
}

static bool isConst(const MachineInstr &MI) {
  // Address space in MMO be different then address space on pointer.
  const MachineMemOperand *MMO = *MI.memoperands_begin();
  const unsigned AS = MMO->getAddrSpace();
  return AS == AMDGPUAS::CONSTANT_ADDRESS ||
         AS == AMDGPUAS::CONSTANT_ADDRESS_32BIT;
}

static bool isVolatileMMO(const MachineInstr &MI) {
  return (*MI.memoperands_begin())->isVolatile();
}

static bool isInvMMO(const MachineInstr &MI) {
  return (*MI.memoperands_begin())->isInvariant();
}

static bool isNoClobberMMO(const MachineInstr &MI) {
  return (*MI.memoperands_begin())->getFlags() & MONoClobber;
}

static bool isNaturalAligned(const MachineInstr &MI) {
  const MachineMemOperand *MMO = *MI.memoperands_begin();
  return MMO->getAlign() >= Align(MMO->getSize().getValue());
}

static bool is8Or16BitMMO(const MachineInstr &MI) {
  const MachineMemOperand *MMO = *MI.memoperands_begin();
  const unsigned MemSize = 8 * MMO->getSize().getValue();
  return MemSize == 16 || MemSize == 8;
}

static bool is32BitMMO(const MachineInstr &MI) {
  const MachineMemOperand *MMO = *MI.memoperands_begin();
  return 8 * MMO->getSize().getValue() == 32;
}

static bool isUL(const MachineInstr &MI) {
  return !isAtomicMMO(MI) && isUniMMO(MI) &&
         (isConst(MI) || !isVolatileMMO(MI)) &&
         (isConst(MI) || isInvMMO(MI) || isNoClobberMMO(MI));
}

static bool IsDataPF(const MachineInstr &MI) {
  // prefetch cache type: 0 == instruction (I$) prefetch, 1 == data prefetch.
  return MI.getOperand(3).getImm() != 0;
}

// Initialize rules
RegBankLegalizeRules::RegBankLegalizeRules(const GCNSubtarget &_ST,
                                           MachineRegisterInfo &_MRI)
    : ST(&_ST), MRI(&_MRI) {
  FeatureMask F = 0;
  if (ST->useVMulU64Inst())
    F |= featureBit(UseVMulU64Inst);
  if (ST->hasScalarMulHiInsts())
    F |= featureBit(HasScalarMulHiInsts);
  if (ST->hasScalarSMulU64())
    F |= featureBit(HasScalarSMulU64);
  if (ST->hasScalarCompareEq64())
    F |= featureBit(HasScalarCompareEq64);
  if (ST->has16BitInsts())
    F |= featureBit(Has16BitInsts);
  if (ST->hasAtomicFlatPkAdd16Insts())
    F |= featureBit(HasAtomicFlatPkAdd16Insts);
  if (ST->hasAtomicBufferGlobalPkAddF16NoRtnInsts() ||
      ST->hasAtomicBufferGlobalPkAddF16Insts())
    F |= featureBit(HasAtomicBufferGlobalPkAddF16Insts);
  if (ST->hasAtomicDsPkAdd16Insts())
    F |= featureBit(HasAtomicDsPkAdd16Insts);
  if (ST->hasScalarDwordx3Loads())
    F |= featureBit(HasScalarDwordx3Loads);
  if (ST->hasScalarSubwordLoads())
    F |= featureBit(HasScalarSubwordLoads);
  if (ST->useRealTrue16Insts())
    F |= featureBit(UseRealTrue16Insts);
  if (ST->hasSALUFloatInsts())
    F |= featureBit(HasSALUFloatInsts);
  if (ST->hasPseudoScalarTrans())
    F |= featureBit(HasPseudoScalarTrans);
  if (ST->hasSafeSmemPrefetch())
    F |= featureBit(HasSafeSmemPrefetch);
  if (ST->hasVmemPrefInsts())
    F |= featureBit(HasVmemPrefInsts);
  if (ST->hasSALUMinimumMaximumInsts())
    F |= featureBit(HasSALUMinimumMaximumInsts);
  if (ST->hasGFX90AInsts())
    F |= featureBit(HasGFX90AInsts);
  Features = F;

  using namespace Intrinsic;

  // AMDGPURegBankLegalizeRules.def lists each set of rules as
  //   RULES_G(FastTypes, (Opcode, ...), Rule, ...) for G_ opcodes, or
  //   RULES_I(FastTypes, (IntrinsicID, ...), Rule, ...) for intrinsics,
  // where a Rule is Uni(...), Div(...) or Any(...).
#define RBL_UNPAREN(...) __VA_ARGS__
#define Uni(...) (void)Rules.Uni(__VA_ARGS__)
#define Div(...) (void)Rules.Div(__VA_ARGS__)
#define Any(...) (void)Rules.Any(__VA_ARGS__)
#define PRED(...) (+[](const MachineInstr &MI) -> bool { return __VA_ARGS__; })
#define RULES_G(FastTypes, Opcodes, ...)                                       \
  {                                                                            \
    auto &&Rules = addRulesForGOpcs({RBL_UNPAREN Opcodes}, FastTypes);         \
    __VA_ARGS__;                                                               \
  }
#define RULES_I(FastTypes, Opcodes, ...)                                       \
  {                                                                            \
    auto &&Rules = addRulesForIOpcs({RBL_UNPAREN Opcodes}, FastTypes);         \
    __VA_ARGS__;                                                               \
  }
#include "AMDGPURegBankLegalizeRules.def"
#undef RULES_I
#undef RULES_G
#undef PRED
#undef Any
#undef Div
#undef Uni
#undef RBL_UNPAREN
}
