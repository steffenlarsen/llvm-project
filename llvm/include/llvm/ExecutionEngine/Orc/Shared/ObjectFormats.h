//===------ ObjectFormats.h - Object format details for ORC -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ORC-specific object format details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SHARED_OBJECTFORMATS_H
#define LLVM_EXECUTIONENGINE_ORC_SHARED_OBJECTFORMATS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/Orc/Shared/MachOObjectFormat.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace orc {

// ELF section names.
LLVM_ABI extern const StringRef ELFEHFrameSectionName;

LLVM_ABI extern const StringRef ELFInitArrayFuncSectionName;
LLVM_ABI extern const StringRef ELFInitFuncSectionName;
LLVM_ABI extern const StringRef ELFFiniArrayFuncSectionName;
LLVM_ABI extern const StringRef ELFFiniFuncSectionName;
LLVM_ABI extern const StringRef ELFCtorArrayFuncSectionName;
LLVM_ABI extern const StringRef ELFDtorArrayFuncSectionName;

LLVM_ABI extern const StringRef ELFInitSectionNames[3];
LLVM_ABI extern const StringRef ELFFiniSectionNames[3];

LLVM_ABI extern const StringRef ELFThreadBSSSectionName;
LLVM_ABI extern const StringRef ELFThreadDataSectionName;

LLVM_ABI bool isELFInitializerSection(StringRef SecName);
LLVM_ABI bool isELFFinalizerSection(StringRef SecName);

LLVM_ABI bool isCOFFInitializerSection(StringRef Name);

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_SHARED_OBJECTFORMATS_H
