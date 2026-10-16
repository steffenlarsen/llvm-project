//===- LocalSlots.cpp - Locals assigned once in CIR functions -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/Transforms/OffloadOpt/LocalSlots.h"
#include "clang/CIR/Dialect/Transforms/CIRTransformUtils.h"

using namespace mlir;

cir::AllocaOp cir::findSlot(Value addr) {
  while (auto cast = addr.getDefiningOp<cir::CastOp>())
    addr = cast.getOperand();
  return addr.getDefiningOp<cir::AllocaOp>();
}

void cir::collectSlotUsers(cir::AllocaOp slot,
                           SmallVectorImpl<Operation *> &out) {
  SmallVector<Value, 4> worklist{slot.getResult()};
  while (!worklist.empty()) {
    Value v = worklist.pop_back_val();
    for (Operation *user : v.getUsers()) {
      if (auto cast = dyn_cast<cir::CastOp>(user)) {
        worklist.push_back(cast.getResult());
        continue;
      }
      if (isa<cir::LifetimeStartOp, cir::LifetimeEndOp>(user))
        continue;
      out.push_back(user);
    }
  }
}

bool cir::addrIsSlot(Value addr, cir::AllocaOp slot) {
  return findSlot(addr) == slot;
}

cir::StoreOp cir::onlyStore(cir::AllocaOp slot) {
  cir::StoreOp only;
  SmallVector<Operation *, 8> slotUsers;
  collectSlotUsers(slot, slotUsers);
  for (Operation *user : slotUsers) {
    if (auto store = dyn_cast<cir::StoreOp>(user)) {
      if (only || !addrIsSlot(store.getAddr(), slot))
        return {};
      only = store;
      continue;
    }
    if (!isa<cir::LoadOp>(user))
      return {};
  }
  return only;
}

std::optional<int64_t> cir::tryConstThroughSlot(Value v) {
  if (std::optional<int64_t> c = cir::tryResolveToConstant(v))
    return c;

  auto load = v.getDefiningOp<cir::LoadOp>();
  if (!load)
    return std::nullopt;
  cir::AllocaOp slot = findSlot(load.getAddr());
  cir::StoreOp only = slot ? onlyStore(slot) : cir::StoreOp();
  if (!only)
    return std::nullopt;
  return cir::tryResolveToConstant(only.getValue());
}
