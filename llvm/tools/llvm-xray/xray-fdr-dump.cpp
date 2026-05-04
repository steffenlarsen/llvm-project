//===- xray-fdr-dump.cpp: XRay FDR Trace Dump Tool ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the FDR trace dumping tool, using the libraries for handling FDR
// mode traces specifically.
//
//===----------------------------------------------------------------------===//
#include "llvm/Support/FileSystem.h"
#include "llvm/XRay/BlockIndexer.h"
#include "llvm/XRay/BlockPrinter.h"
#include "llvm/XRay/BlockVerifier.h"
#include "llvm/XRay/FDRRecordConsumer.h"
#include "llvm/XRay/FDRRecordProducer.h"
#include "llvm/XRay/FDRRecords.h"
#include "llvm/XRay/FileHeaderReader.h"
#include "llvm/XRay/RecordPrinter.h"

using namespace llvm;
using namespace xray;

std::string DumpInputVal;
bool DumpVerifyVal;

Error tryFdrDump() {
  auto FDOrErr = sys::fs::openNativeFileForRead(DumpInputVal);
  if (!FDOrErr)
    return FDOrErr.takeError();

  uint64_t FileSize;
  if (auto EC = sys::fs::file_size(DumpInputVal, FileSize))
    return createStringError(EC, "Failed to get file size for '%s'.",
                             DumpInputVal.c_str());

  std::error_code EC;
  sys::fs::mapped_file_region MappedFile(
      *FDOrErr, sys::fs::mapped_file_region::mapmode::readonly, FileSize, 0,
      EC);
  sys::fs::closeFile(*FDOrErr);

  DataExtractor DE(StringRef(MappedFile.data(), MappedFile.size()), true);
  uint64_t OffsetPtr = 0;

  auto FileHeaderOrError = readBinaryFormatHeader(DE, OffsetPtr);
  if (!FileHeaderOrError)
    return FileHeaderOrError.takeError();
  auto &H = FileHeaderOrError.get();

  FileBasedRecordProducer P(H, DE, OffsetPtr);

  RecordPrinter RP(outs(), "\n");
  if (!DumpVerifyVal) {
    PipelineConsumer C({&RP});
    while (DE.isValidOffsetForDataOfSize(OffsetPtr, 1)) {
      auto R = P.produce();
      if (!R)
        return R.takeError();
      if (auto E = C.consume(std::move(R.get())))
        return E;
    }
    return Error::success();
  }

  BlockPrinter BP(outs(), RP);
  std::vector<std::unique_ptr<Record>> Records;
  LogBuilderConsumer C(Records);
  while (DE.isValidOffsetForDataOfSize(OffsetPtr, 1)) {
    auto R = P.produce();
    if (!R) {
      for (auto &Ptr : Records)
        if (auto E = Ptr->apply(RP))
          return joinErrors(std::move(E), R.takeError());
      return R.takeError();
    }
    if (auto E = C.consume(std::move(R.get())))
      return E;
  }

  BlockIndexer::Index Index;
  BlockIndexer BI(Index);
  for (auto &Ptr : Records)
    if (auto E = Ptr->apply(BI))
      return E;

  if (auto E = BI.flush())
    return E;

  BlockVerifier BV;
  for (const auto &ProcessThreadBlocks : Index) {
    auto &Blocks = ProcessThreadBlocks.second;
    for (auto &B : Blocks) {
      for (auto *R : B.Records) {
        if (auto E = R->apply(BV))
          return E;
        if (auto E = R->apply(BP))
          return E;
      }
      BV.reset();
      BP.reset();
    }
  }
  outs().flush();
  return Error::success();
}
