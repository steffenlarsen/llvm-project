//===---------- ObjectFormats.cpp - Object format details for ORC ---------===//
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

#include "llvm/ExecutionEngine/Orc/Shared/ObjectFormats.h"
#include "llvm/ADT/STLExtras.h"

namespace llvm {
namespace orc {

const StringRef ELFEHFrameSectionName = ".eh_frame";

const StringRef ELFInitArrayFuncSectionName = ".init_array";
const StringRef ELFInitFuncSectionName = ".init";
const StringRef ELFFiniArrayFuncSectionName = ".fini_array";
const StringRef ELFFiniFuncSectionName = ".fini";
const StringRef ELFCtorArrayFuncSectionName = ".ctors";
const StringRef ELFDtorArrayFuncSectionName = ".dtors";

const StringRef ELFInitSectionNames[3]{
    ELFInitArrayFuncSectionName,
    ELFInitFuncSectionName,
    ELFCtorArrayFuncSectionName,
};

const StringRef ELFFiniSectionNames[3]{
    ELFFiniArrayFuncSectionName,
    ELFFiniFuncSectionName,
    ELFDtorArrayFuncSectionName,
};

const StringRef ELFThreadBSSSectionName = ".tbss";
const StringRef ELFThreadDataSectionName = ".tdata";

bool isMachOInitializerSection(StringRef QualifiedName) {
  return llvm::is_contained(MachOInitSectionNames, QualifiedName);
}

bool isELFInitializerSection(StringRef SecName) {
  for (StringRef InitSection : ELFInitSectionNames) {
    StringRef Name = SecName;
    if (Name.consume_front(InitSection) && (Name.empty() || Name[0] == '.'))
      return true;
  }
  return false;
}

bool isELFFinalizerSection(StringRef SecName) {
  for (StringRef FiniSection : ELFFiniSectionNames) {
    StringRef Name = SecName;
    if (Name.consume_front(FiniSection) && (Name.empty() || Name[0] == '.'))
      return true;
  }
  return false;
}

bool isCOFFInitializerSection(StringRef SecName) {
  return SecName.starts_with(".CRT");
}

} // namespace orc
} // namespace llvm
