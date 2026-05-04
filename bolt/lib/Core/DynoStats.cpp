//===- bolt/Core/DynoStats.cpp - Dynamic execution stats ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the DynoStats class.
//
//===----------------------------------------------------------------------===//

#include "bolt/Core/DynoStats.h"
#include "bolt/Core/BinaryBasicBlock.h"
#include "bolt/Core/BinaryFunction.h"
#include "bolt/Core/BoltCoreOptionsOptInfos.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <string>

#undef  DEBUG_TYPE
#define DEBUG_TYPE "bolt"

using namespace llvm;
using namespace bolt;

namespace opts {} // namespace opts

namespace llvm {
namespace bolt {

bool DynoStats::operator<(const DynoStats &Other) const {
  return std::lexicographical_compare(
      &Stats[FIRST_DYNO_STAT], &Stats[LAST_DYNO_STAT],
      &Other.Stats[FIRST_DYNO_STAT], &Other.Stats[LAST_DYNO_STAT]);
}

bool DynoStats::operator==(const DynoStats &Other) const {
  return std::equal(&Stats[FIRST_DYNO_STAT], &Stats[LAST_DYNO_STAT],
                    &Other.Stats[FIRST_DYNO_STAT]);
}

bool DynoStats::lessThan(const DynoStats &Other,
                         ArrayRef<Category> Keys) const {
  return std::lexicographical_compare(
      Keys.begin(), Keys.end(), Keys.begin(), Keys.end(),
      [this, &Other](const Category A, const Category) {
        return Stats[A] < Other.Stats[A];
      });
}

void DynoStats::print(raw_ostream &OS, const DynoStats *Other,
                      MCInstPrinter *Printer) const {
  auto *CoreOpts = bolt_core_opts::getBoltCoreOpts(*Ctx);
  const uint32_t DynoStatsScale =
      CoreOpts ? CoreOpts->get<&clv2::BOLTCORE_DynoStatsScale>() : 1u;
  const uint32_t PrintDynoOpcodeStat =
      CoreOpts ? CoreOpts->get<&clv2::BOLTCORE_PrintDynoOpcodeStat>() : 0u;
  auto printStatWithDelta = [&](const std::string &Name, uint64_t Stat,
                                uint64_t OtherStat) {
    OS << format("%'20lld : ", Stat * DynoStatsScale) << Name;
    if (Other) {
      if (Stat != OtherStat) {
        OtherStat = std::max(OtherStat, uint64_t(1)); // to prevent divide by 0
        OS << format(" (%+.1f%%)", ((float)Stat - (float)OtherStat) * 100.0 /
                                       (float)(OtherStat));
      } else {
        OS << " (=)";
      }
    }
    OS << '\n';
  };

  for (auto Stat = DynoStats::FIRST_DYNO_STAT + 1;
       Stat < DynoStats::LAST_DYNO_STAT; ++Stat) {

    if (!PrintAArch64Stats && Stat == DynoStats::VENEER_CALLS_AARCH64)
      continue;

    printStatWithDelta(Desc[Stat], Stats[Stat], Other ? (*Other)[Stat] : 0);
  }
  if (PrintDynoOpcodeStat && Printer) {
    OS << "\nProgram-wide opcode histogram:\n";
    OS << "              Opcode,   Execution Count,     Max Exec Count, "
          "Function Name:Offset ...\n";
    std::vector<std::pair<uint64_t, unsigned>> SortedHistogram;
    for (const OpcodeStatTy &Stat : OpcodeHistogram)
      SortedHistogram.emplace_back(Stat.second.first, Stat.first);

    // Sort using lexicographic ordering
    llvm::sort(SortedHistogram);

    // Dump in ascending order: Start with Opcode with Highest execution
    // count.
    for (auto &Stat : llvm::reverse(SortedHistogram)) {
      OS << format("%20s,%'18lld", Printer->getOpcodeName(Stat.second).data(),
                   Stat.first * DynoStatsScale);
      auto It = OpcodeHistogram.find(Stat.second);
      assert(It != OpcodeHistogram.end());
      MaxOpcodeHistogramTy MaxMultiMap = It->second.second;
      // Start with function name:BB offset with highest execution count.
      for (auto &Max : llvm::reverse(MaxMultiMap)) {
        OS << format(", %'18lld, ", Max.first * DynoStatsScale)
           << Max.second.first.str() << ':' << Max.second.second;
      }
      OS << '\n';
    }
  }
}

void DynoStats::operator+=(const DynoStats &Other) {
  for (auto Stat = DynoStats::FIRST_DYNO_STAT + 1;
       Stat < DynoStats::LAST_DYNO_STAT; ++Stat) {
    Stats[Stat] += Other[Stat];
  }
  auto *CoreOpts = bolt_core_opts::getBoltCoreOpts(*Ctx);
  const uint32_t PrintDynoOpcodeStat =
      CoreOpts ? CoreOpts->get<&clv2::BOLTCORE_PrintDynoOpcodeStat>() : 0u;
  for (const OpcodeStatTy &Stat : Other.OpcodeHistogram) {
    auto I = OpcodeHistogram.find(Stat.first);
    if (I == OpcodeHistogram.end()) {
      OpcodeHistogram.emplace(Stat);
    } else {
      // Merge other histograms, log only the PrintDynoOpcodeStat'th
      // maximum counts.
      I->second.first += Stat.second.first;
      auto &MMap = I->second.second;
      auto &OtherMMap = Stat.second.second;
      auto Size = MMap.size();
      assert(Size <= PrintDynoOpcodeStat);
      for (auto OtherMMapPair : llvm::reverse(OtherMMap)) {
        if (Size++ >= PrintDynoOpcodeStat) {
          auto First = MMap.begin();
          if (OtherMMapPair.first <= First->first)
            break;
          MMap.erase(First);
        }
        MMap.emplace(OtherMMapPair);
      }
    }
  }
}

DynoStats getDynoStats(BinaryFunction &BF) {
  auto &BC = BF.getBinaryContext();
  const clv2::OptionsContext &Ctx = BC.getOptionsContext();
  auto *CoreOpts = bolt_core_opts::getBoltCoreOpts(Ctx);
  const uint32_t PrintDynoOpcodeStat =
      CoreOpts ? CoreOpts->get<&clv2::BOLTCORE_PrintDynoOpcodeStat>() : 0u;

  DynoStats Stats(/*PrintAArch64Stats*/ BC.isAArch64(), Ctx);

  // Return empty-stats about the function we don't completely understand.
  if (!BF.isSimple() || !BF.hasValidProfile() || !BF.hasCanonicalCFG())
    return Stats;

  // Update enumeration of basic blocks for correct detection of branch'
  // direction.
  BF.getLayout().updateLayoutIndices();

  for (BinaryBasicBlock *const BB : BF.getLayout().blocks()) {
    // The basic block execution count equals to the sum of incoming branch
    // frequencies. This may deviate from the sum of outgoing branches of the
    // basic block especially since the block may contain a function that
    // does not return or a function that throws an exception.
    const uint64_t BBExecutionCount = BB->getKnownExecutionCount();

    // Ignore empty blocks and blocks that were not executed.
    if (BB->getNumNonPseudos() == 0 || BBExecutionCount == 0)
      continue;

    // Count AArch64 linker-inserted veneers
    if (BF.isAArch64Veneer())
      Stats[DynoStats::VENEER_CALLS_AARCH64] += BF.getKnownExecutionCount();

    // Count various instruction types by iterating through all instructions.
    // When -print-dyno-opcode-stats is on, count per each opcode and record
    // maximum execution counts.
    for (const MCInst &Instr : *BB) {
      if (PrintDynoOpcodeStat) {
        unsigned Opcode = Instr.getOpcode();
        auto I = Stats.OpcodeHistogram.find(Opcode);
        if (I == Stats.OpcodeHistogram.end()) {
          DynoStats::MaxOpcodeHistogramTy MMap;
          MMap.emplace(BBExecutionCount,
                       std::make_pair(BF.getOneName(), BB->getOffset()));
          Stats.OpcodeHistogram.emplace(Opcode,
                                        std::make_pair(BBExecutionCount, MMap));
        } else {
          I->second.first += BBExecutionCount;
          bool Insert = true;
          if (I->second.second.size() == PrintDynoOpcodeStat) {
            auto First = I->second.second.begin();
            if (First->first < BBExecutionCount)
              I->second.second.erase(First);
            else
              Insert = false;
          }
          if (Insert) {
            I->second.second.emplace(
                BBExecutionCount,
                std::make_pair(BF.getOneName(), BB->getOffset()));
          }
        }
      }

      if (BC.MIB->mayStore(Instr)) {
        Stats[DynoStats::STORES] += BBExecutionCount;
      }
      if (BC.MIB->mayLoad(Instr)) {
        Stats[DynoStats::LOADS] += BBExecutionCount;
      }
      if (!BC.MIB->isCall(Instr))
        continue;

      uint64_t CallFreq = BBExecutionCount;
      if (BC.MIB->getConditionalTailCall(Instr)) {
        CallFreq =
            BC.MIB->getAnnotationWithDefault<uint64_t>(Instr, "CTCTakenCount");
      }
      Stats[DynoStats::FUNCTION_CALLS] += CallFreq;
      if (BC.MIB->isIndirectCall(Instr)) {
        Stats[DynoStats::INDIRECT_CALLS] += CallFreq;
      } else if (const MCSymbol *CallSymbol = BC.MIB->getTargetSymbol(Instr)) {
        const BinaryFunction *BF = BC.getFunctionForSymbol(CallSymbol);
        if (BF && BF->isPLTFunction()) {
          Stats[DynoStats::PLT_CALLS] += CallFreq;

          // We don't process PLT functions and hence have to adjust relevant
          // dynostats here for:
          //
          //   jmp *GOT_ENTRY(%rip)
          //
          // NOTE: this is arch-specific.
          Stats[DynoStats::FUNCTION_CALLS] += CallFreq;
          Stats[DynoStats::INDIRECT_CALLS] += CallFreq;
          Stats[DynoStats::LOADS] += CallFreq;
          Stats[DynoStats::INSTRUCTIONS] += CallFreq;
        }
      }
    }

    Stats[DynoStats::INSTRUCTIONS] += BB->getNumNonPseudos() * BBExecutionCount;

    // Jump tables.
    const MCInst *LastInstr = BB->getLastNonPseudoInstr();
    if (BC.MIB->getJumpTable(*LastInstr)) {
      Stats[DynoStats::JUMP_TABLE_BRANCHES] += BBExecutionCount;
      LLVM_DEBUG(
        static uint64_t MostFrequentJT;
        if (BBExecutionCount > MostFrequentJT) {
          MostFrequentJT = BBExecutionCount;
          dbgs() << "BOLT-INFO: most frequently executed jump table is in "
                 << "function " << BF << " in basic block " << BB->getName()
                 << " executed totally " << BBExecutionCount << " times.\n";
        }
      );
      continue;
    }

    if (BC.MIB->isIndirectBranch(*LastInstr) && !BC.MIB->isCall(*LastInstr)) {
      Stats[DynoStats::UNKNOWN_INDIRECT_BRANCHES] += BBExecutionCount;
      continue;
    }

    // Update stats for branches.
    const MCSymbol *TBB = nullptr;
    const MCSymbol *FBB = nullptr;
    MCInst *CondBranch = nullptr;
    MCInst *UncondBranch = nullptr;
    if (!BB->analyzeBranch(TBB, FBB, CondBranch, UncondBranch))
      continue;

    if (!CondBranch && !UncondBranch)
      continue;

    // Simple unconditional branch.
    if (!CondBranch) {
      Stats[DynoStats::UNCOND_BRANCHES] += BBExecutionCount;
      continue;
    }

    // CTCs: instruction annotations could be stripped, hence check the number
    // of successors to identify conditional tail calls.
    if (BB->succ_size() == 1) {
      if (BB->branch_info_begin() != BB->branch_info_end())
        Stats[DynoStats::UNCOND_BRANCHES] += BB->branch_info_begin()->Count;
      continue;
    }

    // Conditional branch that could be followed by an unconditional branch.
    uint64_t TakenCount = BB->getTakenBranchInfo().Count;
    if (TakenCount == BinaryBasicBlock::COUNT_NO_PROFILE)
      TakenCount = 0;

    uint64_t NonTakenCount = BB->getFallthroughBranchInfo().Count;
    if (NonTakenCount == BinaryBasicBlock::COUNT_NO_PROFILE)
      NonTakenCount = 0;

    if (BF.isForwardBranch(BB, BB->getConditionalSuccessor(true))) {
      Stats[DynoStats::FORWARD_COND_BRANCHES] += BBExecutionCount;
      Stats[DynoStats::FORWARD_COND_BRANCHES_TAKEN] += TakenCount;
    } else {
      Stats[DynoStats::BACKWARD_COND_BRANCHES] += BBExecutionCount;
      Stats[DynoStats::BACKWARD_COND_BRANCHES_TAKEN] += TakenCount;
    }

    if (UncondBranch) {
      Stats[DynoStats::UNCOND_BRANCHES] += NonTakenCount;
    }
  }

  return Stats;
}

} // namespace bolt
} // namespace llvm
