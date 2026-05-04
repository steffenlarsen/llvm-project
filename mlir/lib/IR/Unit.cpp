//===- Unit.cpp - Support for manipulating IR Unit ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/Unit.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/OperationSupport.h"
#include "mlir/IR/Region.h"
#include "llvm/Support/raw_ostream.h"
#include <iterator>

using namespace mlir;

static void printOp(llvm::raw_ostream &os, Operation *op,
                    OpPrintingFlags &flags) {
  if (!op) {
    os << "<Operation:nullptr>";
    return;
  }
  op->print(os, flags);
}

static void printRegion(llvm::raw_ostream &os, Region *region,
                        OpPrintingFlags &flags) {
  if (!region) {
    os << "<Region:nullptr>";
    return;
  }
  os << "Region #" << region->getRegionNumber() << " for op ";
  printOp(os, region->getParentOp(), flags);
}

static void printBlock(llvm::raw_ostream &os, Block *block,
                       OpPrintingFlags &flags) {
  Region *region = block->getParent();
  os << "Block #" << block->computeBlockNumber() << " for ";
  bool shouldSkipRegions = flags.shouldSkipRegions();
  printRegion(os, region, flags.skipRegions());
  if (!shouldSkipRegions)
    block->print(os);
}

MLIRContext *mlir::IRUnit::getContext() const {
  if (auto *op = llvm::dyn_cast_if_present<Operation *>(*this))
    return op->getContext();
  if (auto *region = llvm::dyn_cast_if_present<Region *>(*this))
    return region->getContext();
  if (auto *block = llvm::dyn_cast_if_present<Block *>(*this)) {
    // Null for an unlinked block; printRegion below renders that case as
    // <Region:nullptr> rather than treating it as impossible.
    Region *parent = block->getParent();
    return parent ? parent->getContext() : nullptr;
  }
  if (auto value = llvm::dyn_cast_if_present<Value>(*this))
    return value.getContext();
  llvm_unreachable("unknown IRUnit");
}

void mlir::IRUnit::print(llvm::raw_ostream &os, OpPrintingFlags flags) const {
  if (auto *op = llvm::dyn_cast_if_present<Operation *>(*this))
    return printOp(os, op, flags);
  if (auto *region = llvm::dyn_cast_if_present<Region *>(*this))
    return printRegion(os, region, flags);
  if (auto *block = llvm::dyn_cast_if_present<Block *>(*this))
    return printBlock(os, block, flags);
  llvm_unreachable("unknown IRUnit");
}

llvm::raw_ostream &mlir::operator<<(llvm::raw_ostream &os, const IRUnit &unit) {
  unit.print(os);
  return os;
}
