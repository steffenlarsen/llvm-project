//===- ObjCARCOptions.td - ObjC ARC options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def ObjCARCOptsReg : Registry<"llvm::arc_opts"> {
  // Getter bodies are compiled once, in ObjCARCOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

def ARC_MaxPtrStates : Option<
    "arc-opt-max-ptr-states",
    "Maximum number of ptr states the optimizer keeps track of",
    "unsigned"> {
  let Registry = ObjCARCOptsReg;
  let Default = "4095";
  let Hidden = 1;
}

def ARC_UseObjCClaimRV : Option<
    "arc-contract-use-objc-claim-rv",
    "Enable generation of calls to objc_claimAutoreleasedReturnValue",
    "std::optional<bool>"> {
  let Registry = ObjCARCOptsReg;
}
