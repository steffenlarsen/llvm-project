//===- llvm/Remarks/RemarkStreamer.cpp - Remark Streamer -*- C++ --------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of the main remark streamer.
//
//===----------------------------------------------------------------------===//

#include "llvm/Remarks/RemarkStreamer.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Remarks/RemarksOptions.h"
#include <cassert>
#include <optional>

using namespace llvm;
using namespace llvm::remarks;

RemarkStreamer::RemarkStreamer(
    std::unique_ptr<remarks::RemarkSerializer> RemarkSerializer,
    std::optional<StringRef> FilenameIn)
    : RemarkSerializer(std::move(RemarkSerializer)),
      Filename(FilenameIn ? std::optional<std::string>(FilenameIn->str())
                          : std::nullopt) {}

RemarkStreamer::~RemarkStreamer() {
  // Ensure that llvm::finalizeOptimizationRemarks was called before the
  // RemarkStreamer is destroyed.
  assert(!RemarkSerializer &&
         "RemarkSerializer must be released before RemarkStreamer is "
         "destroyed. Ensure llvm::finalizeOptimizationRemarks is called.");
}

Error RemarkStreamer::setFilter(StringRef Filter) {
  Regex R = Regex(Filter);
  std::string RegexError;
  if (!R.isValid(RegexError))
    return createStringError(std::make_error_code(std::errc::invalid_argument),
                             RegexError.data());
  PassFilter = std::move(R);
  return Error::success();
}

bool RemarkStreamer::matchesFilter(StringRef Str) {
  if (PassFilter)
    return PassFilter->match(Str);
  // No filter means all strings pass.
  return true;
}

bool RemarkStreamer::needsSection(const LLVMContext &Ctx) const {
  std::optional<bool> V = Ctx.getOptions<RemarksOptions>().REM_RemarksSection;
  return V.has_value() && *V;
}

bool RemarkStreamer::wantsSection(const LLVMContext &Ctx) const {
  std::optional<bool> V = Ctx.getOptions<RemarksOptions>().REM_RemarksSection;
  if (V.has_value() && !*V)
    return false;
  // Enable remark sections by default for bitstream remarks (so dsymutil can
  // find all remarks for a linked binary)
  return needsSection(Ctx) ||
         RemarkSerializer->SerializerFormat == Format::Bitstream;
}
