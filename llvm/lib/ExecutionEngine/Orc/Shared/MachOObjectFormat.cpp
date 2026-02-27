//===-------- MachOObjectFormat.cpp -- MachO format details for ORC -------===//
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

#include "llvm/ExecutionEngine/Orc/Shared/MachOObjectFormat.h"

namespace llvm {
namespace orc {

const StringRef MachODataCommonSectionName = "__DATA,__common";
const StringRef MachODataDataSectionName = "__DATA,__data";
const StringRef MachOEHFrameSectionName = "__TEXT,__eh_frame";
const StringRef MachOCompactUnwindSectionName = "__LD,__compact_unwind";
const StringRef MachOCStringSectionName = "__TEXT,__cstring";
const StringRef MachOModInitFuncSectionName = "__DATA,__mod_init_func";
const StringRef MachOObjCCatListSectionName = "__DATA,__objc_catlist";
const StringRef MachOObjCCatList2SectionName = "__DATA,__objc_catlist2";
const StringRef MachOObjCClassListSectionName = "__DATA,__objc_classlist";
const StringRef MachOObjCClassNameSectionName = "__TEXT,__objc_classname";
const StringRef MachOObjCClassRefsSectionName = "__DATA,__objc_classrefs";
const StringRef MachOObjCConstSectionName = "__DATA,__objc_const";
const StringRef MachOObjCDataSectionName = "__DATA,__objc_data";
const StringRef MachOObjCImageInfoSectionName = "__DATA,__objc_imageinfo";
const StringRef MachOObjCMethNameSectionName = "__TEXT,__objc_methname";
const StringRef MachOObjCMethTypeSectionName = "__TEXT,__objc_methtype";
const StringRef MachOObjCNLCatListSectionName = "__DATA,__objc_nlcatlist";
const StringRef MachOObjCNLClassListSectionName = "__DATA,__objc_nlclslist";
const StringRef MachOObjCProtoListSectionName = "__DATA,__objc_protolist";
const StringRef MachOObjCProtoRefsSectionName = "__DATA,__objc_protorefs";
const StringRef MachOObjCSelRefsSectionName = "__DATA,__objc_selrefs";
const StringRef MachOSwift5ProtoSectionName = "__TEXT,__swift5_proto";
const StringRef MachOSwift5ProtosSectionName = "__TEXT,__swift5_protos";
const StringRef MachOSwift5TypesSectionName = "__TEXT,__swift5_types";
const StringRef MachOSwift5TypeRefSectionName = "__TEXT,__swift5_typeref";
const StringRef MachOSwift5FieldMetadataSectionName = "__TEXT,__swift5_fieldmd";
const StringRef MachOSwift5EntrySectionName = "__TEXT,__swift5_entry";
const StringRef MachOTextTextSectionName = "__TEXT,__text";
const StringRef MachOThreadBSSSectionName = "__DATA,__thread_bss";
const StringRef MachOThreadDataSectionName = "__DATA,__thread_data";
const StringRef MachOThreadVarsSectionName = "__DATA,__thread_vars";
const StringRef MachOUnwindInfoSectionName = "__TEXT,__unwind_info";

const StringRef MachOInitSectionNames[22] = {
    MachOModInitFuncSectionName,         MachOObjCCatListSectionName,
    MachOObjCCatList2SectionName,        MachOObjCClassListSectionName,
    MachOObjCClassNameSectionName,       MachOObjCClassRefsSectionName,
    MachOObjCConstSectionName,           MachOObjCDataSectionName,
    MachOObjCImageInfoSectionName,       MachOObjCMethNameSectionName,
    MachOObjCMethTypeSectionName,        MachOObjCNLCatListSectionName,
    MachOObjCNLClassListSectionName,     MachOObjCProtoListSectionName,
    MachOObjCProtoRefsSectionName,       MachOObjCSelRefsSectionName,
    MachOSwift5ProtoSectionName,         MachOSwift5ProtosSectionName,
    MachOSwift5TypesSectionName,         MachOSwift5TypeRefSectionName,
    MachOSwift5FieldMetadataSectionName, MachOSwift5EntrySectionName,
};

bool isMachOInitializerSection(StringRef SegName, StringRef SecName) {
  for (const auto &InitSection : MachOInitSectionNames) {
    // Loop below assumes all MachO init sectios have a length-6
    // segment name.
    assert(InitSection[6] == ',' && "Init section seg name has length != 6");
    if (InitSection.starts_with(SegName) && InitSection.substr(7) == SecName)
      return true;
  }
  return false;
}

} // namespace orc
} // namespace llvm
