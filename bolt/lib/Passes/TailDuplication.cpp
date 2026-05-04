//===- bolt/Passes/TailDuplication.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the TailDuplication class.
//
//===----------------------------------------------------------------------===//

#include "bolt/Passes/TailDuplication.h"
#include "bolt/Core/BoltCoreOptionsOptInfos.h"
#include "bolt/Passes/BoltPassesOptionsOptInfos.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/CommandLineV2.h"

#include <numeric>
#include <queue>

#define DEBUG_TYPE "taildup"

using namespace llvm;

namespace opts {} // namespace opts

namespace llvm {
namespace bolt {

void TailDuplication::getCallerSavedRegs(const MCInst &Inst, BitVector &Regs,
                                         BinaryContext &BC) const {
  if (!BC.MIB->isCall(Inst))
    return;
  BitVector CallRegs = BitVector(BC.MRI->getNumRegs(), false);
  BC.MIB->getCalleeSavedRegs(CallRegs);
  CallRegs.flip();
  Regs |= CallRegs;
}

bool TailDuplication::regIsPossiblyOverwritten(const MCInst &Inst, unsigned Reg,
                                               BinaryContext &BC) const {
  BitVector WrittenRegs = BitVector(BC.MRI->getNumRegs(), false);
  BC.MIB->getWrittenRegs(Inst, WrittenRegs);
  getCallerSavedRegs(Inst, WrittenRegs, BC);
  if (BC.MIB->isRep(Inst))
    BC.MIB->getRepRegs(WrittenRegs);
  const BitVector &AllAliases = BC.MIB->getAliases(Reg, false);
  return WrittenRegs.anyCommon(AllAliases);
}

bool TailDuplication::regIsDefinitelyOverwritten(const MCInst &Inst,
                                                 unsigned Reg,
                                                 BinaryContext &BC) const {
  BitVector WrittenRegs = BitVector(BC.MRI->getNumRegs(), false);
  BC.MIB->getWrittenRegs(Inst, WrittenRegs);
  getCallerSavedRegs(Inst, WrittenRegs, BC);
  if (BC.MIB->isRep(Inst))
    BC.MIB->getRepRegs(WrittenRegs);
  return (!regIsUsed(Inst, Reg, BC) && WrittenRegs.test(Reg) &&
          !BC.MIB->isConditionalMove(Inst));
}

bool TailDuplication::regIsUsed(const MCInst &Inst, unsigned Reg,
                                BinaryContext &BC) const {
  BitVector SrcRegs = BitVector(BC.MRI->getNumRegs(), false);
  BC.MIB->getSrcRegs(Inst, SrcRegs);
  const BitVector &SmallerAliases = BC.MIB->getAliases(Reg, true);
  return SrcRegs.anyCommon(SmallerAliases);
}

bool TailDuplication::isOverwrittenBeforeUsed(BinaryBasicBlock &StartBB,
                                              unsigned Reg) const {
  BinaryFunction *BF = StartBB.getFunction();
  BinaryContext &BC = BF->getBinaryContext();
  std::queue<BinaryBasicBlock *> Q;
  for (auto Itr = StartBB.succ_begin(); Itr != StartBB.succ_end(); ++Itr) {
    BinaryBasicBlock *NextBB = *Itr;
    Q.push(NextBB);
  }
  SmallPtrSet<BinaryBasicBlock *, 16> Visited;
  // Breadth first search through successive blocks and see if Reg is ever used
  // before its overwritten
  while (Q.size() > 0) {
    BinaryBasicBlock *CurrBB = Q.front();
    Q.pop();
    if (Visited.count(CurrBB))
      continue;
    Visited.insert(CurrBB);
    bool Overwritten = false;
    for (auto Itr = CurrBB->begin(); Itr != CurrBB->end(); ++Itr) {
      MCInst &Inst = *Itr;
      if (regIsUsed(Inst, Reg, BC))
        return false;
      if (regIsDefinitelyOverwritten(Inst, Reg, BC)) {
        Overwritten = true;
        break;
      }
    }
    if (Overwritten)
      continue;
    for (auto Itr = CurrBB->succ_begin(); Itr != CurrBB->succ_end(); ++Itr) {
      BinaryBasicBlock *NextBB = *Itr;
      Q.push(NextBB);
    }
  }
  return true;
}

void TailDuplication::constantAndCopyPropagate(
    BinaryBasicBlock &OriginalBB,
    std::vector<BinaryBasicBlock *> &BlocksToPropagate) {
  BinaryFunction *BF = OriginalBB.getFunction();
  BinaryContext &BC = BF->getBinaryContext();

  BlocksToPropagate.insert(BlocksToPropagate.begin(), &OriginalBB);
  // Iterate through the original instructions to find one to propagate
  for (auto Itr = OriginalBB.begin(); Itr != OriginalBB.end();) {
    MCInst &OriginalInst = *Itr;
    // It must be a non conditional
    if (BC.MIB->isConditionalMove(OriginalInst)) {
      ++Itr;
      continue;
    }

    // Move immediate or move register
    if ((!BC.MII->get(OriginalInst.getOpcode()).isMoveImmediate() ||
         !OriginalInst.getOperand(1).isImm()) &&
        (!BC.MII->get(OriginalInst.getOpcode()).isMoveReg() ||
         !OriginalInst.getOperand(1).isReg())) {
      ++Itr;
      continue;
    }

    // True if this is constant propagation and not copy propagation
    bool ConstantProp = BC.MII->get(OriginalInst.getOpcode()).isMoveImmediate();
    // The Register to replaced
    unsigned Reg = OriginalInst.getOperand(0).getReg();
    // True if the register to replace was replaced everywhere it was used
    bool ReplacedEverywhere = true;
    // True if the register was definitely overwritten
    bool Overwritten = false;
    // True if the register to replace and the register to replace with (for
    // copy propagation) has not been overwritten and is still usable
    bool RegsActive = true;

    // Iterate through successor blocks and through their instructions
    for (BinaryBasicBlock *NextBB : BlocksToPropagate) {
      for (auto PropagateItr =
               ((NextBB == &OriginalBB) ? Itr + 1 : NextBB->begin());
           PropagateItr < NextBB->end(); ++PropagateItr) {
        MCInst &PropagateInst = *PropagateItr;
        if (regIsUsed(PropagateInst, Reg, BC)) {
          bool Replaced = false;
          // If both registers are active for copy propagation or the register
          // to replace is active for constant propagation
          if (RegsActive) {
            // Set Replaced and so ReplacedEverwhere to false if it cannot be
            // replaced (no replacing that opcode, Register is src and dest)
            if (ConstantProp)
              Replaced = BC.MIB->replaceRegWithImm(
                  PropagateInst, Reg, OriginalInst.getOperand(1).getImm());
            else
              Replaced = BC.MIB->replaceRegWithReg(
                  PropagateInst, Reg, OriginalInst.getOperand(1).getReg());
          }
          ReplacedEverywhere = ReplacedEverywhere && Replaced;
        }
        // For copy propagation, make sure no propagation happens after the
        // register to replace with is overwritten
        if (!ConstantProp &&
            regIsPossiblyOverwritten(PropagateInst,
                                     OriginalInst.getOperand(1).getReg(), BC))
          RegsActive = false;

        // Make sure no propagation happens after the register to replace is
        // overwritten
        if (regIsPossiblyOverwritten(PropagateInst, Reg, BC))
          RegsActive = false;

        // Record if the register to replace is overwritten
        if (regIsDefinitelyOverwritten(PropagateInst, Reg, BC)) {
          Overwritten = true;
          break;
        }
      }
      if (Overwritten)
        break;
    }

    // If the register was replaced everywhere and it was overwritten in either
    // one of the iterated through blocks or one of the successor blocks, delete
    // the original move instruction
    if (ReplacedEverywhere &&
        (Overwritten ||
         isOverwrittenBeforeUsed(
             *BlocksToPropagate[BlocksToPropagate.size() - 1], Reg))) {
      // If both registers are active for copy propagation or the register
      // to replace is active for constant propagation
      StaticInstructionDeletionCount++;
      DynamicInstructionDeletionCount += OriginalBB.getExecutionCount();
      Itr = OriginalBB.eraseInstruction(Itr);
    } else {
      ++Itr;
    }
  }
}

bool TailDuplication::isInCacheLine(const BinaryBasicBlock &BB,
                                    const BinaryBasicBlock &Succ) const {
  if (&BB == &Succ)
    return true;

  auto *PassOpts = bolt_passes_opts::getBoltPassesOpts(*OptsCtx);
  uint64_t Distance = 0;
  int Direction = (Succ.getLayoutIndex() > BB.getLayoutIndex()) ? 1 : -1;

  for (unsigned I = BB.getLayoutIndex() + Direction; I != Succ.getLayoutIndex();
       I += Direction) {
    Distance += BB.getFunction()->getLayout().getBlock(I)->getOriginalSize();
    if (Distance >
        PassOpts->get<&clv2::BOLTPASS_TailDuplicationMinimumOffset>())
      return false;
  }
  return true;
}

std::vector<BinaryBasicBlock *>
TailDuplication::moderateDuplicate(BinaryBasicBlock &BB,
                                   BinaryBasicBlock &Tail) const {
  std::vector<BinaryBasicBlock *> BlocksToDuplicate;
  // The block must be hot
  if (BB.getKnownExecutionCount() == 0)
    return BlocksToDuplicate;
  // and its successor is not already in the same cache line
  if (isInCacheLine(BB, Tail))
    return BlocksToDuplicate;
  // and its size do not exceed the maximum allowed size
  auto *PassOpts = bolt_passes_opts::getBoltPassesOpts(*OptsCtx);
  if (Tail.getOriginalSize() >
      PassOpts->get<&clv2::BOLTPASS_TailDuplicationMaximumDuplication>())
    return BlocksToDuplicate;
  // If duplicating would introduce a new branch, don't duplicate
  for (auto Itr = Tail.succ_begin(); Itr != Tail.succ_end(); ++Itr) {
    if ((*Itr)->getLayoutIndex() == Tail.getLayoutIndex() + 1)
      return BlocksToDuplicate;
  }

  BlocksToDuplicate.push_back(&Tail);
  return BlocksToDuplicate;
}

std::vector<BinaryBasicBlock *>
TailDuplication::aggressiveDuplicate(BinaryBasicBlock &BB,
                                     BinaryBasicBlock &Tail) const {
  std::vector<BinaryBasicBlock *> BlocksToDuplicate;
  // The block must be hot
  if (BB.getKnownExecutionCount() == 0)
    return BlocksToDuplicate;
  // and its successor is not already in the same cache line
  if (isInCacheLine(BB, Tail))
    return BlocksToDuplicate;

  BinaryBasicBlock *CurrBB = &Tail;
  while (CurrBB) {
    LLVM_DEBUG(dbgs() << "Aggressive tail duplication: adding "
                      << CurrBB->getName() << " to duplication list\n";);
    BlocksToDuplicate.push_back(CurrBB);

    if (CurrBB->hasJumpTable()) {
      LLVM_DEBUG(dbgs() << "Aggressive tail duplication: clearing duplication "
                           "list due to a JT in "
                        << CurrBB->getName() << '\n';);
      BlocksToDuplicate.clear();
      break;
    }

    // With no successors, we've reached the end and should duplicate all of
    // BlocksToDuplicate
    if (CurrBB->succ_size() == 0)
      break;

    // With two successors, if they're both a jump, we should duplicate all
    // blocks in BlocksToDuplicate. Otherwise, we cannot find a simple stream of
    // blocks to copy
    if (CurrBB->succ_size() >= 2) {
      if (CurrBB->getConditionalSuccessor(false)->getLayoutIndex() ==
              CurrBB->getLayoutIndex() + 1 ||
          CurrBB->getConditionalSuccessor(true)->getLayoutIndex() ==
              CurrBB->getLayoutIndex() + 1) {
        LLVM_DEBUG(dbgs() << "Aggressive tail duplication: clearing "
                             "duplication list, can't find a simple stream at "
                          << CurrBB->getName() << '\n';);
        BlocksToDuplicate.clear();
      }
      break;
    }

    // With one successor, if its a jump, we should duplicate all blocks in
    // BlocksToDuplicate. Otherwise, we should keep going
    BinaryBasicBlock *SuccBB = CurrBB->getSuccessor();
    if (SuccBB->getLayoutIndex() != CurrBB->getLayoutIndex() + 1)
      break;
    CurrBB = SuccBB;
  }
  // Don't duplicate if its too much code
  unsigned DuplicationByteCount = std::accumulate(
      std::begin(BlocksToDuplicate), std::end(BlocksToDuplicate), 0,
      [](int value, BinaryBasicBlock *p) {
        return value + p->getOriginalSize();
      });
  auto *PassOpts = bolt_passes_opts::getBoltPassesOpts(*OptsCtx);
  if (DuplicationByteCount >
      PassOpts->get<&clv2::BOLTPASS_TailDuplicationMaximumDuplication>()) {
    LLVM_DEBUG(
        dbgs() << "Aggressive tail duplication: duplication byte count ("
               << DuplicationByteCount << ") exceeds maximum "
               << PassOpts
                      ->get<&clv2::BOLTPASS_TailDuplicationMaximumDuplication>()
               << '\n';);
    BlocksToDuplicate.clear();
  }
  LLVM_DEBUG(dbgs() << "Aggressive tail duplication: found "
                    << BlocksToDuplicate.size() << " blocks to duplicate\n";);
  return BlocksToDuplicate;
}

bool TailDuplication::shouldDuplicate(BinaryBasicBlock *Pred,
                                      BinaryBasicBlock *Tail) const {
  if (Pred == Tail)
    return false;
  // Cannot duplicate non-tail blocks
  if (Tail->succ_size() != 0)
    return false;
  // The blocks are already in the order
  if (Pred->getLayoutIndex() + 1 == Tail->getLayoutIndex())
    return false;
  // No tail duplication for blocks with jump tables
  if (Pred->hasJumpTable())
    return false;
  if (Tail->hasJumpTable())
    return false;

  return true;
}

double TailDuplication::cacheScore(uint64_t SrcAddr, uint64_t SrcSize,
                                   uint64_t DstAddr, uint64_t DstSize,
                                   uint64_t Count) const {
  assert(Count != BinaryBasicBlock::COUNT_NO_PROFILE);

  auto *PassOpts = bolt_passes_opts::getBoltPassesOpts(*OptsCtx);

  bool IsForwardJump = SrcAddr <= DstAddr;
  uint64_t JumpDistance = 0;
  // Computing the length of the jump so that it takes the sizes of the two
  // blocks into consideration
  if (IsForwardJump) {
    JumpDistance = (DstAddr + DstSize) - (SrcAddr);
  } else {
    JumpDistance = (SrcAddr + SrcSize) - (DstAddr);
  }

  if (JumpDistance >=
      PassOpts->get<&clv2::BOLTPASS_TailDuplicationMaxCacheDistance>())
    return 0;
  double Prob =
      1.0 -
      static_cast<double>(JumpDistance) /
          PassOpts->get<&clv2::BOLTPASS_TailDuplicationMaxCacheDistance>();
  return (IsForwardJump
              ? 1.0
              : PassOpts->get<
                    &clv2::BOLTPASS_TailDuplicationCacheBackwardWeight>()) *
         Prob * Count;
}

bool TailDuplication::cacheScoreImproved(const MCCodeEmitter *Emitter,
                                         BinaryFunction &BF,
                                         BinaryBasicBlock *Pred,
                                         BinaryBasicBlock *Tail) const {
  // Collect (estimated) basic block sizes
  DenseMap<const BinaryBasicBlock *, uint64_t> BBSize;
  for (const BinaryBasicBlock &BB : BF) {
    BBSize[&BB] = std::max<uint64_t>(BB.estimateSize(Emitter), 1);
  }

  // Build current addresses of basic blocks starting at the entry block
  DenseMap<BinaryBasicBlock *, uint64_t> CurAddr;
  uint64_t Addr = 0;
  for (BinaryBasicBlock *SrcBB : BF.getLayout().blocks()) {
    CurAddr[SrcBB] = Addr;
    Addr += BBSize[SrcBB];
  }

  // Build new addresses (after duplication) starting at the entry block
  DenseMap<BinaryBasicBlock *, uint64_t> NewAddr;
  Addr = 0;
  for (BinaryBasicBlock *SrcBB : BF.getLayout().blocks()) {
    NewAddr[SrcBB] = Addr;
    Addr += BBSize[SrcBB];
    if (SrcBB == Pred)
      Addr += BBSize[Tail];
  }

  // Compute the cache score for the existing layout of basic blocks
  double CurScore = 0;
  for (BinaryBasicBlock *SrcBB : BF.getLayout().blocks()) {
    auto BI = SrcBB->branch_info_begin();
    for (BinaryBasicBlock *DstBB : SrcBB->successors()) {
      if (SrcBB != DstBB) {
        CurScore += cacheScore(CurAddr[SrcBB], BBSize[SrcBB], CurAddr[DstBB],
                               BBSize[DstBB], BI->Count);
      }
      ++BI;
    }
  }

  // Compute the cache score for the layout of blocks after tail duplication
  double NewScore = 0;
  for (BinaryBasicBlock *SrcBB : BF.getLayout().blocks()) {
    auto BI = SrcBB->branch_info_begin();
    for (BinaryBasicBlock *DstBB : SrcBB->successors()) {
      if (SrcBB != DstBB) {
        if (SrcBB == Pred && DstBB == Tail) {
          NewScore += cacheScore(NewAddr[SrcBB], BBSize[SrcBB],
                                 NewAddr[SrcBB] + BBSize[SrcBB], BBSize[DstBB],
                                 BI->Count);
        } else {
          NewScore += cacheScore(NewAddr[SrcBB], BBSize[SrcBB], NewAddr[DstBB],
                                 BBSize[DstBB], BI->Count);
        }
      }
      ++BI;
    }
  }

  return NewScore > CurScore;
}

std::vector<BinaryBasicBlock *>
TailDuplication::cacheDuplicate(const MCCodeEmitter *Emitter,
                                BinaryFunction &BF, BinaryBasicBlock *Pred,
                                BinaryBasicBlock *Tail) const {
  std::vector<BinaryBasicBlock *> BlocksToDuplicate;
  auto *PassOpts = bolt_passes_opts::getBoltPassesOpts(*OptsCtx);

  // No need to duplicate cold basic blocks
  if (Pred->isCold() || Tail->isCold()) {
    return BlocksToDuplicate;
  }
  // Always duplicate "small" tail basic blocks, which might be beneficial for
  // code size, since a jump instruction is eliminated
  if (Tail->estimateSize(Emitter) <=
      PassOpts->get<&clv2::BOLTPASS_TailDuplicationMinimumDuplication>()) {
    BlocksToDuplicate.push_back(Tail);
    return BlocksToDuplicate;
  }
  // Never duplicate "large" tail basic blocks
  if (Tail->estimateSize(Emitter) >
      PassOpts->get<&clv2::BOLTPASS_TailDuplicationMaximumDuplication>()) {
    return BlocksToDuplicate;
  }
  // Do not append basic blocks after the last hot block in the current layout
  auto NextBlock = BF.getLayout().getBasicBlockAfter(Pred);
  if (NextBlock == nullptr || (!Pred->isCold() && NextBlock->isCold())) {
    return BlocksToDuplicate;
  }

  // Duplicate the tail only if it improves the cache score
  if (cacheScoreImproved(Emitter, BF, Pred, Tail)) {
    BlocksToDuplicate.push_back(Tail);
  }

  return BlocksToDuplicate;
}

std::vector<BinaryBasicBlock *> TailDuplication::duplicateBlocks(
    BinaryBasicBlock &BB,
    const std::vector<BinaryBasicBlock *> &BlocksToDuplicate) const {
  BinaryFunction *BF = BB.getFunction();
  BinaryContext &BC = BF->getBinaryContext();

  // Ratio of this new branches execution count to the total size of the
  // successor's execution count.  Used to set this new branches execution count
  // and lower the old successor's execution count
  double ExecutionCountRatio =
      BB.getExecutionCount() >= BB.getSuccessor()->getExecutionCount()
          ? 1.0
          : (double)BB.getExecutionCount() /
                BB.getSuccessor()->getExecutionCount();

  // Use the last branch info when adding a successor to LastBB
  BinaryBasicBlock::BinaryBranchInfo &LastBI =
      BB.getBranchInfo(*(BB.getSuccessor()));

  BinaryBasicBlock *LastOriginalBB = &BB;
  BinaryBasicBlock *LastDuplicatedBB = &BB;
  assert(LastDuplicatedBB->succ_size() == 1 &&
         "tail duplication cannot act on a block with more than 1 successor");
  LastDuplicatedBB->removeSuccessor(LastDuplicatedBB->getSuccessor());

  std::vector<std::unique_ptr<BinaryBasicBlock>> DuplicatedBlocks;
  std::vector<BinaryBasicBlock *> DuplicatedBlocksToReturn;

  for (BinaryBasicBlock *CurBB : BlocksToDuplicate) {
    DuplicatedBlocks.emplace_back(
        BF->createBasicBlock((BC.Ctx)->createNamedTempSymbol("tail-dup")));
    BinaryBasicBlock *NewBB = DuplicatedBlocks.back().get();

    NewBB->addInstructions(CurBB->begin(), CurBB->end());
    // Set execution count as if it was just a copy of the original
    NewBB->setExecutionCount(CurBB->getExecutionCount());
    NewBB->setIsCold(CurBB->isCold());
    LastDuplicatedBB->addSuccessor(NewBB, LastBI);

    DuplicatedBlocksToReturn.push_back(NewBB);

    // As long as its not the first block, adjust both original and duplicated
    // to what they should be
    if (LastDuplicatedBB != &BB) {
      LastOriginalBB->adjustExecutionCount(1.0 - ExecutionCountRatio);
      LastDuplicatedBB->adjustExecutionCount(ExecutionCountRatio);
    }

    if (CurBB->succ_size() == 1)
      LastBI = CurBB->getBranchInfo(*(CurBB->getSuccessor()));

    LastOriginalBB = CurBB;
    LastDuplicatedBB = NewBB;
  }

  LastDuplicatedBB->addSuccessors(
      LastOriginalBB->succ_begin(), LastOriginalBB->succ_end(),
      LastOriginalBB->branch_info_begin(), LastOriginalBB->branch_info_end());

  LastOriginalBB->adjustExecutionCount(1.0 - ExecutionCountRatio);
  LastDuplicatedBB->adjustExecutionCount(ExecutionCountRatio);

  BF->insertBasicBlocks(&BB, std::move(DuplicatedBlocks));

  return DuplicatedBlocksToReturn;
}

void TailDuplication::runOnFunction(BinaryFunction &Function) {
  auto *PassOpts = bolt_passes_opts::getBoltPassesOpts(*OptsCtx);
  auto *CoreOpts = bolt_core_opts::getBoltCoreOpts(*OptsCtx);
  bool DoNoThreads = CoreOpts->get<&clv2::BOLTCORE_NoThreads>();

  // Create a separate MCCodeEmitter to allow lock-free execution
  BinaryContext &BC = Function.getBinaryContext();
  BinaryContext::IndependentCodeEmitter Emitter;
  if (!DoNoThreads) {
    Emitter = BC.createIndependentMCCodeEmitter();
  }

  Function.getLayout().updateLayoutIndices();

  // New blocks will be added and layout will change,
  // so make a copy here to iterate over the original layout
  BinaryFunction::BasicBlockOrderType BlockLayout(
      Function.getLayout().block_begin(), Function.getLayout().block_end());
  bool ModifiedFunction = false;
  for (BinaryBasicBlock *BB : BlockLayout) {
    AllDynamicCount += BB->getKnownExecutionCount();

    // The block must be with one successor
    if (BB->succ_size() != 1)
      continue;
    BinaryBasicBlock *Tail = BB->getSuccessor();
    // Verify that the tail should be duplicated
    if (!shouldDuplicate(BB, Tail))
      continue;

    std::vector<BinaryBasicBlock *> BlocksToDuplicate;
    auto TailDuplicationModeOpt =
        static_cast<bolt::TailDuplication::DuplicationMode>(
            PassOpts->get<&clv2::BOLTPASS_TailDuplication>());
    if (TailDuplicationModeOpt == TailDuplication::TD_AGGRESSIVE) {
      BlocksToDuplicate = aggressiveDuplicate(*BB, *Tail);
    } else if (TailDuplicationModeOpt == TailDuplication::TD_MODERATE) {
      BlocksToDuplicate = moderateDuplicate(*BB, *Tail);
    } else if (TailDuplicationModeOpt == TailDuplication::TD_CACHE) {
      BlocksToDuplicate = cacheDuplicate(Emitter.MCE.get(), Function, BB, Tail);
    } else {
      llvm_unreachable("unknown tail duplication mode");
    }

    if (BlocksToDuplicate.empty())
      continue;

    // Apply the duplication
    ModifiedFunction = true;
    DuplicationsDynamicCount += BB->getExecutionCount();
    auto DuplicatedBlocks = duplicateBlocks(*BB, BlocksToDuplicate);
    for (BinaryBasicBlock *BB : DuplicatedBlocks) {
      DuplicatedBlockCount++;
      DuplicatedByteCount += BB->estimateSize(Emitter.MCE.get());
    }

    if (PassOpts->get<&clv2::BOLTPASS_TailDuplicationConstCopyPropagation>()) {
      constantAndCopyPropagate(*BB, DuplicatedBlocks);
      BinaryBasicBlock *FirstBB = BlocksToDuplicate[0];
      if (FirstBB->pred_size() == 1) {
        BinaryBasicBlock *PredBB = *FirstBB->pred_begin();
        if (PredBB->succ_size() == 1)
          constantAndCopyPropagate(*PredBB, BlocksToDuplicate);
      }
    }

    // Layout indices might be stale after duplication
    Function.getLayout().updateLayoutIndices();
  }
  if (ModifiedFunction)
    ModifiedFunctions++;
}

Error TailDuplication::runOnFunctions(BinaryContext &BC) {
  OptsCtx = &BC.getOptionsContext();
  auto *PassOpts = bolt_passes_opts::getBoltPassesOpts(*OptsCtx);

  auto TailDuplicationModeOpt =
      static_cast<bolt::TailDuplication::DuplicationMode>(
          PassOpts->get<&clv2::BOLTPASS_TailDuplication>());
  if (TailDuplicationModeOpt == TailDuplication::TD_NONE)
    return Error::success();

  for (auto &It : BC.getBinaryFunctions()) {
    BinaryFunction &Function = It.second;
    if (!shouldOptimize(Function))
      continue;
    runOnFunction(Function);
  }

  BC.outs()
      << "BOLT-INFO: tail duplication"
      << format(" modified %zu (%.2f%%) functions;", ModifiedFunctions,
                100.0 * ModifiedFunctions / BC.getBinaryFunctions().size())
      << format(" duplicated %zu blocks (%zu bytes) responsible for",
                DuplicatedBlockCount, DuplicatedByteCount)
      << format(" %zu dynamic executions (%.2f%% of all block executions)",
                DuplicationsDynamicCount,
                100.0 * DuplicationsDynamicCount / AllDynamicCount)
      << "\n";

  if (PassOpts->get<&clv2::BOLTPASS_TailDuplicationConstCopyPropagation>()) {
    BC.outs() << "BOLT-INFO: tail duplication "
              << format(
                     "applied %zu static and %zu dynamic propagation deletions",
                     StaticInstructionDeletionCount,
                     DynamicInstructionDeletionCount)
              << "\n";
  }
  return Error::success();
}

} // end namespace bolt
} // end namespace llvm
