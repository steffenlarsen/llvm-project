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
#include "llvm/IR/IROptionsOptInfos.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/Regex.h"

using namespace llvm;

namespace {

/// Regular expression corresponding to the value given in one of the
/// -pass-remarks* command line flags. Passes whose name matches this regexp
/// will emit a diagnostic when calling the associated diagnostic function
/// (emitOptimizationRemark, emitOptimizationRemarkMissed or
/// emitOptimizationRemarkAnalysis).
struct PassRemarksOpt {
  std::shared_ptr<Regex> Pattern;

  void operator=(const std::string &Val) {
    // Create a regexp object to match pass names for emitOptimizationRemark.
    if (!Val.empty()) {
      Pattern = std::make_shared<Regex>(Val);
      std::string RegexError;
      if (!Pattern->isValid(RegexError))
        report_fatal_error(Twine("Invalid regular expression '") + Val +
                               "' in -pass-remarks: " + RegexError,
                           false);
    }
  }
};

/// Build a Regex from a clv2 list of patterns. Returns a combined pattern
/// if any values are present.
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

static PassRemarksOpt PassRemarksPassedOptLoc;
static PassRemarksOpt PassRemarksMissedOptLoc;
static PassRemarksOpt PassRemarksAnalysisOptLoc;

static const ir_opts::ParsedOpts *getOptsFromCtx(const LLVMContext *Ctx) {
  return Ctx ? clv2::getView<&clv2::IROptsReg>(Ctx->getOptionsContext())
             : nullptr;
}

static std::shared_ptr<Regex> getPassRemarksPattern(const LLVMContext *Ctx) {
  if (auto *O = getOptsFromCtx(Ctx))
    return buildPatternFromList(O->get<&clv2::IR_PassRemarks>());
  return PassRemarksPassedOptLoc.Pattern;
}

static std::shared_ptr<Regex>
getPassRemarksMissedPattern(const LLVMContext *Ctx) {
  if (auto *O = getOptsFromCtx(Ctx))
    return buildPatternFromList(O->get<&clv2::IR_PassRemarksMissed>());
  return PassRemarksMissedOptLoc.Pattern;
}

static std::shared_ptr<Regex>
getPassRemarksAnalysisPattern(const LLVMContext *Ctx) {
  if (auto *O = getOptsFromCtx(Ctx))
    return buildPatternFromList(O->get<&clv2::IR_PassRemarksAnalysis>());
  return PassRemarksAnalysisOptLoc.Pattern;
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
