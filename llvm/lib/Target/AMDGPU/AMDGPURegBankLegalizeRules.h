//===- AMDGPURegBankLegalizeRules --------------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_AMDGPUREGBANKLEGALIZERULES_H
#define LLVM_LIB_TARGET_AMDGPU_AMDGPUREGBANKLEGALIZERULES_H

#include "llvm/ADT/ArrayRef.h"
#include <cstdint>
#include <initializer_list>

namespace llvm {

class LLT;
class MachineRegisterInfo;
class MachineInstr;
class GCNSubtarget;
class MachineFunction;
template <typename T> class GenericUniformityInfo;
template <typename T> class GenericSSAContext;
using MachineSSAContext = GenericSSAContext<MachineFunction>;
using MachineUniformityInfo = GenericUniformityInfo<MachineSSAContext>;

namespace AMDGPU {

/// \returns true if \p Ty is a pointer type with size \p Width.
bool isAnyPtr(LLT Ty, unsigned Width);

// IDs used to build predicate for RegBankLegalizeRule. Predicate can have one
// or more IDs and each represents a check for 'uniform or divergent' + LLT or
// just LLT on register operand.
// Most often checking one operand is enough to decide which RegBankLLTMapping
// to apply (see Fast Rules), IDs are useful when two or more operands need to
// be checked.
enum UniformityLLTOpPredicateID : uint8_t {
  // Represents non-register and physical register operands.
  _,
  // scalars
  S1,
  S16,
  S32,
  S64,
  S128,

  UniS1,
  UniS16,
  UniS32,
  UniS64,
  UniS128,

  UniBF16,

  DivS1,
  DivS16,
  DivS32,
  DivS64,
  DivS128,

  // any LLT, divergent-check only predicate
  DivAnyTy,

  // pointers
  P0,
  P1,
  P2,
  P3,
  P4,
  P5,
  P8,
  Ptr32,
  Ptr64,
  Ptr128,

  UniP0,
  UniP1,
  UniP2,
  UniP3,
  UniP4,
  UniP5,
  UniP6,
  UniP8,
  UniPtr32,
  UniPtr64,
  UniPtr128,

  DivP0,
  DivP1,
  DivP2,
  DivP3,
  DivP4,
  DivP5,
  DivPtr32,
  DivPtr64,
  DivPtr128,

  // vectors
  V2S16,
  V2S32,
  V2S64,
  V3S32,
  V4S32,
  V32S32,

  UniV2S16,
  UniV2S32,
  UniV4S32,
  UniV2S64,
  UniV3S32,
  UniV6S32,
  UniV8S16,
  UniV8S32,
  UniV16S16,
  UniV16S32,
  UniV32S16,
  UniV32S32,

  DivV2S16,
  DivV2S32,
  DivV4S32,
  DivV2S64,
  DivV3S32,
  DivV4S16,
  DivV8S16,
  DivV8S32,
  DivV16S16,
  DivV16S32,
  DivV6S32,
  DivV32S16,
  DivV32S32,

  // B types
  B32,
  B64,
  B96,
  B128,
  B160,
  B256,
  B512,
  BRC,

  UniB32,
  UniB64,
  UniB96,
  UniB128,
  UniB160,
  UniB256,
  UniB512,
  UniBRC,

  DivB32,
  DivB64,
  DivB96,
  DivB128,
  DivB160,
  DivB256,
  DivB512,
  DivBRC
};

// How to apply register bank on register operand.
// In most cases, this serves as a LLT and register bank assert.
// Can change operands and insert copies, extends, truncs, and read-any-lanes.
// Anything more complicated requires LoweringMethod.
enum RegBankLLTMappingApplyID : uint8_t {
  InvalidMapping,
  None,
  IntrId,
  Imm,
  Vcc,

  // any LLT, bank-only apply IDs
  VgprAnyTy,
  AgprAnyTy,
  VgprOrAgprAnyTy,

  // sgpr scalars, pointers, vectors and B-types
  Sgpr16,
  Sgpr32,
  Sgpr64,
  Sgpr128,
  SgprP0,
  SgprP1,
  SgprP2,
  SgprP3,
  SgprP4,
  SgprP5,
  SgprP6,
  SgprP8,
  SgprPtr32,
  SgprPtr64,
  SgprPtr128,
  SgprV2S16,
  SgprV4S32,
  SgprV2S32,
  SgprB32,
  SgprB64,
  SgprB96,
  SgprB128,
  SgprB256,
  SgprB512,
  SgprBRC,

  // vgpr scalars, pointers, vectors and B-types
  Vgpr16,
  Vgpr32,
  Vgpr64,
  Vgpr128,
  VgprP0,
  VgprP1,
  VgprP2,
  VgprP3,
  VgprP4,
  VgprP5,
  VgprPtr32,
  VgprPtr64,
  VgprPtr128,
  VgprV2S16,
  VgprV2S32,
  VgprV3S32,
  VgprB32,
  VgprB64,
  VgprB96,
  VgprB128,
  VgprB160,
  VgprB256,
  VgprB512,
  VgprBRC,
  VgprV4S16,
  VgprV8S16,
  VgprV16S16,
  VgprV4S32,
  VgprV8S32,
  VgprV2S64,

  // Dst only modifiers: read-any-lane and truncs
  UniInVcc,
  UniInVgprS16,
  UniInVgprS32,
  UniInVgprS64,
  UniInVgprV2S16,
  UniInVgprV2S32,
  UniInVgprV3S32,
  UniInVgprV4S32,
  UniInVgprV2S64,
  UniInVgprV6S32,
  UniInVgprV8S16,
  UniInVgprV8S32,
  UniInVgprV16S16,
  UniInVgprV16S32,
  UniInVgprV32S16,
  UniInVgprV32S32,
  UniInVgprB32,
  UniInVgprB64,
  UniInVgprB96,
  UniInVgprB128,
  UniInVgprB160,
  UniInVgprB256,
  UniInVgprB512,

  Sgpr32Trunc,

  // Dst only modifiers: dst was assigned VGPR by RegBankSelect but the
  // instruction result must be in SGPR. Replace dst with SGPR, then copy the
  // result back to the original VGPR.
  Sgpr32ToVgprDst,
  Sgpr64ToVgprDst,

  // Src only modifiers: execute in waterfall loop if divergent
  Sgpr32_WF,
  SgprV4S32_WF,

  // Src only modifiers: execute in waterfall loop for calls
  SgprP0Call_WF,
  SgprP4Call_WF,

  // Src only modifiers: for operands that must end up in M0. If divergent,
  // readfirstlane to SGPR. The result can then be copied to M0 in ISel.
  SgprB32_M0,

  // Src only modifiers: operand must be SGPR, if in VGPR, insert readfirstlane
  // to move to SGPR.
  SgprB32_ReadFirstLane,
  SgprB64_ReadFirstLane,
  SgprV4S32_ReadFirstLane,
  SgprV8S32_ReadFirstLane,

  // Src only modifiers: extends
  Sgpr32AExt,
  Sgpr32AExtBoolInReg,
  Sgpr32SExt,
  Sgpr32ZExt,
  Vgpr32AExt,
  Vgpr32SExt,
  Vgpr32ZExt,

  VgprV6S32,
  VgprV16S32,
  VgprV32S16,
  VgprV32S32,
};

// Instruction needs to be replaced with sequence of instructions. Lowering was
// not done by legalizer since instructions is available in either sgpr or vgpr.
// For example S64 AND is available on sgpr, for that reason S64 AND is legal in
// context of Legalizer that only checks LLT. But S64 AND is not available on
// vgpr. Lower it to two S32 vgpr ANDs.
enum LoweringMethodID : uint8_t {
  DoNotLower,
  VccExtToSel,
  UniExtToSel,
  UnpackBitShift,
  UnpackMinMax,
  S_BFE,
  V_BFE,
  VgprToVccCopy,
  UniMAD64,
  UniMul64,
  DivSMulToMAD,
  SplitTo32,
  SplitTo32Mul,
  ScalarizeToS16,
  SplitTo32Select,
  SplitTo32SExtInReg,
  S_BUF_to_BUF,
  Ext32To64,
  UniCstExt,
  CtPop64To32,
  SplitLoad,
  WidenLoad,
  WidenMMOToS32,
  UnpackAExt,
  VerifyAllSgpr,
  ApplyAllVgpr,
  UnmergeToShiftTrunc,
  AextToS32InIncomingBlockGPHI,
  VerifyAllSgprGPHI,
  VerifyAllSgprOrVgprGPHI,
  ApplyINTRIN_IMAGE,
  ApplyBVH_INTERSECT_RAY,
  SplitBitCount64To32,
  ExtrVecEltToSel,
  ExtrVecEltTo32,
  InsVecEltToSel,
  InsVecEltTo32,
  AbsToNegMax,
  AbsToS32,
  DynStackAlloc,
  DeletePrefetch,
  LowerSetRounding,
  LowerGetRounding
};

enum FastRulesTypes : uint8_t {
  NoFastRules,
  Standard,  // S16, S32, S64, V2S16
  StandardB, // B32, B64, B96, B128
  Vector,    // S32, V2S32, V3S32, V4S32
};

// Subtarget features that rules depend on. A rule can require features to be
// present or absent, see FeatureCond.
enum RegBankLegalizeFeature : uint8_t {
  UseVMulU64Inst,
  HasScalarMulHiInsts,
  HasScalarSMulU64,
  HasScalarCompareEq64,
  Has16BitInsts,
  HasAtomicFlatPkAdd16Insts,
  HasAtomicBufferGlobalPkAddF16Insts,
  HasAtomicDsPkAdd16Insts,
  HasScalarDwordx3Loads,
  HasScalarSubwordLoads,
  UseRealTrue16Insts,
  HasSALUFloatInsts,
  HasPseudoScalarTrans,
  HasSafeSmemPrefetch,
  HasVmemPrefInsts,
  HasSALUMinimumMaximumInsts,
  HasGFX90AInsts
};

using FeatureMask = uint32_t;

constexpr FeatureMask featureBit(RegBankLegalizeFeature F) {
  return FeatureMask(1) << F;
}

// Features that must be present (Required) and absent (Forbidden) for a rule
// to apply. The default condition always holds.
struct FeatureCond {
  FeatureMask Required = 0;
  FeatureMask Forbidden = 0;

  constexpr bool holds(FeatureMask Features) const {
    return (Features & Required) == Required && !(Features & Forbidden);
  }
};

constexpr FeatureCond If(RegBankLegalizeFeature F) {
  return {featureBit(F), 0};
}
constexpr FeatureCond IfNot(RegBankLegalizeFeature F) {
  return {0, featureBit(F)};
}
constexpr FeatureCond operator&&(FeatureCond A, FeatureCond B) {
  return {A.Required | B.Required, A.Forbidden | B.Forbidden};
}

struct RegBankLLTMapping {
  static constexpr unsigned MaxDstOps = 2;
  static constexpr unsigned MaxSrcOps = 10;
  RegBankLLTMappingApplyID DstOps[MaxDstOps] = {};
  RegBankLLTMappingApplyID SrcOps[MaxSrcOps] = {};
  uint8_t NumDstOps = 0;
  uint8_t NumSrcOps = 0;
  LoweringMethodID LoweringMethod;

  // Lists longer than MaxDstOps or MaxSrcOps do not compile in the constant
  // rule tables.
  constexpr RegBankLLTMapping(
      std::initializer_list<RegBankLLTMappingApplyID> DstOpMappingList,
      std::initializer_list<RegBankLLTMappingApplyID> SrcOpMappingList,
      LoweringMethodID LoweringMethod = DoNotLower)
      : NumDstOps(DstOpMappingList.size()), NumSrcOps(SrcOpMappingList.size()),
        LoweringMethod(LoweringMethod) {
    unsigned I = 0;
    for (RegBankLLTMappingApplyID ID : DstOpMappingList)
      DstOps[I++] = ID;
    I = 0;
    for (RegBankLLTMappingApplyID ID : SrcOpMappingList)
      SrcOps[I++] = ID;
  }

  ArrayRef<RegBankLLTMappingApplyID> getDstOpMapping() const {
    return ArrayRef(DstOps, NumDstOps);
  }
  ArrayRef<RegBankLLTMappingApplyID> getSrcOpMapping() const {
    return ArrayRef(SrcOps, NumSrcOps);
  }
};

struct PredicateMapping {
  static constexpr unsigned MaxOps = 9;
  UniformityLLTOpPredicateID OpUniformityAndTypes[MaxOps] = {};
  uint8_t NumOps = 0;
  bool (*TestFunc)(const MachineInstr &) = nullptr;

  constexpr PredicateMapping() = default;
  constexpr PredicateMapping(
      std::initializer_list<UniformityLLTOpPredicateID> OpList,
      bool (*TestFunc)(const MachineInstr &) = nullptr)
      : NumOps(OpList.size()), TestFunc(TestFunc) {
    unsigned I = 0;
    for (UniformityLLTOpPredicateID ID : OpList)
      OpUniformityAndTypes[I++] = ID;
  }

  bool match(const MachineInstr &MI, const MachineUniformityInfo &MUI,
             const MachineRegisterInfo &MRI) const;
};

// One rule of a SetOfRulesForOpcode.
struct RegBankLegalizeRule {
  enum KindTy : uint8_t {
    // "Fast Rules": applies when operand 0 is uniform (or divergent) and of
    // type FastTy.
    UniformFast,
    DivergentFast,
    // "Slow Rules": applies when Predicate matches.
    Slow
  };
  KindTy Kind;
  UniformityLLTOpPredicateID FastTy;
  PredicateMapping Predicate;
  RegBankLLTMapping OperandMapping;
  FeatureCond Cond;
};

// The rules for a group of opcodes, in a constant table. Rules are kept for all
// subtargets; Cond selects those of a subtarget.
struct SetOfRulesForOpcode {
  bool IsIntrinsic;
  FastRulesTypes FastTypes;
  const unsigned *Opcodes;
  unsigned NumOpcodes;
  const RegBankLegalizeRule *Rules;
  unsigned NumRules;

  ArrayRef<RegBankLegalizeRule> rules() const {
    return ArrayRef(Rules, NumRules);
  }

  // "Fast Rules": instead of testing each rule's Predicate, look up the rule
  // for the type and uniformity of operand 0. If fast rules are enabled, a
  // rule must be added for each type that "could match fast Predicate". If
  // not, InvalidMapping is returned which results in failure, and the "Slow
  // Rules" are not searched.
  const RegBankLLTMapping *findMappingForMI(const MachineInstr &MI,
                                            const MachineRegisterInfo &MRI,
                                            const MachineUniformityInfo &MUI,
                                            FeatureMask Features) const;
};

// Essentially 'map<Opcode(or intrinsic_opcode), SetOfRulesForOpcode>' for the
// features of a subtarget. The rules themselves are constant tables shared by
// all subtargets.
class RegBankLegalizeRules {
  FeatureMask Features = 0;

public:
  explicit RegBankLegalizeRules(const GCNSubtarget &ST);

  FeatureMask getFeatures() const { return Features; }

  const SetOfRulesForOpcode *getRulesForOpc(MachineInstr &MI) const;
};

} // end namespace AMDGPU
} // end namespace llvm

#endif
