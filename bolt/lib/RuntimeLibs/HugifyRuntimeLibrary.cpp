//===- bolt/RuntimeLibs/HugifyRuntimeLibrary.cpp - Hugify RT Library ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the HugifyRuntimeLibrary class.
//
//===----------------------------------------------------------------------===//

#include "bolt/RuntimeLibs/HugifyRuntimeLibrary.h"
#include "bolt/Core/BinaryContext.h"
#include "bolt/Core/Linker.h"
#include "bolt/RuntimeLibs/BoltRuntimeLibsOptionsOptInfos.h"
#include "bolt/Utils/BoltUtilsOptionsOptInfos.h"
#include "llvm/MC/MCStreamer.h"

using namespace llvm;
using namespace bolt;

namespace opts {

extern bool HotText;

} // namespace opts

void HugifyRuntimeLibrary::adjustCommandLineOptions(
    const BinaryContext &BC) const {
  bool HotText = bolt::bolt_utils_opts::getHotText(BC);
  if (HotText) {
    errs()
        << "BOLT-ERROR: -hot-text should be applied to binaries with "
           "pre-compiled manual hugify support, while -hugify will add hugify "
           "support automatically. These two options cannot both be present.\n";
    exit(1);
  }
  // After the check, we set HotText to be true because automated hugify support
  // relies on it.
  // BC is const here, so writing back to the parsed view still needs the
  // cast.  (FIXME: mutating a shared options view at runtime is itself
  // hostile to the parallelism clv2 is meant to enable.)
  auto &Ctx = const_cast<clv2::OptionsContext &>(BC.getOptionsContext());
  if (auto *V = Ctx.getViewPtr<&clv2::BoltUtilsOptsReg>())
    V->get<&clv2::BOLT_HotText>() = true;
  if (!BC.StartFunctionAddress) {
    errs() << "BOLT-ERROR: hugify runtime libraries require a known entry "
              "point of "
              "the input binary\n";
    exit(1);
  }
}

void HugifyRuntimeLibrary::link(BinaryContext &BC, StringRef ToolPath,
                                BOLTLinker &Linker,
                                BOLTLinker::SectionsMapper MapSections) {

  std::string RuntimeHugifyLib = bolt_rtlibs_opts::getRuntimeHugifyLib(BC);
  std::string LibPath = getLibPath(ToolPath, RuntimeHugifyLib);
  loadLibrary(LibPath, Linker, MapSections);

  assert(!RuntimeStartAddress &&
         "We don't currently support linking multiple runtime libraries");
  auto StartSymInfo = Linker.lookupSymbolInfo("__bolt_hugify_self");
  if (!StartSymInfo) {
    errs() << "BOLT-ERROR: hugify library does not define __bolt_hugify_self: "
           << LibPath << "\n";
    exit(1);
  }
  RuntimeStartAddress = StartSymInfo->Address;
}
