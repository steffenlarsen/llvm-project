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

  // Match (expr + C) where C == divisor - 1.
  //
  // The constant is rarely written pre-folded.  Everyone spells the ceiling
  // division `(n + BLOCK - 1) / BLOCK`, which reaches CIR as an add of BLOCK
  // followed by a subtract of 1 -- nothing canonicalises host CIR before the
  // offload passes run, so the two constants are still separate ops.  Matching
  // only a single `cir.add` of `divisor - 1` therefore recognises the form
  // almost nobody writes.  Accumulate the net constant across a short
  // add/subtract chain instead.
  constexpr int kMaxAddendSteps = 4;
  int64_t addend = 0;
  mlir::Value cursor = divOperand;
  for (int step = 0; step < kMaxAddendSteps; ++step) {
    mlir::Operation *op = cursor.getDefiningOp();
    if (!op || op->getNumOperands() != 2)
      break;

    llvm::StringRef opName = op->getName().getStringRef();
    const bool isAdd = opName == "cir.add";
    const bool isSub = opName == "cir.sub";
    if (!isAdd && !isSub)
      break;

    std::optional<int64_t> constant = tryResolveToConstant(op->getOperand(1));
    if (!constant)
      break;

    addend += isAdd ? *constant : -*constant;
    cursor = op->getOperand(0);

    // Strip casts on the running dividend.
    while (mlir::Operation *castOp = cursor.getDefiningOp()) {
      llvm::StringRef castName = castOp->getName().getStringRef();
      if ((castName.starts_with("cir.cast") || castName == "cir.unary") &&
          castOp->getNumOperands() == 1) {
        cursor = castOp->getOperand(0);
        continue;
      }
      break;
    }

    if (addend == *divisor - 1) {
      dividend = cursor;
      return divisor;
    }
  }

  return std::nullopt;
}

/// Strip the casts CIRGen inserts around integer subexpressions.
static mlir::Value stripIntCasts(mlir::Value v) {
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
  return v;
}

/// Return the value a single-block region yields, or null.
static mlir::Value getYieldedValue(mlir::Region &region) {
  if (!region.hasOneBlock())
    return {};
  auto yield = llvm::dyn_cast_or_null<cir::YieldOp>(
      region.front().getTerminator());
  if (!yield || yield.getNumOperands() != 1)
    return {};
  return yield.getOperand(0);
}

/// Recognise `min(expr, C)` and return `expr`, setting \p cap to C.
///
/// `std::min` and the MIN macro both reach CIR as a cir.ternary over a
/// cir.cmp, with one arm yielding the compared value and the other the
/// constant.  The arms reload from the same slot rather than reusing the
/// compared SSA value, so the arms are matched by traced origin.
static mlir::Value peelMinWithConstant(mlir::Value v,
                                       std::optional<int64_t> &cap) {
  auto ternary = stripIntCasts(v).getDefiningOp<cir::TernaryOp>();
  if (!ternary)
    return v;

  auto cmp = ternary.getCond().getDefiningOp<cir::CmpOp>();
  if (!cmp)
    return v;
  cir::CmpOpKind kind = cmp.getKind();
  bool lhsSmallerWhenTrue = kind == cir::CmpOpKind::lt ||
                            kind == cir::CmpOpKind::le;
  bool lhsLargerWhenTrue = kind == cir::CmpOpKind::gt ||
                           kind == cir::CmpOpKind::ge;
  if (!lhsSmallerWhenTrue && !lhsLargerWhenTrue)
    return v;

  mlir::Value trueVal = getYieldedValue(ternary.getTrueRegion());
  mlir::Value falseVal = getYieldedValue(ternary.getFalseRegion());
  if (!trueVal || !falseVal)
    return v;

  // The arm taken when the compared value is the smaller one is the one that
  // must yield that value; the other must yield the constant bound.
  mlir::Value boundedArm = lhsSmallerWhenTrue ? trueVal : falseVal;
  mlir::Value capArm = lhsSmallerWhenTrue ? falseVal : trueVal;

  std::optional<int64_t> capValue = cir::tryResolveToConstant(capArm);
  if (!capValue || *capValue <= 0)
    return v;
  // The comparison must be against that same constant, otherwise this is some
  // other conditional and not a clamp.
  std::optional<int64_t> cmpConst =
      cir::tryResolveToConstant(cmp.getRhs());
  if (!cmpConst || *cmpConst != *capValue)
    return v;

  // Confirm the surviving arm really is the compared value.
  cir::ValueTraceResult armOrigin = cir::traceValueOrigin(boundedArm);
  cir::ValueTraceResult lhsOrigin = cir::traceValueOrigin(cmp.getLhs());
  if (armOrigin.kind == cir::ValueTraceResult::Unknown ||
      !armOrigin.terminal || armOrigin.terminal != lhsOrigin.terminal)
    return v;

  cap = capValue;
  return cmp.getLhs();
}

std::optional<cir::GridDimRelation>
cir::matchGridDimRelation(mlir::Value gridDim,
                          cir::OffloadKernelLaunchOp launch) {
  // Step through the dim3 member load back to the constructor argument.
  ValueTraceResult dimTrace = traceValueOrigin(gridDim);
  mlir::Value expr = dimTrace.kind == ValueTraceResult::Dim3CtorArg
                         ? dimTrace.terminal
                         : gridDim;

  GridDimRelation rel;
  expr = peelMinWithConstant(expr, rel.cap);

  // ceilDiv by a constant, if present; otherwise the identity form.
  mlir::Value dividend;
  if (std::optional<int64_t> divisor = matchCeilDiv(expr, dividend)) {
    rel.divisor = *divisor;
    expr = dividend;
  }

  std::optional<unsigned> argIdx = traceToKernelArgIndex(expr, launch);
  if (!argIdx)
    return std::nullopt;
  rel.argIndex = *argIdx;
  return rel;
}

/// Collect the entry-block parameter indices \p v can be, following casts and
/// the block arguments a flattened conditional leaves behind.
static void collectReachableParams(mlir::Value v, mlir::Block *entry,
                                   llvm::SmallVectorImpl<unsigned> &out,
                                   unsigned depth = 0) {
  if (depth > 8 || !v)
    return;
  v = stripIntCasts(v);
  // CIRGen spills parameters to slots and reloads them, so the returned value
  // is normally a load rather than the block argument itself.
  if (cir::ValueTraceResult tr = cir::traceValueOrigin(v); tr.terminal)
    v = tr.terminal;
  if (auto arg = llvm::dyn_cast<mlir::BlockArgument>(v)) {
    if (arg.getOwner() == entry) {
      if (!llvm::is_contained(out, arg.getArgNumber()))
        out.push_back(arg.getArgNumber());
      return;
    }
    // A merge point: every incoming edge is a possible answer.
    mlir::Block *block = arg.getOwner();
    unsigned idx = arg.getArgNumber();
    for (mlir::Block *pred : block->getPredecessors()) {
      auto branch = llvm::dyn_cast_or_null<mlir::BranchOpInterface>(
          pred->getTerminator());
      if (!branch)
        continue;
      for (unsigned s = 0, e = pred->getNumSuccessors(); s < e; ++s) {
        if (pred->getSuccessor(s) != block)
          continue;
        mlir::SuccessorOperands ops = branch.getSuccessorOperands(s);
        if (idx < ops.size())
          collectReachableParams(ops[idx], entry, out, depth + 1);
      }
    }
  }
}

/// If \p ptr is a local slot with exactly one store, return the stored value.
///
/// Passing the slot's address to a call is tolerated, because that is exactly
/// how the value reaches the clamp being analysed here.  A callee could in
/// principle store through such a pointer, which would make the single store
/// found here an incomplete picture -- acceptable only because every caller of
/// this analysis re-tests the resulting relation at run time.
static mlir::Value uniqueStoredValue(mlir::Value ptr) {
  auto alloca = stripIntCasts(ptr).getDefiningOp<cir::AllocaOp>();
  if (!alloca)
    return {};
  cir::StoreOp unique;
  for (mlir::Operation *user : alloca.getResult().getUsers()) {
    if (auto store = llvm::dyn_cast<cir::StoreOp>(user)) {
      if (store.getValue() == alloca.getResult())
        return {}; // the slot's address escapes as a value
      if (unique)
        return {};
      unique = store;
      continue;
    }
    if (llvm::isa<cir::LoadOp, cir::CallOp>(user))
      continue;
    return {};
  }
  return unique ? unique.getValue() : mlir::Value{};
}

/// Look through `load(call(...))` where the callee just hands back one of its
/// pointer parameters, and push what those parameters point at.
///
/// This is how `std::min` reaches these passes.  They run before any inlining,
/// so the clamp is still an out-of-line call returning a reference rather than
/// the conditional the source suggests -- the shape a macro-based MIN would
/// have produced directly.
static void pushThroughReferenceReturningCall(
    mlir::Value v, llvm::function_ref<void(mlir::Value)> push) {
  auto load = v.getDefiningOp<cir::LoadOp>();
  if (!load)
    return;
  auto call = stripIntCasts(load.getAddr()).getDefiningOp<cir::CallOp>();
  if (!call || call.isIndirect() || call.getNumResults() != 1)
    return;
  auto callee = mlir::SymbolTable::lookupNearestSymbolFrom<cir::FuncOp>(
      call, call.getCalleeAttr());
  if (!callee || callee.getBody().empty())
    return;

  mlir::Block *entry = &callee.getBody().front();
  llvm::SmallVector<unsigned> params;
  callee.getBody().walk([&](cir::ReturnOp ret) {
    if (ret.getNumOperands() == 1)
      collectReachableParams(ret.getOperand(0), entry, params);
  });

  for (unsigned idx : params) {
    if (idx >= call.getArgOperands().size())
      continue;
    if (mlir::Value stored = uniqueStoredValue(call.getArgOperands()[idx]))
      push(stored);
  }
}

std::optional<cir::GridDimRelation>
cir::findGridDimCandidate(mlir::Value gridDim,
                          cir::OffloadKernelLaunchOp launch) {
  // traceValueOrigin keeps walking past the dim3 constructor argument, so the
  // final kind describes wherever it stopped rather than the dim3 step.  Start
  // from whatever it reached; that is already past the member-load chain.
  ValueTraceResult dimTrace = traceValueOrigin(gridDim);
  mlir::Value start = dimTrace.terminal ? dimTrace.terminal : gridDim;

  // Breadth-first over the ways a value can reach the grid extent.  A clamp
  // survives CIRFlattenCFG as a block argument merging the clamped and
  // unclamped values, so both incoming edges have to be followed.
  struct Item {
    mlir::Value value;
    int64_t divisor;
  };
  llvm::SmallVector<Item> worklist{{start, 1}};
  llvm::SmallPtrSet<void *, 8> visited;

  while (!worklist.empty()) {
    Item item = worklist.pop_back_val();
    mlir::Value v = stripIntCasts(item.value);
    if (!v || !visited.insert(v.getAsOpaquePointer()).second)
      continue;

    if (std::optional<unsigned> argIdx = traceToKernelArgIndex(v, launch)) {
      GridDimRelation rel;
      rel.argIndex = *argIdx;
      rel.divisor = item.divisor;
      return rel;
    }

    // A ceiling division contributes its divisor to the bound.
    mlir::Value dividend;
    if (std::optional<int64_t> d = matchCeilDiv(v, dividend)) {
      worklist.push_back({dividend, item.divisor * *d});
      continue;
    }

    // The grid extent is usually computed into a local first --
    // `const int num_blocks = (k + N - 1) / N;` -- so the value here is a
    // reload of that slot rather than the division itself.  Hop back to what
    // was stored, but only when the slot is written exactly once and never
    // escapes, so the loaded value provably is the stored one.
    if (auto load = llvm::dyn_cast_or_null<cir::LoadOp>(v.getDefiningOp())) {
      if (auto slot = load.getAddr().getDefiningOp<cir::AllocaOp>()) {
        cir::StoreOp singleStore;
        bool usable = true;
        for (mlir::Operation *user : slot.getResult().getUsers()) {
          if (auto store = llvm::dyn_cast<cir::StoreOp>(user)) {
            if (singleStore || store.getAddr() != slot.getResult()) {
              usable = false;
              break;
            }
            singleStore = store;
            continue;
          }
          if (llvm::isa<cir::LoadOp>(user))
            continue;
          // Anything else could write through the slot.
          usable = false;
          break;
        }
        if (usable && singleStore)
          worklist.push_back({singleStore.getValue(), item.divisor});
      }
      continue;
    }

    if (auto blockArg = llvm::dyn_cast<mlir::BlockArgument>(v)) {
      mlir::Block *block = blockArg.getOwner();
      unsigned idx = blockArg.getArgNumber();
      for (mlir::Block *pred : block->getPredecessors()) {
        auto branch = llvm::dyn_cast_or_null<mlir::BranchOpInterface>(
            pred->getTerminator());
        if (!branch)
          continue;
        for (unsigned s = 0, e = pred->getNumSuccessors(); s < e; ++s) {
          if (pred->getSuccessor(s) != block)
            continue;
          mlir::SuccessorOperands ops = branch.getSuccessorOperands(s);
          if (idx < ops.size())
            worklist.push_back({ops[idx], item.divisor});
        }
      }
      continue;
    }

    pushThroughReferenceReturningCall(
        v, [&](mlir::Value nv) { worklist.push_back({nv, item.divisor}); });

    // A clamp written without a branch still shows up as a ternary.
    if (auto ternary = v.getDefiningOp<cir::TernaryOp>()) {
      if (mlir::Value t = getYieldedValue(ternary.getTrueRegion()))
        worklist.push_back({t, item.divisor});
      if (mlir::Value f = getYieldedValue(ternary.getFalseRegion()))
        worklist.push_back({f, item.divisor});
    }
  }
  return std::nullopt;
}

/// Reduce a trace terminal to a form two traces of the same value can share.
///
/// traceValueOrigin stops at an alloca, so one reference to a parameter can
/// come back as the entry BlockArg while another comes back as the slot it was
/// spilled into -- the same value under two names, which then compare unequal.
/// Look through a slot that is written exactly once and never escapes, where
/// the loaded value provably is the stored one.
static mlir::Value canonicalizeSpilledTerminal(mlir::Value terminal) {
  auto slot = terminal.getDefiningOp<cir::AllocaOp>();
  if (!slot)
    return terminal;

  cir::StoreOp singleStore;
  for (mlir::Operation *user : slot.getResult().getUsers()) {
    if (auto store = llvm::dyn_cast<cir::StoreOp>(user)) {
      if (singleStore || store.getAddr() != slot.getResult())
        return terminal;
      singleStore = store;
      continue;
    }
    if (llvm::isa<cir::LoadOp>(user))
      continue;
    return terminal;
  }
  if (!singleStore)
    return terminal;

  cir::ValueTraceResult inner = cir::traceValueOrigin(singleStore.getValue());
  return inner.terminal ? inner.terminal : singleStore.getValue();
}

std::optional<unsigned> cir::traceToKernelArgIndex(
    mlir::Value v, cir::OffloadKernelLaunchOp launch) {
  auto result = traceValueOrigin(v);
  if (result.kind == ValueTraceResult::Unknown)
    return std::nullopt;
  if (!result.terminal)
    return std::nullopt;

  mlir::Value lhs = canonicalizeSpilledTerminal(result.terminal);

  mlir::OperandRange kernelArgs = launch.getKernelOperands();
  for (unsigned i = 0; i < kernelArgs.size(); ++i) {
    auto argResult = traceValueOrigin(kernelArgs[i]);
    if (!argResult.terminal)
      continue;
    if (lhs == canonicalizeSpilledTerminal(argResult.terminal))
      return i;
  }
  return std::nullopt;
}
