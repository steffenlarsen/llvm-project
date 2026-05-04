//===- XCoreOptions.td - XCore target options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def XCoreOptsReg : Registry<"llvm::xcore_opts"> {
  let ContextTypes = [CTX_Module];
  // Getter bodies are compiled once, in XCoreOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

def XCORE_MaxThreads : Option<
    "xcore-max-threads",
    "Maximum number of threads (for emulation thread-local storage)",
    "unsigned"> {
  let Registry = XCoreOptsReg;
  let Default = "8";
  let Hidden = 1;
  let ValueDesc = "number";
}
