//===- ObjCARCOptions.td - ObjC ARC options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def ObjCARCOptions : OptionsStruct<"llvm"> {}

defm ARC_MaxPtrStates : ValueField<
    "arc-opt-max-ptr-states", "unsigned", "4095",
    "Maximum number of ptr states the optimizer keeps track of",
    /*hidden=*/1>;

defm ARC_UseObjCClaimRV : OptionalValueField<
    "arc-contract-use-objc-claim-rv", "bool", "true",
    "Enable generation of calls to objc_claimAutoreleasedReturnValue">;
