//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the CIR dialect enum base classes
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_CIR_DIALECT_IR_CIRENUMATTR_TD
#define CLANG_CIR_DIALECT_IR_CIRENUMATTR_TD

include "mlir/IR/EnumAttr.td"
include "clang/CIR/Dialect/IR/CIRDialect.td"

class CIR_I32EnumAttr<string name, string summary, list<I32EnumAttrCase> cases>
    : I32EnumAttr<name, summary, cases> {
  let cppNamespace = "::cir";
}

class CIR_I64EnumAttr<string name, string summary, list<I64EnumAttrCase> cases>
    : I64EnumAttr<name, summary, cases> {
  let cppNamespace = "::cir";
}

class CIR_I32BitEnumAttr<string name, string summary,
                         list<BitEnumCaseBase> cases>
    : I32BitEnumAttr<name, summary, cases> {
  let cppNamespace = "::cir";
}

class CIR_EnumAttr<EnumAttrInfo info, string name = "", list<Trait> traits = []>
    : EnumAttr<CIR_Dialect, info, name, traits> {
  let assemblyFormat = "`<` $value `>`";
}

class CIR_DefaultValuedEnumParameter<EnumAttrInfo info, string value = "">
    : EnumParameter<info> {
  let defaultValue = value;
}

def CIR_LangAddressSpace : CIR_I32EnumAttr<
  "LangAddressSpace", "language address space kind", [
  I32EnumAttrCase<"Default", 0, "default">,
  I32EnumAttrCase<"OffloadPrivate", 1, "offload_private">,
  I32EnumAttrCase<"OffloadLocal", 2, "offload_local">,
  I32EnumAttrCase<"OffloadGlobal", 3, "offload_global">,
  I32EnumAttrCase<"OffloadConstant", 4, "offload_constant">,
  I32EnumAttrCase<"OffloadGeneric", 5, "offload_generic">,
  I32EnumAttrCase<"OffloadGlobalDevice", 6, "offload_global_device">,
  I32EnumAttrCase<"OffloadGlobalHost", 7, "offload_global_host">
]> {
  let description = [{
    Enumerates language-specific address spaces used by CIR. These represent
    semantic qualifiers from source languages (e.g., CUDA `__shared__`,
    OpenCL `__local`) before target lowering.
  }];

  let genSpecializedAttr = 0;
}

//===----------------------------------------------------------------------===//
// Offload enums
//===----------------------------------------------------------------------===//

def CIR_OffloadDimension : CIR_I32EnumAttr<
  "OffloadDimension", "offload dimension", [
  I32EnumAttrCase<"X", 0, "x">,
  I32EnumAttrCase<"Y", 1, "y">,
  I32EnumAttrCase<"Z", 2, "z">
]> {
  let genSpecializedAttr = 0;
}

def CIR_OffloadDimensionAttr
    : CIR_EnumAttr<CIR_OffloadDimension, "offload_dim">;

def CIR_OffloadReduceKind : CIR_I32EnumAttr<
  "OffloadReduceKind", "offload reduction operation", [
  I32EnumAttrCase<"Add", 0, "add">,
  I32EnumAttrCase<"Mul", 1, "mul">,
  I32EnumAttrCase<"MinUI", 2, "minui">,
  I32EnumAttrCase<"MinSI", 3, "minsi">,
  I32EnumAttrCase<"MaxUI", 4, "maxui">,
  I32EnumAttrCase<"MaxSI", 5, "maxsi">,
  I32EnumAttrCase<"And", 6, "and">,
  I32EnumAttrCase<"Or", 7, "or">,
  I32EnumAttrCase<"Xor", 8, "xor">
]> {
  let genSpecializedAttr = 0;
}

def CIR_OffloadReduceKindAttr
    : CIR_EnumAttr<CIR_OffloadReduceKind, "offload_reduce_kind">;

def CIR_OffloadBroadcastKind : CIR_I32EnumAttr<
  "OffloadBroadcastKind", "offload broadcast kind", [
  I32EnumAttrCase<"FirstActiveLane", 0, "first_active_lane">,
  I32EnumAttrCase<"SpecificLane", 1, "specific_lane">
]> {
  let genSpecializedAttr = 0;
}

def CIR_OffloadBroadcastKindAttr
    : CIR_EnumAttr<CIR_OffloadBroadcastKind, "offload_broadcast_kind">;

def CIR_OffloadAllocKind : CIR_I32EnumAttr<
  "OffloadAllocKind", "offload allocation kind", [
  I32EnumAttrCase<"Device",  0, "device">,
  I32EnumAttrCase<"Managed", 1, "managed">,
  I32EnumAttrCase<"Host",    2, "host">
]> {
  let genSpecializedAttr = 0;
}

def CIR_OffloadAllocKindAttr
    : CIR_EnumAttr<CIR_OffloadAllocKind, "offload_alloc_kind">;

#endif // CLANG_CIR_DIALECT_IR_CIRENUMATTR_TD
