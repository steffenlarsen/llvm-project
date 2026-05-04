//===- PrintPasses.h - Determining whether/when to print IR ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_PRINTPASSES_H
#define LLVM_IR_PRINTPASSES_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include <vector>

namespace llvm {

enum class ChangePrinter {
  None,
  Verbose,
  Quiet,
  DiffVerbose,
  DiffQuiet,
  ColourDiffVerbose,
  ColourDiffQuiet,
  DotCfgVerbose,
  DotCfgQuiet
};

class LLVMContext;
namespace clv2 {
class OptionsContext;
}

LLVM_ABI ChangePrinter getPrintChanged(const LLVMContext &Ctx);

LLVM_ABI bool shouldPrintBeforeSomePass(const LLVMContext &Ctx);
LLVM_ABI bool shouldPrintAfterSomePass(const LLVMContext &Ctx);

LLVM_ABI bool shouldPrintBeforePass(const LLVMContext &Ctx, StringRef PassID);
LLVM_ABI bool shouldPrintAfterPass(const LLVMContext &Ctx, StringRef PassID);

// Overloads without LLVMContext for legacy pass manager use.
LLVM_ABI bool shouldPrintBeforePass(StringRef PassID);
LLVM_ABI bool shouldPrintAfterPass(StringRef PassID);

// Overloads taking OptionsContext directly (used by legacy PM with threaded
// context).
LLVM_ABI bool shouldPrintBeforePass(StringRef PassID,
                                    const clv2::OptionsContext &Ctx);
LLVM_ABI bool shouldPrintAfterPass(StringRef PassID,
                                   const clv2::OptionsContext &Ctx);

LLVM_ABI bool shouldPrintBeforeAll(const LLVMContext &Ctx);
LLVM_ABI bool shouldPrintAfterAll(const LLVMContext &Ctx);

LLVM_ABI std::vector<std::string> printBeforePasses(const LLVMContext &Ctx);
LLVM_ABI std::vector<std::string> printAfterPasses(const LLVMContext &Ctx);

LLVM_ABI bool forcePrintModuleIR(const LLVMContext &Ctx);
LLVM_ABI bool forcePrintFuncIR(const LLVMContext &Ctx);
LLVM_ABI bool isPassInPrintList(const LLVMContext &Ctx, StringRef PassName);
LLVM_ABI bool isFilterPassesEmpty(const LLVMContext &Ctx);
LLVM_ABI bool isFunctionInPrintList(const LLVMContext &Ctx,
                                    StringRef FunctionName);

// Ensure temporary files exist, creating or re-using them.  \p FD contains
// file descriptors (-1 indicates that the file should be created) and
// \p SR contains the corresponding initial content.  \p FileName will have
// the filenames filled in when creating files.  Return first error code (if
// any) and stop.
LLVM_ABI std::error_code prepareTempFiles(SmallVector<int> &FD,
                                          ArrayRef<StringRef> SR,
                                          SmallVector<std::string> &FileName);

// Remove the temporary files in \p FileName.  Typically used in conjunction
// with prepareTempFiles.  Return first error code (if any) and stop..
LLVM_ABI std::error_code cleanUpTempFiles(ArrayRef<std::string> FileName);

// Perform a system based diff between \p Before and \p After, using \p
// OldLineFormat, \p NewLineFormat, and \p UnchangedLineFormat to control the
// formatting of the output. Return an error message for any failures instead
// of the diff.
LLVM_ABI std::string doSystemDiff(const clv2::OptionsContext &Ctx,
                                  StringRef Before, StringRef After,
                                  StringRef OldLineFormat,
                                  StringRef NewLineFormat,
                                  StringRef UnchangedLineFormat);

// Report a -print-changed diff for one pass over one IR unit (function or
// module). IsInteresting is isPassInPrintList(PassID); ShouldReport is whether
// the unit passed all filters (Before/After are only set then).
LLVM_ABI void reportChangedIR(const LLVMContext &Ctx, StringRef Before,
                              StringRef After, StringRef PassName,
                              StringRef PassID, StringRef IRName,
                              bool IsInteresting, bool ShouldReport);

} // namespace llvm

#endif // LLVM_IR_PRINTPASSES_H
