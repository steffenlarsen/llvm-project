//===- MachineBlockFrequencyInfo.cpp - MBB Frequency Analysis -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Loops should be simplified before this analysis.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Analysis/AnalysisOptionsOptInfos.h"
#include "llvm/Analysis/BlockFrequencyInfoImpl.h"
#include "llvm/CodeGen/CodeGenPassOptionsOptInfos.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineCycleAnalysis.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/OptionsContext.h"
#include <optional>
#include <string>

using namespace llvm;

#define DEBUG_TYPE "machine-block-freq"

// --- clv2 OptionInfo descriptors for MachineBlockFrequencyInfo options ---
static constexpr clv2::EnumVal<GVDAGType> ViewBlockLayoutWithBFIVals[] = {
    {"none", GVDT_None, "do not display graphs."},
    {"fraction", GVDT_Fraction,
     "display a graph using the fractional block frequency representation."},
    {"integer", GVDT_Integer,
     "display a graph using the raw integer fractional block frequency "
     "representation."},
    {"count", GVDT_Count,
     "display a graph using the real profile count if available."},
};
static constexpr auto OI_ViewBlockLayoutWithBFI =
    clv2::makeEnumOption<GVDAGType>(
        "view-block-layout-with-bfi",
        "Pop up a window to show a dag displaying MBP layout and "
        "associated block frequencies of the CFG.",
        ViewBlockLayoutWithBFIVals, clv2::Init{GVDT_None}, clv2::Hidden);

static constexpr clv2::EnumVal<GVDAGType> ViewMachineBlockFreqPropDAGVals[] = {
    {"none", GVDT_None, "do not display graphs."},
    {"fraction", GVDT_Fraction,
     "display a graph using the fractional block frequency "
     "representation."},
    {"integer", GVDT_Integer,
     "display a graph using the raw integer fractional block frequency "
     "representation."},
    {"count", GVDT_Count,
     "display a graph using the real profile count if available."},
};
static constexpr auto OI_ViewMachineBlockFreqPropDAG =
    clv2::makeEnumOption<GVDAGType>(
        "view-machine-block-freq-propagation-dags",
        "Pop up a window to show a dag displaying how machine block "
        "frequencies propagate through the CFG.",
        ViewMachineBlockFreqPropDAGVals, clv2::Init{GVDT_None}, clv2::Hidden);

static constexpr clv2::OptionsRegistry<&OI_ViewBlockLayoutWithBFI,
                                       &OI_ViewMachineBlockFreqPropDAG>
    MBFIOptsReg;

namespace an_opts = llvm::an_opts;

static std::string getViewBlockFreqFuncName(const clv2::OptionsContext &Ctx) {
  return std::string(
      clv2::getOptValOr<&clv2::AnalysisOptsReg,
                        &clv2::AN_ViewBlockFreqFuncName>(Ctx, std::string{}));
}

static unsigned getViewHotFreqPercent(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOrDefault<&clv2::AN_ViewHotFreqPercent>(Ctx);
}

static std::string getPrintBFIFuncName(const clv2::OptionsContext &Ctx) {
  return std::string(
      clv2::getOptValOr<&clv2::AnalysisOptsReg, &clv2::AN_PrintBFIFuncName>(
          Ctx, std::string{}));
}

static bool getPrintMachineBfi(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOrDefault<&clv2::CGPASS_PrintMachineBfi>(Ctx);
}

static GVDAGType getGVDT(const clv2::OptionsContext &Ctx) {
  GVDAGType Layout =
      clv2::getOptValOr<&MBFIOptsReg, &OI_ViewBlockLayoutWithBFI>(Ctx,
                                                                  GVDT_None);
  if (Layout != GVDT_None)
    return Layout;

  return clv2::getOptValOr<&MBFIOptsReg, &OI_ViewMachineBlockFreqPropDAG>(
      Ctx, GVDT_None);
}

template <> struct llvm::GraphTraits<MachineBlockFrequencyInfo *> {
  using NodeRef = const MachineBasicBlock *;
  using ChildIteratorType = MachineBasicBlock::const_succ_iterator;
  using nodes_iterator = pointer_iterator<MachineFunction::const_iterator>;

  static NodeRef getEntryNode(const MachineBlockFrequencyInfo *G) {
    return &G->getFunction()->front();
  }

  static ChildIteratorType child_begin(const NodeRef N) {
    return N->succ_begin();
  }

  static ChildIteratorType child_end(const NodeRef N) { return N->succ_end(); }

  static nodes_iterator nodes_begin(const MachineBlockFrequencyInfo *G) {
    return nodes_iterator(G->getFunction()->begin());
  }

  static nodes_iterator nodes_end(const MachineBlockFrequencyInfo *G) {
    return nodes_iterator(G->getFunction()->end());
  }
};

using MBFIDOTGraphTraitsBase =
    BFIDOTGraphTraitsBase<MachineBlockFrequencyInfo,
                          MachineBranchProbabilityInfo>;

template <>
struct llvm::DOTGraphTraits<MachineBlockFrequencyInfo *>
    : public MBFIDOTGraphTraitsBase {
  const MachineFunction *CurFunc = nullptr;
  DenseMap<const MachineBasicBlock *, int> LayoutOrderMap;

  explicit DOTGraphTraits(bool isSimple = false)
      : MBFIDOTGraphTraitsBase(isSimple) {}

  std::string getNodeLabel(const MachineBasicBlock *Node,
                           const MachineBlockFrequencyInfo *Graph) {
    int layout_order = -1;
    // Attach additional ordering information if 'isSimple' is false.
    if (!isSimple()) {
      const MachineFunction *F = Node->getParent();
      if (!CurFunc || F != CurFunc) {
        if (CurFunc)
          LayoutOrderMap.clear();

        CurFunc = F;
        int O = 0;
        for (auto MBI = F->begin(); MBI != F->end(); ++MBI, ++O) {
          LayoutOrderMap[&*MBI] = O;
        }
      }
      layout_order = LayoutOrderMap[Node];
    }
    const auto &Ctx =
        Node->getParent()->getFunction().getContext().getOptionsContext();
    return MBFIDOTGraphTraitsBase::getNodeLabel(Node, Graph, getGVDT(Ctx),
                                                layout_order);
  }

  std::string getNodeAttributes(const MachineBasicBlock *Node,
                                const MachineBlockFrequencyInfo *Graph) {
    const auto &Ctx =
        Node->getParent()->getFunction().getContext().getOptionsContext();
    return MBFIDOTGraphTraitsBase::getNodeAttributes(
        Node, Graph, getViewHotFreqPercent(Ctx));
  }

  std::string getEdgeAttributes(const MachineBasicBlock *Node, EdgeIter EI,
                                const MachineBlockFrequencyInfo *MBFI) {
    const auto &Ctx =
        Node->getParent()->getFunction().getContext().getOptionsContext();
    return MBFIDOTGraphTraitsBase::getEdgeAttributes(
        Node, EI, MBFI, MBFI->getMBPI(), getViewHotFreqPercent(Ctx));
  }
};

AnalysisKey MachineBlockFrequencyAnalysis::Key;

MachineBlockFrequencyAnalysis::Result
MachineBlockFrequencyAnalysis::run(MachineFunction &MF,
                                   MachineFunctionAnalysisManager &MFAM) {
  auto &MBPI = MFAM.getResult<MachineBranchProbabilityAnalysis>(MF);
  auto &MCI = MFAM.getResult<MachineCycleAnalysis>(MF);
  return Result(MF, MBPI, MCI);
}

PreservedAnalyses
MachineBlockFrequencyPrinterPass::run(MachineFunction &MF,
                                      MachineFunctionAnalysisManager &MFAM) {
  auto &MBFI = MFAM.getResult<MachineBlockFrequencyAnalysis>(MF);
  OS << "Machine block frequency for machine function: " << MF.getName()
     << '\n';
  MBFI.print(OS);
  return PreservedAnalyses::all();
}

INITIALIZE_PASS_BEGIN(MachineBlockFrequencyInfoWrapperPass, DEBUG_TYPE,
                      "Machine Block Frequency Analysis", true, true)
INITIALIZE_PASS_DEPENDENCY(MachineBranchProbabilityInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineCycleInfoWrapperPass)
INITIALIZE_PASS_END(MachineBlockFrequencyInfoWrapperPass, DEBUG_TYPE,
                    "Machine Block Frequency Analysis", true, true)

char MachineBlockFrequencyInfoWrapperPass::ID = 0;

MachineBlockFrequencyInfoWrapperPass::MachineBlockFrequencyInfoWrapperPass()
    : MachineFunctionPass(ID) {}

MachineBlockFrequencyInfo::MachineBlockFrequencyInfo() = default;

MachineBlockFrequencyInfo::MachineBlockFrequencyInfo(
    MachineBlockFrequencyInfo &&) = default;

MachineBlockFrequencyInfo::MachineBlockFrequencyInfo(
    const MachineFunction &F, const MachineBranchProbabilityInfo &MBPI,
    const MachineCycleInfo &MCI) {
  calculate(F, MBPI, MCI);
}

MachineBlockFrequencyInfo::~MachineBlockFrequencyInfo() = default;

bool MachineBlockFrequencyInfo::invalidate(
    MachineFunction &MF, const PreservedAnalyses &PA,
    MachineFunctionAnalysisManager::Invalidator &) {
  // Check whether the analysis, all analyses on machine functions, or the
  // machine function's CFG have been preserved.
  auto PAC = PA.getChecker<MachineBlockFrequencyAnalysis>();
  return !PAC.preserved() &&
         !PAC.preservedSet<AllAnalysesOn<MachineFunction>>() &&
         !PAC.preservedSet<CFGAnalyses>();
}

void MachineBlockFrequencyInfoWrapperPass::getAnalysisUsage(
    AnalysisUsage &AU) const {
  AU.addRequired<MachineBranchProbabilityInfoWrapperPass>();
  AU.addRequired<MachineCycleInfoWrapperPass>();
  AU.setPreservesAll();
  MachineFunctionPass::getAnalysisUsage(AU);
}

void MachineBlockFrequencyInfo::calculate(
    const MachineFunction &F, const MachineBranchProbabilityInfo &MBPI,
    const MachineCycleInfo &MCI) {
  if (!MBFI)
    MBFI.reset(new ImplType);
  const clv2::OptionsContext &Ctx =
      F.getFunction().getContext().getOptionsContext();
  MBFI->setOptionsContext(Ctx);
  MBFI->calculate(F, MBPI, MCI);
  if (getGVDT(Ctx) != GVDT_None &&
      (getViewBlockFreqFuncName(Ctx).empty() ||
       F.getName() == getViewBlockFreqFuncName(Ctx))) {
    view("MachineBlockFrequencyDAGS." + F.getName());
  }
  if (getPrintMachineBfi(F.getFunction().getContext().getOptionsContext()) &&
      (getPrintBFIFuncName(Ctx).empty() ||
       F.getName() == getPrintBFIFuncName(Ctx))) {
    MBFI->print(dbgs());
  }
}

bool MachineBlockFrequencyInfoWrapperPass::runOnMachineFunction(
    MachineFunction &F) {
  MachineBranchProbabilityInfo &MBPI =
      getAnalysis<MachineBranchProbabilityInfoWrapperPass>().getMBPI();
  MachineCycleInfo &MCI =
      getAnalysis<MachineCycleInfoWrapperPass>().getCycleInfo();
  MBFI.calculate(F, MBPI, MCI);
  return false;
}

void MachineBlockFrequencyInfo::print(raw_ostream &OS) { MBFI->print(OS); }

void MachineBlockFrequencyInfo::releaseMemory() { MBFI.reset(); }

/// Pop up a ghostview window with the current block frequency propagation
/// rendered using dot.
void MachineBlockFrequencyInfo::view(const Twine &Name, bool isSimple) const {
  // This code is only for debugging.
  ViewGraph(const_cast<MachineBlockFrequencyInfo *>(this), Name, isSimple);
}

BlockFrequency
MachineBlockFrequencyInfo::getBlockFreq(const MachineBasicBlock *MBB) const {
  return MBFI ? MBFI->getBlockFreq(MBB) : BlockFrequency(0);
}

std::optional<uint64_t> MachineBlockFrequencyInfo::getBlockProfileCount(
    const MachineBasicBlock *MBB) const {
  if (!MBFI)
    return std::nullopt;

  const Function &F = MBFI->getFunction()->getFunction();
  return MBFI->getBlockProfileCount(F, MBB);
}

std::optional<uint64_t>
MachineBlockFrequencyInfo::getProfileCountFromFreq(BlockFrequency Freq) const {
  if (!MBFI)
    return std::nullopt;

  const Function &F = MBFI->getFunction()->getFunction();
  return MBFI->getProfileCountFromFreq(F, Freq);
}

bool MachineBlockFrequencyInfo::isIrrLoopHeader(
    const MachineBasicBlock *MBB) const {
  assert(MBFI && "Expected analysis to be available");
  return MBFI->isIrrLoopHeader(MBB);
}

void MachineBlockFrequencyInfo::onEdgeSplit(
    const MachineBasicBlock &NewPredecessor,
    const MachineBasicBlock &NewSuccessor,
    const MachineBranchProbabilityInfo &MBPI) {
  assert(MBFI && "Expected analysis to be available");
  auto NewSuccFreq = MBFI->getBlockFreq(&NewPredecessor) *
                     MBPI.getEdgeProbability(&NewPredecessor, &NewSuccessor);

  MBFI->setBlockFreq(&NewSuccessor, NewSuccFreq);
}

const MachineFunction *MachineBlockFrequencyInfo::getFunction() const {
  return MBFI ? MBFI->getFunction() : nullptr;
}

const MachineBranchProbabilityInfo *MachineBlockFrequencyInfo::getMBPI() const {
  return MBFI ? &MBFI->getBPI() : nullptr;
}

BlockFrequency MachineBlockFrequencyInfo::getEntryFreq() const {
  return MBFI ? MBFI->getEntryFreq() : BlockFrequency(0);
}

Printable llvm::printBlockFreq(const MachineBlockFrequencyInfo &MBFI,
                               BlockFrequency Freq) {
  return Printable([&MBFI, Freq](raw_ostream &OS) {
    printRelativeBlockFreq(OS, MBFI.getEntryFreq(), Freq);
  });
}

Printable llvm::printBlockFreq(const MachineBlockFrequencyInfo &MBFI,
                               const MachineBasicBlock &MBB) {
  return printBlockFreq(MBFI, MBFI.getBlockFreq(&MBB));
}
