//===- ARCOptions.td - ARC target options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def ARCOptsReg : Registry<"llvm::arc_target_opts"> {
  let ContextTypes = [CTX_MachineFunction];
  // Getter bodies are compiled once, in ARCOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

def ARC_KillAddrMode : Option<
    "arc-kill-addr-mode", "",
    "unsigned"> {
  let Registry = ARCOptsReg;
  let Default = "0";
  let ReallyHidden = 1;
}

def ARC_UseSaveRestoreFunclet : Option<
    "arc-save-restore-funclet",
    "Use arc callee save/restore functions",
    "bool"> {
  let Registry = ARCOptsReg;
  let Default = "1";
  let Hidden = 1;
}
