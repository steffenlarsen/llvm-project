//===- RemarksOptions.td - Remarks options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def RemarksOptsReg : Registry<"llvm::remarks"> {
  // Getter bodies are compiled once, in RemarksOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

def REM_RemarksSection : Option<
    "remarks-section",
    "Emit a section containing remark diagnostics metadata. By default, this "
    "is enabled for the following formats: bitstream.",
    "std::optional<bool>"> {
  let Registry = RemarksOptsReg;
  let Hidden = 1;
}
