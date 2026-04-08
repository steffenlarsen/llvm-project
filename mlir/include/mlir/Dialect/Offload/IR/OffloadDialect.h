//===- OffloadDialect.h - MLIR Offload Dialect ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the Offload dialect and its operations.
//
// The offload dialect preserves unified host+device GPU program structure
// through CIR-level IR, enabling cross-boundary analyses before the
// traditional host/device split performed by the SplitSingleSource pass.
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_DIALECT_OFFLOAD_IR_OFFLOADDIALECT_H
#define MLIR_DIALECT_OFFLOAD_IR_OFFLOADDIALECT_H

#include "mlir/Dialect/GPU/IR/GPUDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Dialect.h"
#include "mlir/Interfaces/FunctionInterfaces.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/FunctionInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

// Generated enum declarations (ExecSpace, MemSpace).
#include "mlir/Dialect/Offload/IR/OffloadOpsEnums.h.inc"

// Generated dialect class declaration.
#include "mlir/Dialect/Offload/IR/OffloadOpsDialect.h.inc"

// Generated attribute class declarations (LaunchBoundsAttr, etc.).
#define GET_ATTRDEF_CLASSES
#include "mlir/Dialect/Offload/IR/OffloadOpsAttributes.h.inc"

// Generated type class declarations (StreamType, EventType).
#define GET_TYPEDEF_CLASSES
#include "mlir/Dialect/Offload/IR/OffloadOpsTypes.h.inc"

// Generated operation class declarations.
#define GET_OP_CLASSES
#include "mlir/Dialect/Offload/IR/OffloadOps.h.inc"

#endif // MLIR_DIALECT_OFFLOAD_IR_OFFLOADDIALECT_H
