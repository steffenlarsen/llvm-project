//===- ObjectOptions.td - Object options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def ObjectOptsReg : Registry<"llvm::object"> {
  // Getter bodies are compiled once, in ObjectOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

def OBJ_DisableBitcodeVersionUpgrade : Option<
    "disable-bitcode-version-upgrade",
    "Disable automatic bitcode upgrade for version mismatch",
    "bool"> {
  let Registry = ObjectOptsReg;
  let Hidden = 1;
}
