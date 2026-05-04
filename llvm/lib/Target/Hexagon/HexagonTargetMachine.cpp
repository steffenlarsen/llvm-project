//===-- HexagonTargetMachine.cpp - Define TargetMachine for Hexagon -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the info about Hexagon target spec.
//
//===----------------------------------------------------------------------===//

#include "HexagonTargetMachine.h"
#include "Hexagon.h"
#include "HexagonISelLowering.h"
#include "HexagonLoopIdiomRecognition.h"
#include "HexagonMachineFunctionInfo.h"
#include "HexagonMachineScheduler.h"
#include "HexagonTargetObjectFile.h"
#include "HexagonTargetTransformInfo.h"
#include "HexagonVectorLoopCarriedReuse.h"
#include "TargetInfo/HexagonTargetInfo.h"
#include "llvm/CodeGen/MIRParser/MIParser.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/VLIWMachineScheduler.h"
#include "llvm/IR/Function.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Target/Hexagon/HexagonOptionsOptInfos.h"
#include "llvm/Transforms/Scalar.h"
#include <optional>

using namespace llvm;

/// HexagonTargetMachineModule - Note that this is used on hosts that
/// cannot link in a library unless there are references into the
/// library.  In particular, it seems that it is not possible to get
/// things to work on Win32 without this.  Though it is unused, do not
/// remove it.
extern "C" int HexagonTargetMachineModule;
int HexagonTargetMachineModule = 0;

static ScheduleDAGInstrs *createVLIWMachineSched(MachineSchedContext *C) {
  ScheduleDAGMILive *DAG = new VLIWMachineScheduler(
      C, std::make_unique<HexagonConvergingVLIWScheduler>());
  DAG->addMutation(std::make_unique<HexagonSubtarget::UsrOverflowMutation>());
  DAG->addMutation(std::make_unique<HexagonSubtarget::HVXMemLatencyMutation>());
  DAG->addMutation(std::make_unique<HexagonSubtarget::CallMutation>());
  DAG->addMutation(createCopyConstrainDAGMutation(DAG->TII, DAG->TRI));
  return DAG;
}

static MachineSchedRegistry
    SchedCustomRegistry("hexagon", "Run Hexagon's custom scheduler",
                        createVLIWMachineSched);

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::Static);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeHexagonTarget() {
  // Register the target.
  RegisterTargetMachine<HexagonTargetMachine> X(getTheHexagonTarget());

  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeHexagonAlignGlobalArraysPass(PR);
  initializeHexagonAsmPrinterPass(PR);
  initializeHexagonBitSimplifyPass(PR);
  initializeHexagonConstExtendersPass(PR);
  initializeHexagonConstPropagationPass(PR);
  initializeHexagonCopyToCombinePass(PR);
  initializeHexagonEarlyIfConversionPass(PR);
  initializeHexagonGenMemAbsolutePass(PR);
  initializeHexagonGenMuxPass(PR);
  initializeHexagonGlobalSchedulerPass(PR);
  initializeHexagonLiveVariablesPass(PR);
  initializeHexagonHardwareLoopsPass(PR);
  initializeHexagonHVXSaveRemarkPass(PR);
  initializeHexagonLoopIdiomRecognizeLegacyPassPass(PR);
  initializeHexagonNewValueJumpPass(PR);
  initializeHexagonOptAddrModePass(PR);
  initializeHexagonPacketizerPass(PR);
  initializeHexagonRDFOptPass(PR);
  initializeHexagonSplitDoubleRegsPass(PR);
  initializeHexagonVectorCombineLegacyPass(PR);
  initializeHexagonVectorLoopCarriedReuseLegacyPassPass(PR);
  initializeHexagonVExtractPass(PR);
  initializeHexagonDAGToDAGISelLegacyPass(PR);
  initializeHexagonLoopReschedulingPass(PR);
  initializeHexagonBranchRelaxationPass(PR);
  initializeHexagonCFGOptimizerPass(PR);
  initializeHexagonCommonGEPPass(PR);
  initializeHexagonCopyHoistingPass(PR);
  initializeHexagonExpandCondsetsPass(PR);
  initializeHexagonLoopAlignPass(PR);
  initializeHexagonTfrCleanupPass(PR);
  initializeHexagonFixupHwLoopsPass(PR);
  initializeHexagonCallFrameInformationPass(PR);
  initializeHexagonGenExtractPass(PR);
  initializeHexagonGenInsertPass(PR);
  initializeHexagonGenPredicatePass(PR);
  initializeHexagonLoadWideningPass(PR);
  initializeHexagonStoreWideningPass(PR);
  initializeHexagonMaskPass(PR);
  initializeHexagonOptimizeSZextendsPass(PR);
  initializeHexagonPeepholePass(PR);
  initializeHexagonSplitConst32AndConst64Pass(PR);
  initializeHexagonVectorPrintPass(PR);
  initializeHexagonQFPOptimizerPass(PR);
  initializeHexagonXQFloatGeneratorPass(PR);
  initializeHexagonPostRAHandleQFPPass(PR);
  initializeMachineKCFILegacyPass(PR);
}

HexagonTargetMachine::HexagonTargetMachine(const Target &T, const Triple &TT,
                                           StringRef CPU, StringRef FS,
                                           const TargetOptions &Options,
                                           std::optional<Reloc::Model> RM,
                                           std::optional<CodeModel::Model> CM,
                                           CodeGenOptLevel OL, bool JIT)
    // Specify the vector alignment explicitly. For v512x1, the calculated
    // alignment would be 512*alignment(i1), which is 512 bytes, instead of
    // the required minimum of 64 bytes.
    : CodeGenTargetMachineImpl(
          T, TT.computeDataLayout(), TT, CPU, FS, Options,
          getEffectiveRelocModel(RM),
          getEffectiveCodeModel(CM, CodeModel::Small),
          (clv2::getOptValOr<&clv2::HexagonOptsReg, &clv2::HEX_NoOpt>(
               Options.getOptsCtx(), false)
               ? CodeGenOptLevel::None
               : OL)),
      TLOF(std::make_unique<HexagonTargetObjectFile>()),
      Subtarget(Triple(TT), CPU, FS, *this) {
  initAsmInfo();
}

const HexagonSubtarget *
HexagonTargetMachine::getSubtargetImpl(const Function &F) const {
  AttributeList FnAttrs = F.getAttributes();
  Attribute CPUAttr = FnAttrs.getFnAttr("target-cpu");
  Attribute FSAttr = FnAttrs.getFnAttr("target-features");

  std::string CPU =
      CPUAttr.isValid() ? CPUAttr.getValueAsString().str() : TargetCPU;
  std::string FS =
      FSAttr.isValid() ? FSAttr.getValueAsString().str() : TargetFS;

  auto &I = SubtargetMap[CPU + FS];
  if (!I)
    I = std::make_unique<HexagonSubtarget>(TargetTriple, CPU, FS, *this);
  return I.get();
}

void HexagonTargetMachine::registerPassBuilderCallbacks(PassBuilder &PB) {
#define GET_PASS_REGISTRY "HexagonPassRegistry.def"
#include "llvm/Passes/TargetPassRegistry.inc"

  PB.registerLateLoopOptimizationsEPCallback(
      [=](LoopPassManager &LPM, OptimizationLevel Level) {
        if (Level != OptimizationLevel::O0)
          LPM.addPass(HexagonLoopIdiomRecognitionPass());
      });
  PB.registerLoopOptimizerEndEPCallback(
      [=](LoopPassManager &LPM, OptimizationLevel Level) {
        if (Level != OptimizationLevel::O0)
          LPM.addPass(HexagonVectorLoopCarriedReusePass());
      });
}

TargetTransformInfo
HexagonTargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(std::make_unique<HexagonTTIImpl>(this, F));
}

MachineFunctionInfo *HexagonTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return HexagonMachineFunctionInfo::create<HexagonMachineFunctionInfo>(
      Allocator, F, STI);
}

yaml::MachineFunctionInfo *
HexagonTargetMachine::createDefaultFuncInfoYAML() const {
  return new yaml::HexagonFunctionInfo();
}

yaml::MachineFunctionInfo *
HexagonTargetMachine::convertFuncInfoToYAML(const MachineFunction &MF) const {
  const auto *MFI = MF.getInfo<HexagonMachineFunctionInfo>();
  const auto &TRI = *MF.getSubtarget().getRegisterInfo();
  return new yaml::HexagonFunctionInfo(*MFI, TRI);
}

bool HexagonTargetMachine::parseMachineFunctionInfo(
    const yaml::MachineFunctionInfo &MFI_, PerFunctionMIParsingState &PFS,
    SMDiagnostic &Error, SMRange &SourceRange) const {
  const auto &YamlMFI = static_cast<const yaml::HexagonFunctionInfo &>(MFI_);
  MachineFunction &MF = PFS.MF;
  HexagonMachineFunctionInfo *MFI = MF.getInfo<HexagonMachineFunctionInfo>();

  MFI->initializeBaseYamlFields(YamlMFI);

  // Parse StackAlignBaseReg register name
  if (!YamlMFI.StackAlignBaseReg.Value.empty()) {
    Register Reg;
    if (parseNamedRegisterReference(PFS, Reg, YamlMFI.StackAlignBaseReg.Value,
                                    Error)) {
      SourceRange = YamlMFI.StackAlignBaseReg.SourceRange;
      return true;
    }
    MFI->setStackAlignBaseReg(Reg);
  }

  return false;
}

HexagonTargetMachine::~HexagonTargetMachine() = default;

ScheduleDAGInstrs *
HexagonTargetMachine::createMachineScheduler(MachineSchedContext *C) const {
  return createVLIWMachineSched(C);
}

namespace {
/// Hexagon Code Generator Pass Configuration Options.
class HexagonPassConfig : public TargetPassConfig {
public:
  HexagonPassConfig(HexagonTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  HexagonTargetMachine &getHexagonTargetMachine() const {
    return getTM<HexagonTargetMachine>();
  }

  void addIRPasses() override;
  bool addInstSelector() override;
  bool addILPOpts() override;
  void addPreRegAlloc() override;
  void addPostRegAlloc() override;
  void addPreSched2() override;
  void addPreEmitPass() override;
};
} // namespace

TargetPassConfig *HexagonTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new HexagonPassConfig(*this, PM);
}

void HexagonPassConfig::addIRPasses() {
  TargetPassConfig::addIRPasses();
  bool NoOpt = (getOptLevel() == CodeGenOptLevel::None);

  if (!NoOpt) {
    // Raise the alignment of global integer arrays to 8 bytes. At -O1/-O2,
    // reduce .rodata size by keeping byte/half-word arrays at their natural
    // alignment; apply full 8-byte alignment at -O3.
    addPass(createHexagonAlignGlobalArrays(getOptLevel() !=
                                           CodeGenOptLevel::Aggressive));
    if (clv2::getOptValOr<&clv2::HexagonOptsReg, &clv2::HEX_EnableInstSimplify>(
            TM->getOptionsContext(), true))
      addPass(createInstSimplifyLegacyPass());
    addPass(createDeadCodeEliminationPass());
  }

  addPass(createAtomicExpandLegacyPass());

  if (!NoOpt) {
    if (clv2::getOptValOr<&clv2::HexagonOptsReg,
                          &clv2::HEX_EnableInitialCFGCleanup>(
            TM->getOptionsContext(), true))
      addPass(createCFGSimplificationPass(TM->getOptionsContext(),
                                          SimplifyCFGOptions()
                                              .forwardSwitchCondToPhi(true)
                                              .convertSwitchRangeToICmp(true)
                                              .convertSwitchToLookupTable(true)
                                              .needCanonicalLoops(false)
                                              .hoistCommonInsts(true)
                                              .sinkCommonInsts(true)));
    if (clv2::getOptValOr<&clv2::HexagonOptsReg, &clv2::HEX_EnableLoopPrefetch>(
            TM->getOptionsContext(), false))
      addPass(createLoopDataPrefetchPass());
    if (clv2::getOptValOr<&clv2::HexagonOptsReg,
                          &clv2::HEX_EnableVectorCombine>(
            TM->getOptionsContext(), true))
      addPass(createHexagonVectorCombineLegacyPass());
    if (clv2::getOptValOr<&clv2::HexagonOptsReg, &clv2::HEX_EnableCommGEP>(
            TM->getOptionsContext(), true))
      addPass(createHexagonCommonGEP());
    // Replace certain combinations of shifts and ands with extracts.
    if (clv2::getOptValOr<&clv2::HexagonOptsReg, &clv2::HEX_EnableGenExtract>(
            TM->getOptionsContext(), true))
      addPass(createHexagonGenExtract());
  }
}

bool HexagonPassConfig::addInstSelector() {
  HexagonTargetMachine &TM = getHexagonTargetMachine();
  const HexagonSubtarget *HST = TM.getHexagonSubtarget();
  bool NoOpt = (getOptLevel() == CodeGenOptLevel::None);

  if (!NoOpt)
    addPass(createHexagonOptimizeSZextends());

  addPass(createHexagonISelDag(TM, getOptLevel()));
  // Run the QFloat mode code generation pass only if v79 or greater.
  // Do not run this pass, if legacy mode is passed on command line.
  {
    auto QFMode =
        clv2::getOptValOrDefault<&clv2::HEX_QFloatMode>(TM.getOptionsContext());
    if (HST->useHVXV79Ops() && (QFMode != QFloatMode::Legacy))
      addPass(createHexagonXQFloatGenerator());
  }

  if (!NoOpt) {
    if (clv2::getOptValOrDefault<&clv2::HEX_EnableVExtractOpt>(
            TM.getOptionsContext()))
      addPass(createHexagonVExtract());
    // Create logical operations on predicate registers.
    if (clv2::getOptValOrDefault<&clv2::HEX_EnableGenPred>(
            TM.getOptionsContext()))
      addPass(createHexagonGenPredicate());
    // Rotate loops to expose bit-simplification opportunities.
    if (clv2::getOptValOrDefault<&clv2::HEX_EnableLoopResched>(
            TM.getOptionsContext()))
      addPass(createHexagonLoopRescheduling());
    // Split double registers.
    if (!clv2::getOptValOr<&clv2::HexagonOptsReg, &clv2::HEX_DisableHSDR>(
            TM.getOptionsContext(), false))
      addPass(createHexagonSplitDoubleRegs());
    // Bit simplification.
    if (clv2::getOptValOrDefault<&clv2::HEX_EnableBitSimplify>(
            TM.getOptionsContext()))
      addPass(createHexagonBitSimplify());
    addPass(createHexagonPeephole());
    // Constant propagation.
    if (!clv2::getOptValOr<&clv2::HexagonOptsReg, &clv2::HEX_DisableHCP>(
            TM.getOptionsContext(), false)) {
      addPass(createHexagonConstPropagationPass());
      addPass(&UnreachableMachineBlockElimID);
    }
    if (clv2::getOptValOrDefault<&clv2::HEX_EnableGenInsert>(
            TM.getOptionsContext()))
      addPass(createHexagonGenInsert());
    if (clv2::getOptValOrDefault<&clv2::HEX_EnableEarlyIf>(
            TM.getOptionsContext()))
      addPass(createHexagonEarlyIfConversion());
    // For v75 or below, or if legacy mode is requested, run QFPOptizer pass
    // to preserve backward compatibility.
    {
      auto QFMode2 = clv2::getOptValOrDefault<&clv2::HEX_QFloatMode>(
          TM.getOptionsContext());
      if (!HST->useHVXV79Ops() || (QFMode2 == QFloatMode::Legacy))
        addPass(createHexagonQFPOptimizer());
    }
  }

  return false;
}

bool HexagonPassConfig::addILPOpts() {
  if (clv2::getOptValOr<&clv2::HexagonOptsReg, &clv2::HEX_EnableMCR>(
          TM->getOptionsContext(), true))
    addPass(&MachineCombinerID);

  return true;
}

void HexagonPassConfig::addPreRegAlloc() {
  if (getOptLevel() != CodeGenOptLevel::None) {
    if (clv2::getOptValOr<&clv2::HexagonOptsReg, &clv2::HEX_EnableCExtOpt>(
            TM->getOptionsContext(), true))
      addPass(createHexagonConstExtenders());
    if (clv2::getOptValOr<&clv2::HexagonOptsReg,
                          &clv2::HEX_EnableExpandCondsets>(
            TM->getOptionsContext(), true))
      insertPass(&RegisterCoalescerID, &HexagonExpandCondsetsID);
    if (clv2::getOptValOr<&clv2::HexagonOptsReg, &clv2::HEX_EnableCopyHoist>(
            TM->getOptionsContext(), true))
      insertPass(&RegisterCoalescerID, &HexagonCopyHoistingID);
    if (clv2::getOptValOr<&clv2::HexagonOptsReg, &clv2::HEX_EnableTfrCleanup>(
            TM->getOptionsContext(), true))
      insertPass(&VirtRegRewriterID, &HexagonTfrCleanupID);
    if (!clv2::getOptValOr<&clv2::HexagonOptsReg,
                           &clv2::HEX_DisableStoreWidening>(
            TM->getOptionsContext(), false))
      addPass(createHexagonStoreWidening());
    if (!clv2::getOptValOr<&clv2::HexagonOptsReg,
                           &clv2::HEX_DisableLoadWidening>(
            TM->getOptionsContext(), false))
      addPass(createHexagonLoadWidening());
    if (clv2::getOptValOr<&clv2::HexagonOptsReg, &clv2::HEX_EnableGenMemAbs>(
            TM->getOptionsContext(), true))
      addPass(createHexagonGenMemAbsolute());
    if (!clv2::getOptValOr<&clv2::HexagonOptsReg,
                           &clv2::HEX_DisableHardwareLoops>(
            TM->getOptionsContext(), false))
      addPass(createHexagonHardwareLoops());
  }
  if (TM->getOptLevel() >= CodeGenOptLevel::Default)
    addPass(&MachinePipelinerID);
  addPass(createHexagonHVXSaveRemark());
}

void HexagonPassConfig::addPostRegAlloc() {
  HexagonTargetMachine &HTM = getHexagonTargetMachine();
  const HexagonSubtarget *HST = HTM.getHexagonSubtarget();
  // Run PostRAQFP on v79 and above.
  if (clv2::getOptValOr<&clv2::HexagonOptsReg,
                        &clv2::HEX_EnablePostRAHandleQFP>(
          TM->getOptionsContext(), true) &&
      HST->useHVXV79Ops())
    addPass(createHexagonPostRAHandleQFP());

  if (getOptLevel() != CodeGenOptLevel::None) {
    if (clv2::getOptValOr<&clv2::HexagonOptsReg, &clv2::HEX_EnableRDFOpt>(
            TM->getOptionsContext(), true))
      addPass(createHexagonRDFOpt());
    if (!clv2::getOptValOr<&clv2::HexagonOptsReg, &clv2::HEX_DisableCFGOpt>(
            TM->getOptionsContext(), false))
      addPass(createHexagonCFGOptimizer());
    if (!clv2::getOptValOr<&clv2::HexagonOptsReg, &clv2::HEX_DisableAModeOpt>(
            TM->getOptionsContext(), false))
      addPass(createHexagonOptAddrMode());
  }
}

void HexagonPassConfig::addPreSched2() {
  bool NoOpt = (getOptLevel() == CodeGenOptLevel::None);
  addPass(createHexagonCopyToCombine());
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(&IfConverterID);
  addPass(createHexagonSplitConst32AndConst64());
  if (!NoOpt &&
      !clv2::getOptValOr<&clv2::HexagonOptsReg, &clv2::HEX_DisableMask>(
          TM->getOptionsContext(), false))
    addPass(createHexagonMask());

  if (!NoOpt &&
      !clv2::getOptValOr<&clv2::HexagonOptsReg, &clv2::HEX_DisableLiveVars>(
          TM->getOptionsContext(), false)) {
    addPass(&HexagonLiveVariablesID);
  }
}

void HexagonPassConfig::addPreEmitPass() {
  bool NoOpt = (getOptLevel() == CodeGenOptLevel::None);

  if (!NoOpt)
    addPass(createHexagonNewValueJump());

  addPass(createHexagonBranchRelaxation());

  if (!NoOpt) {
    if (!clv2::getOptValOr<&clv2::HexagonOptsReg,
                           &clv2::HEX_DisableHardwareLoops>(
            TM->getOptionsContext(), false))
      addPass(createHexagonFixupHwLoops());
    // Generate MUX from pairs of conditional transfers.
    if (clv2::getOptValOr<&clv2::HexagonOptsReg, &clv2::HEX_EnableGenMux>(
            TM->getOptionsContext(), true))
      addPass(createHexagonGenMux());
    if (!clv2::getOptValOr<&clv2::HexagonOptsReg, &clv2::HEX_DisableLiveVars>(
            TM->getOptionsContext(), false))
      addPass(&HexagonLiveVariablesID);
  }

  // Emit KCFI checks for indirect calls. Must run before packetization so
  // the check and call can be bundled together into a VLIW packet.
  addPass(createKCFIPass());

  // Packetization is mandatory: it handles gather/scatter at all opt levels.
  addPass(createHexagonPacketizer(NoOpt));

  if (!NoOpt) {
    // Global pull-up scheduler
    addPass(createHexagonGlobalScheduler());

    addPass(createHexagonLoopAlign());
  }

  if (clv2::getOptValOr<&clv2::HexagonOptsReg, &clv2::HEX_EnableVectorPrint>(
          TM->getOptionsContext(), false))
    addPass(createHexagonVectorPrint());

  // Add CFI instructions if necessary.
  addPass(createHexagonCallFrameInformation());
}
