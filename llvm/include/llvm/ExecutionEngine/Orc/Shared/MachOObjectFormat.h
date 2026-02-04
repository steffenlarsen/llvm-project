//===---- MachOObjectFormat.h - MachO format details for ORC ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ORC-specific MachO object format details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SHARED_MACHOOBJECTFORMAT_H
#define LLVM_EXECUTIONENGINE_ORC_SHARED_MACHOOBJECTFORMAT_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace orc {

// FIXME: Move these to BinaryFormat?

// MachO section names.

LLVM_ABI extern const StringRef MachODataCommonSectionName;
LLVM_ABI extern const StringRef MachODataDataSectionName;
LLVM_ABI extern const StringRef MachOEHFrameSectionName;
LLVM_ABI extern const StringRef MachOCompactUnwindSectionName;
LLVM_ABI extern const StringRef MachOCStringSectionName;
LLVM_ABI extern const StringRef MachOModInitFuncSectionName;
LLVM_ABI extern const StringRef MachOObjCCatListSectionName;
LLVM_ABI extern const StringRef MachOObjCCatList2SectionName;
LLVM_ABI extern const StringRef MachOObjCClassListSectionName;
LLVM_ABI extern const StringRef MachOObjCClassNameSectionName;
LLVM_ABI extern const StringRef MachOObjCClassRefsSectionName;
LLVM_ABI extern const StringRef MachOObjCConstSectionName;
LLVM_ABI extern const StringRef MachOObjCDataSectionName;
LLVM_ABI extern const StringRef MachOObjCImageInfoSectionName;
LLVM_ABI extern const StringRef MachOObjCMethNameSectionName;
LLVM_ABI extern const StringRef MachOObjCMethTypeSectionName;
LLVM_ABI extern const StringRef MachOObjCNLCatListSectionName;
LLVM_ABI extern const StringRef MachOObjCNLClassListSectionName;
LLVM_ABI extern const StringRef MachOObjCProtoListSectionName;
LLVM_ABI extern const StringRef MachOObjCProtoRefsSectionName;
LLVM_ABI extern const StringRef MachOObjCSelRefsSectionName;
LLVM_ABI extern const StringRef MachOSwift5ProtoSectionName;
LLVM_ABI extern const StringRef MachOSwift5ProtosSectionName;
LLVM_ABI extern const StringRef MachOSwift5TypesSectionName;
LLVM_ABI extern const StringRef MachOSwift5TypeRefSectionName;
LLVM_ABI extern const StringRef MachOSwift5FieldMetadataSectionName;
LLVM_ABI extern const StringRef MachOSwift5EntrySectionName;
LLVM_ABI extern const StringRef MachOTextTextSectionName;
LLVM_ABI extern const StringRef MachOThreadBSSSectionName;
LLVM_ABI extern const StringRef MachOThreadDataSectionName;
LLVM_ABI extern const StringRef MachOThreadVarsSectionName;
LLVM_ABI extern const StringRef MachOUnwindInfoSectionName;

LLVM_ABI extern const StringRef MachOInitSectionNames[22];

LLVM_ABI bool isMachOInitializerSection(StringRef SegName, StringRef SecName);
LLVM_ABI bool isMachOInitializerSection(StringRef QualifiedName);

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_SHARED_MACHOOBJECTFORMAT_H
