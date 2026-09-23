//===- DiagnosticHandler.h - DiagnosticHandler class for LLVM -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#include "llvm/IR/DiagnosticHandler.h"
#include "llvm/IR/IROptions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Regex.h"

using namespace llvm;

namespace {

/// Build a Regex from a -pass-remarks* list of patterns. Returns a combined
/// pattern if any values are present.
static std::shared_ptr<Regex>
buildPatternFromList(const std::vector<std::string> &Vals) {
  if (Vals.empty())
    return nullptr;
  // Repeated flags: the last value wins.
  const std::string &Val = Vals.back();
  if (Val.empty())
    return nullptr;
  auto Pat = std::make_shared<Regex>(Val);
  std::string RegexError;
  if (!Pat->isValid(RegexError))
    report_fatal_error(Twine("Invalid regular expression '") + Val +
                           "' in -pass-remarks: " + RegexError,
                       false);
  return Pat;
}
} // namespace

static const IROptions &getOptsFromCtx(const LLVMContext *Ctx) {
  return Ctx ? Ctx->getOptions<IROptions>() : IROptions::Current;
}

static std::shared_ptr<Regex> getPassRemarksPattern(const LLVMContext *Ctx) {
  return buildPatternFromList(getOptsFromCtx(Ctx).IR_PassRemarks);
}

static std::shared_ptr<Regex>
getPassRemarksMissedPattern(const LLVMContext *Ctx) {
  return buildPatternFromList(getOptsFromCtx(Ctx).IR_PassRemarksMissed);
}

static std::shared_ptr<Regex>
getPassRemarksAnalysisPattern(const LLVMContext *Ctx) {
  return buildPatternFromList(getOptsFromCtx(Ctx).IR_PassRemarksAnalysis);
}

bool DiagnosticHandler::isAnalysisRemarkEnabled(StringRef PassName) const {
  auto Pat = getPassRemarksAnalysisPattern(OwnerCtx);
  return (Pat && Pat->match(PassName));
}
bool DiagnosticHandler::isMissedOptRemarkEnabled(StringRef PassName) const {
  auto Pat = getPassRemarksMissedPattern(OwnerCtx);
  return (Pat && Pat->match(PassName));
}
bool DiagnosticHandler::isPassedOptRemarkEnabled(StringRef PassName) const {
  auto Pat = getPassRemarksPattern(OwnerCtx);
  return (Pat && Pat->match(PassName));
}

bool DiagnosticHandler::isAnyRemarkEnabled() const {
  return (getPassRemarksPattern(OwnerCtx) ||
          getPassRemarksMissedPattern(OwnerCtx) ||
          getPassRemarksAnalysisPattern(OwnerCtx));
}
