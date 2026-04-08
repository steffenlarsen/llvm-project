//===- CIRTransformUtils.cpp - Shared helpers for CIR transforms ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/Transforms/CIRTransformUtils.h"

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "mlir/IR/Matchers.h"

#include "llvm/ADT/DepthFirstIterator.h"

void cir::collectUnreachable(mlir::Operation *parent,
                             llvm::SmallVectorImpl<mlir::Operation *> &ops) {
  // For every region under `parent`, find the blocks unreachable from the
  // entry via a forward CFG traversal and collect their ops.
  llvm::df_iterator_default_set<mlir::Block *, 16> reachable;
  parent->walk([&](mlir::Region *region) {
    // Empty regions have no blocks; single-block regions have only the
    // entry, which is trivially reachable. Either way, nothing to collect.
    if (region->empty() || region->hasOneBlock())
      return;

    // We clear this for each region as we walk the parent because each block
    // is only in one region, so the reachable blocks from previously visited
    // regions aren't needed.
    reachable.clear();

    // The depth_first_ext range iterator internally adds each block to the
    // reachable set as it visits it, so while this loop looks like it doesn't
    // do anything, it's actually populating the set of reachable blocks in
    // this region.
    for (mlir::Block *blk : llvm::depth_first_ext(&region->front(), reachable))
      (void)blk;

    // Collect the unreachable blocks.
    for (mlir::Block &blk : *region) {
      if (reachable.contains(&blk))
        continue;
      for (mlir::Operation &op : blk)
        ops.push_back(&op);
    }
  });
}

mlir::Block *cir::replaceCallWithTryCall(cir::CallOp callOp,
                                         mlir::Block *unwindDest,
                                         mlir::Location loc,
                                         mlir::RewriterBase &rewriter) {
  mlir::Block *callBlock = callOp->getBlock();

  assert(!callOp.getNothrow() && "call is not expected to throw");

  // Split the block after the call - remaining ops become the normal
  // destination.
  mlir::Block *normalDest =
      rewriter.splitBlock(callBlock, std::next(callOp->getIterator()));

  // Build the try_call to replace the original call.
  rewriter.setInsertionPoint(callOp);
  cir::TryCallOp tryCallOp;
  if (callOp.isIndirect()) {
    mlir::Value indTarget = callOp.getIndirectCall();
    auto ptrTy = mlir::cast<cir::PointerType>(indTarget.getType());
    auto resTy = mlir::cast<cir::FuncType>(ptrTy.getPointee());
    tryCallOp =
        cir::TryCallOp::create(rewriter, loc, indTarget, resTy, normalDest,
                               unwindDest, callOp.getArgOperands());
  } else {
    mlir::Type resType = callOp->getNumResults() > 0
                             ? callOp->getResult(0).getType()
                             : mlir::Type();
    tryCallOp =
        cir::TryCallOp::create(rewriter, loc, callOp.getCalleeAttr(), resType,
                               normalDest, unwindDest, callOp.getArgOperands());
  }

  // Copy all attributes from the original call except those already set by
  // TryCallOp::create or that are operation-specific and should not be copied.
  llvm::StringRef excludedAttrs[] = {
      cir::CIRDialect::getCalleeAttrName(), // Set by create()
      cir::CIRDialect::getOperandSegmentSizesAttrName(),
  };
  for (mlir::NamedAttribute attr : callOp->getAttrs()) {
    if (llvm::is_contained(excludedAttrs, attr.getName()))
      continue;
    assert(!llvm::is_contained(
               {
                   cir::CIRDialect::getNoThrowAttrName(),
                   cir::CIRDialect::getNoUnwindAttrName(),
               },
               attr.getName()) &&
           "unexpected attribute on converted call");
    tryCallOp->setAttr(attr.getName(), attr.getValue());
  }

  // Replace uses of the call result with the try_call result. Use the
  // rewriter API so any listener (e.g. the pattern rewriter in
  // FlattenCFG) is notified of the in-place modifications to each user.
  if (callOp->getNumResults() > 0)
    rewriter.replaceAllUsesWith(callOp->getResult(0), tryCallOp.getResult());

  rewriter.eraseOp(callOp);
  return normalDest;
}

mlir::Block *cir::replaceThrowWithTryThrow(cir::ThrowOp throwOp,
                                           mlir::Block *unwindDest,
                                           mlir::Location loc,
                                           mlir::RewriterBase &rewriter) {
  // The throw never returns, so the try_throw's normal destination is
  // literally unreachable. Place it at the end of the parent function
  // rather than splitting it out of the throw's block in the middle of
  // the normal control flow.
  auto funcOp = throwOp->getParentOfType<cir::FuncOp>();
  assert(funcOp && "throw must be inside a function");
  mlir::Region &body = funcOp.getBody();

  mlir::Block *normalDest;
  {
    mlir::OpBuilder::InsertionGuard guard(rewriter);
    normalDest = rewriter.createBlock(&body, body.end());
    cir::UnreachableOp::create(rewriter, loc);
  }

  // Build the try_throw to replace the original throw.
  rewriter.setInsertionPoint(throwOp);
  auto tryThrowOp = cir::TryThrowOp::create(
      rewriter, loc, throwOp.getExceptionPtr(), throwOp.getTypeInfoAttr(),
      throwOp.getDtorAttr(), normalDest, unwindDest);

  // Copy any extra attributes from the original throw. The type_info and
  // dtor attributes are already set by TryThrowOp::create above.
  llvm::StringRef excludedAttrs[] = {
      "type_info",
      "dtor",
  };
  for (mlir::NamedAttribute attr : throwOp->getAttrs()) {
    if (llvm::is_contained(excludedAttrs, attr.getName()))
      continue;
    tryThrowOp->setAttr(attr.getName(), attr.getValue());
  }

  // Erase the throw along with any operations that followed it in its
  // parent block (typically a cir.unreachable left over from CIR codegen).
  // They must be removed because try_throw is a terminator and a block
  // can have only one terminator.
  mlir::Block *throwBlock = throwOp->getBlock();
  while (&throwBlock->back() != tryThrowOp)
    rewriter.eraseOp(&throwBlock->back());

  return normalDest;
}

//===----------------------------------------------------------------------===//
// CIR Value Tracer
//===----------------------------------------------------------------------===//

/// Try to extract an integer from a CIR or MLIR integer attribute.
static std::optional<int64_t> tryExtractInt(mlir::Attribute attr) {
  if (auto intAttr = mlir::dyn_cast<mlir::IntegerAttr>(attr))
    return intAttr.getInt();
  if (auto cirInt = mlir::dyn_cast<cir::IntAttr>(attr))
    return cirInt.getValue().getSExtValue();
  return std::nullopt;
}

cir::ValueTraceResult cir::traceValueOrigin(mlir::Value v, unsigned maxDepth) {
  ValueTraceResult result;
  for (unsigned depth = 0; depth < maxDepth; ++depth) {
    // Check for compile-time constant via MLIR matcher.
    mlir::APInt directConst;
    if (mlir::matchPattern(v, mlir::m_ConstantInt(&directConst))) {
      result.kind = ValueTraceResult::Constant;
      result.terminal = v;
      result.constantValue = directConst.getZExtValue();
      return result;
    }

    mlir::Operation *defOp = v.getDefiningOp();

    // Block argument — function parameter.
    if (!defOp) {
      if (mlir::isa<mlir::BlockArgument>(v)) {
        result.kind = ValueTraceResult::BlockArg;
        result.terminal = v;
        return result;
      }
      return result;
    }

    llvm::StringRef opName = defOp->getName().getStringRef();

    // CIR constant.
    if (opName == "cir.const" && defOp->getNumResults() == 1) {
      mlir::Attribute valAttr = defOp->getAttr("value");
      if (!valAttr) {
        for (mlir::NamedAttribute na : defOp->getAttrDictionary()) {
          valAttr = na.getValue();
          break;
        }
      }
      if (valAttr) {
        if (auto cv = tryExtractInt(valAttr)) {
          result.kind = ValueTraceResult::Constant;
          result.terminal = v;
          result.constantValue = cv;
          return result;
        }
      }
      return result;
    }

    // Transparent casts — peel and continue.
    if ((opName == "arith.index_castui" ||
         opName == "builtin.unrealized_conversion_cast" ||
         opName.starts_with("cir.cast") || opName == "cir.unary") &&
        defOp->getNumOperands() == 1) {
      v = defOp->getOperand(0);
      continue;
    }

    // Alloca — terminal.
    if (opName == "cir.alloca") {
      result.kind = ValueTraceResult::Alloca;
      result.terminal = v;
      return result;
    }

    // Load — follow the address.
    if (opName != "cir.load" || defOp->getNumOperands() < 1)
      return result;

    mlir::Value ptrVal = defOp->getOperand(0);
    mlir::Operation *ptrDefOp = ptrVal.getDefiningOp();
    if (!ptrDefOp)
      return result;

    // Simple alloca path: load(alloca) — find unique store.
    if (ptrDefOp->getName().getStringRef() == "cir.alloca") {
      mlir::Operation *uniqueStore = nullptr;
      for (mlir::OpOperand &use : ptrVal.getUses()) {
        mlir::Operation *userOp = use.getOwner();
        if (userOp->getName().getStringRef() != "cir.store" ||
            userOp->getNumOperands() < 2 || userOp->getOperand(1) != ptrVal)
          continue;
        if (uniqueStore)
          return result;
        uniqueStore = userOp;
      }
      if (uniqueStore) {
        v = uniqueStore->getOperand(0);
        continue;
      }
      result.kind = ValueTraceResult::Alloca;
      result.terminal = ptrVal;
      return result;
    }

    // dim3 struct path: load(get_member(alloca/copy)).
    if (ptrDefOp->getName().getStringRef() != "cir.get_member")
      return result;

    mlir::IntegerAttr idxAttr =
        ptrDefOp->getAttrOfType<mlir::IntegerAttr>("index_attr");
    if (!idxAttr)
      return result;
    int64_t fieldIndex = idxAttr.getInt();

    if (ptrDefOp->getNumOperands() < 1)
      return result;
    mlir::Value basePtr = ptrDefOp->getOperand(0);
    mlir::Operation *baseDefOp = basePtr.getDefiningOp();
    if (!baseDefOp || baseDefOp->getName().getStringRef() != "cir.alloca")
      return result;

    mlir::Value dim3Alloca = basePtr;

    // Follow cir.copy if basePtr is a temporary.
    for (mlir::OpOperand &use : basePtr.getUses()) {
      mlir::Operation *userOp = use.getOwner();
      if (userOp->getName().getStringRef() != "cir.copy" ||
          userOp->getNumOperands() < 2 || userOp->getOperand(0) != basePtr)
        continue;
      dim3Alloca = userOp->getOperand(1);
      break;
    }

    // Check for explicit field stores overriding the constructor.
    {
      mlir::Operation *fieldStore = nullptr;
      bool ambiguous = false;
      for (mlir::OpOperand &use : dim3Alloca.getUses()) {
        mlir::Operation *userOp = use.getOwner();
        if (userOp->getName().getStringRef() != "cir.get_member")
          continue;
        mlir::IntegerAttr userIdx =
            userOp->getAttrOfType<mlir::IntegerAttr>("index_attr");
        if (!userIdx || userIdx.getInt() != fieldIndex)
          continue;
        for (mlir::OpOperand &gmUse : userOp->getResult(0).getUses()) {
          mlir::Operation *gmUser = gmUse.getOwner();
          if (gmUser->getName().getStringRef() != "cir.store" ||
              gmUser->getNumOperands() < 2 ||
              gmUser->getOperand(1) != userOp->getResult(0))
            continue;
          if (fieldStore) { ambiguous = true; break; }
          fieldStore = gmUser;
        }
        if (ambiguous) break;
      }
      if (ambiguous) return result;
      if (fieldStore) { v = fieldStore->getOperand(0); continue; }
    }

    // Find the dim3 constructor call.
    mlir::Operation *allocaOp = dim3Alloca.getDefiningOp();
    if (!allocaOp) return result;
    mlir::Region *funcRegion = allocaOp->getParentRegion();
    if (!funcRegion) return result;

    bool found = false;
    for (mlir::Block &b : *funcRegion) {
      for (mlir::Operation &op : b) {
        if (op.getName().getStringRef() != "cir.call") continue;
        mlir::FlatSymbolRefAttr calleeAttr;
        for (mlir::NamedAttribute na : op.getAttrDictionary()) {
          if (auto sym = mlir::dyn_cast<mlir::FlatSymbolRefAttr>(na.getValue())) {
            calleeAttr = sym; break;
          }
        }
        if (!calleeAttr) continue;
        llvm::StringRef callee = calleeAttr.getValue();
        if (!callee.contains("dim3") ||
            (!callee.contains("C1Ejjj") && !callee.contains("C2Ejjj")))
          continue;
        if (op.getNumOperands() < 4 || op.getOperand(0) != dim3Alloca)
          continue;
        unsigned argIdx = static_cast<unsigned>(fieldIndex) + 1;
        if (argIdx >= op.getNumOperands()) return result;
        result.kind = ValueTraceResult::Dim3CtorArg;
        result.terminal = op.getOperand(argIdx);
        result.dim3FieldIndex = static_cast<unsigned>(fieldIndex);
        v = op.getOperand(argIdx);
        found = true;
        break;
      }
      if (found) break;
    }
    if (!found) return result;
    // Continue tracing the constructor argument.
  }
  return result;
}

std::optional<int64_t> cir::tryResolveToConstant(mlir::Value v) {
  auto result = traceValueOrigin(v);
  if (result.kind == ValueTraceResult::Constant)
    return result.constantValue;
  return std::nullopt;
}

std::optional<int64_t> cir::matchCeilDiv(mlir::Value v,
                                          mlir::Value &dividend) {
  // Strip casts.
  while (auto *defOp = v.getDefiningOp()) {
    llvm::StringRef name = defOp->getName().getStringRef();
    if ((name.starts_with("cir.cast") || name == "cir.unary" ||
         name == "arith.index_castui" ||
         name == "builtin.unrealized_conversion_cast") &&
        defOp->getNumOperands() == 1) {
      v = defOp->getOperand(0);
      continue;
    }
    break;
  }

  auto *defOp = v.getDefiningOp();
  if (!defOp || defOp->getNumOperands() != 2)
    return std::nullopt;
  if (defOp->getName().getStringRef() != "cir.div")
    return std::nullopt;

  auto divisor = tryResolveToConstant(defOp->getOperand(1));
  if (!divisor || *divisor <= 0)
    return std::nullopt;

  mlir::Value divOperand = defOp->getOperand(0);
  // Strip casts on dividend.
  while (auto *dOp = divOperand.getDefiningOp()) {
    llvm::StringRef dn = dOp->getName().getStringRef();
    if ((dn.starts_with("cir.cast") || dn == "cir.unary") &&
        dOp->getNumOperands() == 1) {
      divOperand = dOp->getOperand(0);
      continue;
    }
    break;
  }

  // Match (expr + C-1).
  if (auto *addOp = divOperand.getDefiningOp()) {
    if (addOp->getName().getStringRef() == "cir.add" &&
        addOp->getNumOperands() == 2) {
      auto addConst = tryResolveToConstant(addOp->getOperand(1));
      if (addConst && *addConst == *divisor - 1) {
        dividend = addOp->getOperand(0);
        return divisor;
      }
    }
  }

  return std::nullopt;
}

std::optional<unsigned> cir::traceToKernelArgIndex(
    mlir::Value v, cir::OffloadKernelLaunchOp launch) {
  auto result = traceValueOrigin(v);
  if (result.kind == ValueTraceResult::Unknown)
    return std::nullopt;

  mlir::OperandRange kernelArgs = launch.getKernelOperands();
  for (unsigned i = 0; i < kernelArgs.size(); ++i) {
    auto argResult = traceValueOrigin(kernelArgs[i]);
    if (result.terminal && argResult.terminal &&
        result.terminal == argResult.terminal)
      return i;
  }
  return std::nullopt;
}
