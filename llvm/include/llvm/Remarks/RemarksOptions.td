//===- RemarksOptions.td - Remarks options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def RemarksOptions : OptionsStruct<"llvm"> {}

defm REM_RemarksSection : OptionalValueField<
    "remarks-section", "bool", "true",
    "Emit a section containing remark diagnostics metadata. By default, this "
    "is enabled for the following formats: bitstream.", /*hidden=*/1>;
