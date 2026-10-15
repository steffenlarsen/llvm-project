//===- CIRInlinerInterface.cpp - CIR dialect inliner interface ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/IR/CIRInlinerInterface.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "mlir/Dialect/Func/Extensions/InlinerExtension.h"
#include "mlir/Dialect/LLVMIR/Transforms/InlinerInterfaceImpl.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Transforms/InliningUtils.h"

using namespace mlir;
using namespace cir;

namespace {
/// Minimal DialectInlinerInterface for CIR, sufficient to exercise the generic
/// MLIR inliner on ClangIR.
///
/// Legality is still conservative because the generic inliner models a
/// callable's results through the terminator of its single-block body:
///   - launch markers stay calls: the offload pipeline rebuilds its kernel
///     binding table from `cu.kernel_name` stub calls, so inlining them away
///     would erase the launch sites;
///   - only single-block bodies ending in `cir.return` are accepted. Bodies
///     that end otherwise (e.g. a `cir.scope` whose paths all return) hit an
///     InliningUtils result-attribute assert, and multi-block bodies need
///     result plumbing CIR does not have yet.
/// The terminator handler rewrites `cir.return` into a `cir.br` to the
/// continuation block.
struct CIRInlinerInterface : public mlir::DialectInlinerInterface {
  using mlir::DialectInlinerInterface::DialectInlinerInterface;

  bool isLegalToInline(mlir::Operation *call, mlir::Operation *callable,
                       bool wouldBeCloned) const final {
    // A call to a device stub is a kernel launch, and has to stay a call for
    // the launch to be recognised as one.
    if (call && call->hasAttr("cu.kernel_name"))
      return false;
    auto func = mlir::dyn_cast<cir::FuncOp>(callable);
    if (!func || func.isDeclaration() || func->hasAttr("cu.kernel_name"))
      return false;
    if (std::optional<cir::InlineKind> kind = func.getInlineKind();
        kind && *kind == cir::InlineKind::NoInline)
      return false;
    mlir::Region &body = func.getBody();
    if (!body.hasOneBlock())
      return false;
    mlir::Block &block = body.front();
    if (block.empty() || !mlir::isa<cir::ReturnOp>(&block.back()))
      return false;
    // Before CFG flattening a return may sit inside a structured region -- the
    // `if (c) return x;` shape -- where, once inlined, it would return from the
    // caller instead. Labels are named per function, so a goto could resolve
    // to a label of the caller's.
    mlir::WalkResult unsupported = func.walk([&](mlir::Operation *op) {
      if (mlir::isa<cir::ReturnOp>(op) && op != &block.back())
        return mlir::WalkResult::interrupt();
      if (mlir::isa<cir::LabelOp, cir::GotoOp, cir::IndirectBrOp,
                    cir::VAStartOp>(op))
        return mlir::WalkResult::interrupt();
      return mlir::WalkResult::advance();
    });
    return !unsupported.wasInterrupted();
  }

  bool isLegalToInline(mlir::Region *dest, mlir::Region *src, bool wouldBeCloned,
                       mlir::IRMapping &valueMapping) const final {
    return true;
  }
  bool isLegalToInline(mlir::Operation *op, mlir::Region *dest, bool wouldBeCloned,
                       mlir::IRMapping &valueMapping) const final {
    return true;
  }
  void handleTerminator(mlir::Operation *op, mlir::Block *newDest) const final {
    auto ret = mlir::dyn_cast<cir::ReturnOp>(op);
    assert(ret && "expected cir.return");
    mlir::OpBuilder builder(op);
    cir::BrOp::create(builder, op->getLoc(), newDest, ret.getInput());
    op->erase();
  }
  // Static allocas of the inlined body move to the entry block of the caller,
  // where CIR keeps them: HoistAllocas would put them there before lowering
  // anyway, and the promotion that follows inlining only sees them there.
  void processInlinedCallBlocks(
      mlir::Operation *call,
      llvm::iterator_range<mlir::Region::iterator> inlinedBlocks) const final {
    auto caller = call->getParentOfType<cir::FuncOp>();
    if (!caller || caller.getBody().empty())
      return;
    mlir::Block *callerEntry = &caller.getBody().front();
    mlir::Block *inlinedEntry = &*inlinedBlocks.begin();
    if (callerEntry == inlinedEntry)
      return;
    llvm::SmallVector<cir::AllocaOp> allocas;
    for (cir::AllocaOp alloca : inlinedEntry->getOps<cir::AllocaOp>())
      if (!alloca.getDynAllocSize())
        allocas.push_back(alloca);
    for (cir::AllocaOp alloca : allocas)
      alloca->moveBefore(callerEntry, callerEntry->begin());
  }

  void handleTerminator(mlir::Operation *op,
                        mlir::ValueRange valuesToReplace) const final {
    auto ret = mlir::dyn_cast<cir::ReturnOp>(op);
    assert(ret && "expected cir.return");
    for (auto [oldVal, newVal] : llvm::zip(valuesToReplace, ret.getInput()))
      oldVal.replaceAllUsesWith(newVal);
  }
};
} // namespace

void cir::registerInlinerInterface(mlir::DialectRegistry &registry) {
  // Both the LLVM and func dialects promise DialectInlinerInterface; their
  // implementations are shipped as dialect extensions that must be registered
  // explicitly, and the generic inliner asserts on a promised-but-missing
  // interface as soon as a module mentions those dialects' callables.
  mlir::LLVM::registerInlinerInterface(registry);
  mlir::func::registerInlinerExtension(registry);
  registry.addExtension(
      +[](mlir::MLIRContext *ctx, cir::CIRDialect *dialect) {
        dialect->addInterfaces<CIRInlinerInterface>();
      });
}
