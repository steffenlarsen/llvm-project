//===- ARCOptions.td - ARC target options ----------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def ARCOptions : OptionsStruct<"llvm"> {}

defm ARC_KillAddrMode : ValueField<
    "arc-kill-addr-mode", "unsigned", "0", "",
    /*hidden=*/1>;

defm ARC_UseSaveRestoreFunclet : BoolField<
    "arc-save-restore-funclet", "true",
    "Use arc callee save/restore functions",
    /*hidden=*/1>;
