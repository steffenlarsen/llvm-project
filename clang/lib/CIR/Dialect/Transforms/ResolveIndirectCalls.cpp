//===- ResolveIndirectCalls.cpp - Make calls to a known callee direct -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An indirect call whose callee is provably one function is rewritten into a
// direct call to it. The symbol a pointer holds the address of is followed
// through:
//
//   * bitcasts between pointer types;
//   * `cir.get_global @sym` and `cir.const #cir.global_view<@sym>`;
//   * a load through a pointer to a constant global whose initializer is the
//     address of a symbol, which is what a HIP kernel handle is;
//   * a direct call to a function every return of which yields the address of
//     the same symbol, the shape of an accessor that hands out a function
//     pointer.
//
// A definition that may be replaced at link time proves nothing about the
// function finally called, so interposable functions and globals are not
// looked through. The rewrite only happens when the call's function type is the
// callee's own: a mismatch needs an adjustment the indirect call leaves to the
// backend.
//
//===----------------------------------------------------------------------===//

#include "PassDetail.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/SymbolTable.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"

using namespace mlir;
using namespace cir;

namespace mlir {
#define GEN_PASS_DEF_RESOLVEINDIRECTCALLS
#include "clang/CIR/Dialect/Passes.h.inc"
} // namespace mlir

namespace {

// How many accessor calls deep a pointer is followed.
constexpr unsigned kMaxDepth = 4;

// Finds the symbol -- a function or a global -- a pointer value provably holds
// the address of.
class SymbolResolver {
public:
  // The function `callee` is the address of, or null.
  cir::FuncOp resolveCallee(mlir::Value callee) {
    return mlir::dyn_cast_or_null<cir::FuncOp>(resolveAddress(callee, 0));
  }

private:
  mlir::Operation *resolveAddress(mlir::Value v, unsigned depth);
  mlir::Operation *resolveGlobalView(mlir::Operation *from,
                                     mlir::Attribute attr);
  mlir::Operation *resolveLoaded(cir::LoadOp load, unsigned depth);
  mlir::Operation *resolveReturned(cir::FuncOp fn, unsigned depth);

  mlir::SymbolTableCollection symbols;
  // Functions whose returned pointer has been resolved; null when it could
  // not be.
  llvm::DenseMap<mlir::Operation *, mlir::Operation *> returned;
  llvm::SmallPtrSet<mlir::Operation *, 4> inProgress;
};

static mlir::Value stripBitcasts(mlir::Value v) {
  while (auto cast = v.getDefiningOp<cir::CastOp>()) {
    if (cast.getKind() != cir::CastKind::bitcast)
      break;
    v = cast.getSrc();
  }
  return v;
}

mlir::Operation *SymbolResolver::resolveGlobalView(mlir::Operation *from,
                                                   mlir::Attribute attr) {
  auto view = mlir::dyn_cast_or_null<cir::GlobalViewAttr>(attr);
  if (!view || view.getIndices())
    return nullptr;
  return symbols.lookupNearestSymbolFrom(from, view.getSymbol());
}

// A pointer loaded from a constant global initialized with the address of a
// symbol -- a HIP kernel handle holding its stub, say -- is that address.
mlir::Operation *SymbolResolver::resolveLoaded(cir::LoadOp load,
                                               unsigned depth) {
  if (load.getIsVolatile())
    return nullptr;
  auto global = mlir::dyn_cast_or_null<cir::GlobalOp>(
      resolveAddress(load.getAddr(), depth));
  if (!global || !global.getConstant() || global.isDeclaration() ||
      cir::isInterposableLinkage(global.getLinkage()))
    return nullptr;
  return resolveGlobalView(global, global.getInitialValueAttr());
}

// The symbol every return of `fn` yields the address of.
mlir::Operation *SymbolResolver::resolveReturned(cir::FuncOp fn,
                                                 unsigned depth) {
  if (fn.isDeclaration() || cir::isInterposableLinkage(fn.getLinkage()))
    return nullptr;
  auto it = returned.find(fn);
  if (it != returned.end())
    return it->second;
  if (!inProgress.insert(fn).second)
    return nullptr;

  mlir::Operation *common = nullptr;
  bool unique = true;
  fn.walk([&](cir::ReturnOp ret) {
    if (!unique)
      return;
    mlir::Operation *here = ret.getNumOperands() == 1
                                ? resolveAddress(ret.getOperand(0), depth)
                                : nullptr;
    if (!here || (common && here != common)) {
      unique = false;
      return;
    }
    common = here;
  });

  inProgress.erase(fn);
  mlir::Operation *result = unique ? common : nullptr;
  returned[fn] = result;
  return result;
}

mlir::Operation *SymbolResolver::resolveAddress(mlir::Value v, unsigned depth) {
  if (!v || depth > kMaxDepth)
    return nullptr;
  v = stripBitcasts(v);
  mlir::Operation *def = v.getDefiningOp();
  if (!def)
    return nullptr;

  if (auto get = mlir::dyn_cast<cir::GetGlobalOp>(def))
    return get.getTls()
               ? nullptr
               : symbols.lookupNearestSymbolFrom(get, get.getNameAttr());
  if (auto constant = mlir::dyn_cast<cir::ConstantOp>(def))
    return resolveGlobalView(constant, constant.getValue());
  if (auto load = mlir::dyn_cast<cir::LoadOp>(def))
    return resolveLoaded(load, depth);
  if (auto call = mlir::dyn_cast<cir::CallOp>(def)) {
    if (call.isIndirect())
      return nullptr;
    cir::FuncOp accessor = call.resolveCalleeInTable(symbols);
    return accessor ? resolveReturned(accessor, depth + 1) : nullptr;
  }
  return nullptr;
}

struct ResolveIndirectCallsPass
    : public impl::ResolveIndirectCallsBase<ResolveIndirectCallsPass> {
  void runOnOperation() override;
};

void ResolveIndirectCallsPass::runOnOperation() {
  SymbolResolver resolver;
  llvm::SmallVector<std::pair<cir::CallOp, cir::FuncOp>> rewrites;
  getOperation()->walk([&](cir::CallOp call) {
    if (!call.isIndirect())
      return;
    cir::FuncOp callee = resolver.resolveCallee(call.getIndirectCall());
    if (!callee)
      return;
    auto ptrTy =
        mlir::dyn_cast<cir::PointerType>(call.getIndirectCall().getType());
    if (!ptrTy || ptrTy.getPointee() != callee.getFunctionType())
      return;
    rewrites.emplace_back(call, callee);
  });

  for (auto [call, callee] : rewrites) {
    mlir::OpBuilder builder(call);
    mlir::Type resultType =
        call.getNumResults() ? call.getResult().getType() : mlir::Type();
    auto direct = cir::CallOp::create(builder, call.getLoc(),
                                      mlir::FlatSymbolRefAttr::get(callee),
                                      resultType, call.getArgOperands());
    // Carry the call's attributes over: argument and result attributes
    // describe how each operand is passed, and the rest describe the call
    // itself, none of which changes with how the callee is named.
    for (mlir::NamedAttribute attr : call->getAttrs()) {
      if (attr.getName() == cir::CIRDialect::getCalleeAttrName() ||
          attr.getName() == cir::CIRDialect::getOperandSegmentSizesAttrName())
        continue;
      direct->setAttr(attr.getName(), attr.getValue());
    }
    call->replaceAllUsesWith(direct);
    call.erase();
  }

  if (rewrites.empty())
    markAllAnalysesPreserved();
}

} // namespace

std::unique_ptr<Pass> mlir::createResolveIndirectCallsPass() {
  return std::make_unique<ResolveIndirectCallsPass>();
}
