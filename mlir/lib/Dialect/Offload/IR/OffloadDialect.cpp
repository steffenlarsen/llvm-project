//===- OffloadDialect.cpp - MLIR Offload Dialect implementation -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/Offload/IR/OffloadDialect.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/Interfaces/FunctionImplementation.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/TypeUtilities.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace mlir;
using namespace mlir::offload;

//===----------------------------------------------------------------------===//
// Generated dialect registration
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/Offload/IR/OffloadOpsDialect.cpp.inc"

//===----------------------------------------------------------------------===//
// Generated enum definitions (ExecSpace, MemSpace)
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/Offload/IR/OffloadOpsEnums.cpp.inc"

//===----------------------------------------------------------------------===//
// Generated attribute definitions
//===----------------------------------------------------------------------===//

#define GET_ATTRDEF_CLASSES
#include "mlir/Dialect/Offload/IR/OffloadOpsAttributes.cpp.inc"

//===----------------------------------------------------------------------===//
// Generated type definitions
//===----------------------------------------------------------------------===//

#define GET_TYPEDEF_CLASSES
#include "mlir/Dialect/Offload/IR/OffloadOpsTypes.cpp.inc"

//===----------------------------------------------------------------------===//
// Dialect initialization
//===----------------------------------------------------------------------===//

void OffloadDialect::initialize() {
  addAttributes<
#define GET_ATTRDEF_LIST
#include "mlir/Dialect/Offload/IR/OffloadOpsAttributes.cpp.inc"
      >();

  addTypes<
#define GET_TYPEDEF_LIST
#include "mlir/Dialect/Offload/IR/OffloadOpsTypes.cpp.inc"
      >();

  addOperations<
#define GET_OP_LIST
#include "mlir/Dialect/Offload/IR/OffloadOps.cpp.inc"
      >();
}

//===----------------------------------------------------------------------===//
// FuncOp — custom assembly format and builder
//===----------------------------------------------------------------------===//

void FuncOp::build(OpBuilder &builder, OperationState &state, StringRef name,
                   FunctionType type, ExecSpace execSpace,
                   ArrayRef<NamedAttribute> attrs) {
  state.addAttribute(SymbolTable::getSymbolAttrName(),
                     builder.getStringAttr(name));
  state.addAttribute(getFunctionTypeAttrName(state.name),
                     TypeAttr::get(type));
  state.addAttribute(getExecSpaceAttrName(state.name),
                     ExecSpaceAttr::get(builder.getContext(), execSpace));
  state.attributes.append(attrs.begin(), attrs.end());
  state.addRegion();
}

ParseResult FuncOp::parse(OpAsmParser &parser, OperationState &result) {
  // Parse the function name.
  StringAttr nameAttr;
  if (parser.parseSymbolName(nameAttr, SymbolTable::getSymbolAttrName(),
                             result.attributes))
    return failure();

  // Parse function signature.
  SmallVector<OpAsmParser::Argument> args;
  SmallVector<Type> resultTypes;
  SmallVector<DictionaryAttr> resultAttrs;
  bool isVariadic = false;
  if (function_interface_impl::parseFunctionSignatureWithArguments(
          parser, /*allowVariadic=*/false, args, isVariadic, resultTypes,
          resultAttrs))
    return failure();

  // Build the FunctionType from parsed signature.
  SmallVector<Type> argTypes;
  for (auto &arg : args)
    argTypes.push_back(arg.type);
  auto funcType = FunctionType::get(parser.getContext(), argTypes, resultTypes);
  result.addAttribute(getFunctionTypeAttrName(result.name),
                      TypeAttr::get(funcType));

  // Parse exec_space attribute (mandatory).
  if (parser.parseKeyword("exec_space") || parser.parseEqual())
    return failure();
  Attribute execSpaceAttr;
  if (parser.parseAttribute(execSpaceAttr,
                            getExecSpaceAttrName(result.name).getValue(),
                            result.attributes))
    return failure();

  // Parse optional named attributes as bare key = value pairs until we hit
  // the region opening brace.  Recognized keywords: launch_bounds,
  // reqd_work_group_size.  Any others are forwarded to the generic attr-dict
  // parser via attributes { }.
  while (true) {
    StringRef kw;
    if (failed(parser.parseOptionalKeyword(&kw,
            {"launch_bounds", "reqd_work_group_size", "attributes"})))
      break;

    if (kw == "attributes") {
      // Delegate the rest to the standard attr-dict parser.
      if (parser.parseOptionalAttrDict(result.attributes))
        return failure();
      break;
    }

    // For named keywords, parse "= <attr>".
    if (parser.parseEqual())
      return failure();
    Attribute attr;
    if (parser.parseAttribute(attr, kw, result.attributes))
      return failure();
  }

  // Attach arg/result attrs from signature parsing.
  call_interface_impl::addArgAndResultAttrs(
      parser.getBuilder(), result, args, resultAttrs,
      getArgAttrsAttrName(result.name), getResAttrsAttrName(result.name));

  // Parse the function body.
  auto *body = result.addRegion();
  if (parser.parseRegion(*body, args))
    return failure();

  return success();
}

void FuncOp::print(OpAsmPrinter &p) {
  p << ' ';
  p.printSymbolName(getName());
  FunctionType fnType = getFunctionType();
  function_interface_impl::printFunctionSignature(
      p, *this, fnType.getInputs(), /*isVariadic=*/false, fnType.getResults());
  p << " exec_space = ";
  p.printAttribute(getExecSpaceAttr());
  // Print optional attrs as bare "key = val" pairs (no "attributes {}" wrapper)
  // so the format round-trips through the custom parser above.
  SmallVector<StringRef, 5> elidedAttrs = {
      SymbolTable::getSymbolAttrName(),
      getFunctionTypeAttrName().getValue(),
      getExecSpaceAttrName().getValue(),
      getArgAttrsAttrName().getValue(),
      getResAttrsAttrName().getValue(),
  };
  for (NamedAttribute na : (*this)->getAttrs()) {
    StringRef name = na.getName().getValue();
    if (llvm::is_contained(elidedAttrs, name))
      continue;
    p << ' ' << name << " = ";
    p.printAttribute(na.getValue());
  }
  p << ' ';
  p.printRegion(getBody(), /*printEntryBlockArgs=*/false);
}

LogicalResult FuncOp::verify() {
  // A kernel (exec_space=global) must have no results.
  if (isKernel() && !getFunctionType().getResults().empty())
    return emitOpError("kernel function (exec_space=global) must return void");

  // Verify that gpu.* index/intrinsic ops do not appear in host-only functions.
  // (Device-context ops may not appear in exec_space=host bodies.)
  if (getExecSpace() == ExecSpace::host) {
    auto walkResult = getBody().walk([&](Operation *op) -> WalkResult {
      if (op->getDialect() &&
          op->getDialect()->getNamespace() == "gpu") {
        // gpu.* ops are only legal inside device/global/host_device functions.
        return op->emitError(
            "gpu dialect operation is not allowed inside a host-only "
            "offload.func (exec_space=host)");
      }
      return WalkResult::advance();
    });
    if (walkResult.wasInterrupted())
      return failure();
  }

  return success();
}

//===----------------------------------------------------------------------===//
// ReturnOp
//===----------------------------------------------------------------------===//

LogicalResult ReturnOp::verify() {
  // offload.return may appear inside nested regions (cir.if, cir.scope, etc.)
  // within an offload.func body, so walk up the parent chain.
  auto func = (*this)->getParentOfType<FuncOp>();
  if (!func)
    return emitOpError("must be inside an 'offload.func'");
  ArrayRef<Type> resultTypes = func.getFunctionType().getResults();

  if (getOperands().size() != resultTypes.size())
    return emitOpError("has ")
           << getOperands().size()
           << " operands, but enclosing function returns "
           << resultTypes.size();

  for (auto [idx, pair] :
       llvm::enumerate(llvm::zip(getOperands(), resultTypes))) {
    auto [operand, expectedTy] = pair;
    if (operand.getType() != expectedTy)
      return emitOpError("type of return operand ")
             << idx << " (" << operand.getType()
             << ") doesn't match function result type (" << expectedTy << ")";
  }
  return success();
}

//===----------------------------------------------------------------------===//
// KernelLaunchOp
//===----------------------------------------------------------------------===//

LogicalResult KernelLaunchOp::verify() {
  // Look up the callee symbol in the nearest symbol table.
  Operation *module = (*this)->getParentWithTrait<OpTrait::SymbolTable>();
  if (!module)
    return emitOpError("cannot find a surrounding symbol table");

  auto callee =
      dyn_cast_or_null<FuncOp>(SymbolTable::lookupSymbolIn(module, getCallee()));
  // If the callee is not an offload.func, it may have already been lowered to
  // a gpu.func by SplitSingleSourcePass (stream-aware launches survive that
  // pass and are handled by LowerHostRuntimePass instead).  Skip symbol
  // verification in that transitional state.
  if (!callee)
    return success();

  if (!callee.isKernel())
    return emitOpError("callee '")
           << getCallee()
           << "' must have exec_space = global (found exec_space = "
           << stringifyExecSpace(callee.getExecSpace()) << ")";

  // Verify argument types match the callee signature.
  ArrayRef<Type> paramTypes = callee.getFunctionType().getInputs();
  if (getArgs().size() != paramTypes.size())
    return emitOpError("has ")
           << getArgs().size()
           << " arguments, but callee '" << getCallee() << "' expects "
           << paramTypes.size();

  for (auto [idx, pair] :
       llvm::enumerate(llvm::zip(getArgs(), paramTypes))) {
    auto [arg, expectedTy] = pair;
    if (arg.getType() != expectedTy)
      return emitOpError("type of argument ")
             << idx << " (" << arg.getType()
             << ") doesn't match callee parameter type (" << expectedTy << ")";
  }
  return success();
}

//===----------------------------------------------------------------------===//
// SharedMemAllocOp
//===----------------------------------------------------------------------===//

LogicalResult SharedMemAllocOp::verify() {
  // Must appear inside an offload.func with device or global exec_space.
  auto funcOp = (*this)->getParentOfType<FuncOp>();
  if (!funcOp)
    return emitOpError("must appear inside an offload.func");

  if (!funcOp.isDeviceCode())
    return emitOpError(
        "must appear inside an offload.func with exec_space = device or global");

  return success();
}

//===----------------------------------------------------------------------===//
// MemcpyToSymbolOp
//===----------------------------------------------------------------------===//

LogicalResult MemcpyToSymbolOp::verify() {
  // Verify that the target symbol is an offload.global_var.
  Operation *module = (*this)->getParentWithTrait<OpTrait::SymbolTable>();
  if (!module)
    return emitOpError("cannot find a surrounding symbol table");

  auto globalVar = dyn_cast_or_null<GlobalVarOp>(
      SymbolTable::lookupSymbolIn(module, getSymbol()));
  if (!globalVar)
    return emitOpError("symbol '")
           << getSymbol()
           << "' does not name an offload.global_var in the same module";

  return success();
}

//===----------------------------------------------------------------------===//
// Generated op definitions
//===----------------------------------------------------------------------===//

#define GET_OP_CLASSES
#include "mlir/Dialect/Offload/IR/OffloadOps.cpp.inc"
