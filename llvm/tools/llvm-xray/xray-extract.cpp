//===- xray-extract.cpp: XRay Instrumentation Map Extraction --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of the xray-extract.h interface.
//
// FIXME: Support other XRay-instrumented binary formats other than ELF.
//
//===----------------------------------------------------------------------===//


#include "func-id-helper.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/XRay/InstrumentationMap.h"

using namespace llvm;
using namespace llvm::xray;
using namespace llvm::yaml;

std::string ExtractInputVal;
std::string ExtractOutputVal;
bool ExtractSymbolizeVal;
bool ExtractNoDemangleVal;

static void exportAsYAML(const InstrumentationMap &Map, raw_ostream &OS,
                         FuncIdConversionHelper &FH) {
  std::vector<YAMLXRaySledEntry> YAMLSleds;
  auto Sleds = Map.sleds();
  YAMLSleds.reserve(llvm::size(Sleds));
  for (const auto &Sled : Sleds) {
    auto FuncId = Map.getFunctionId(Sled.Function);
    if (!FuncId)
      return;
    YAMLSleds.push_back(
        {*FuncId, Sled.Address, Sled.Function, Sled.Kind, Sled.AlwaysInstrument,
         ExtractSymbolizeVal ? FH.SymbolOrNumber(*FuncId) : "", Sled.Version});
  }
  Output Out(OS, nullptr, 0);
  Out << YAMLSleds;
}

Error tryExtract() {
  auto InstrumentationMapOrError = loadInstrumentationMap(ExtractInputVal);
  if (!InstrumentationMapOrError)
    return joinErrors(make_error<StringError>(
                          Twine("Cannot extract instrumentation map from '") +
                              ExtractInputVal + "'.",
                          std::make_error_code(std::errc::invalid_argument)),
                      InstrumentationMapOrError.takeError());

  std::error_code EC;
  raw_fd_ostream OS(ExtractOutputVal, EC, sys::fs::OpenFlags::OF_TextWithCRLF);
  if (EC)
    return make_error<StringError>(
        Twine("Cannot open file '") + ExtractOutputVal + "' for writing.", EC);
  const auto &FunctionAddresses =
      InstrumentationMapOrError->getFunctionAddresses();
  symbolize::LLVMSymbolizer::Options opts;
  if (ExtractNoDemangleVal)
    opts.Demangle = false;
  symbolize::LLVMSymbolizer Symbolizer(opts);
  FuncIdConversionHelper FuncIdHelper(ExtractInputVal, Symbolizer,
                                      FunctionAddresses);
  exportAsYAML(*InstrumentationMapOrError, OS, FuncIdHelper);
  return Error::success();
}
