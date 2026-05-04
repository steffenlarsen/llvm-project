//===- BoltRuntimeLibsOptions.td - CLV2 option defs ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "bolt/Utils/BoltCategories.td"


def BoltRuntimeLibsOptsReg : Registry<"llvm::bolt::bolt_rtlibs_opts"> {
  // Getter bodies are compiled once, in BoltRuntimeLibsOptionsGetters.cpp.
  let OutOfLineGetters = 1;
  let ContextTypes = [CTX_BinaryContext];
}

// --- HugifyRuntimeLibrary.cpp / InstrumentationRuntimeLibrary.cpp ---

def BOLTRT_Hugify : Option<
    "hugify",
    "Automatically put hot code on 2MB page(s) (hugify) at "
    "runtime. No manual call to hugify is needed in the binary "
    "(which is what --hot-text relies on).",
    "bool"> {
  let Registry = BoltRuntimeLibsOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

def BOLTRT_RuntimeHugifyLib : Option<
    "runtime-hugify-lib",
    "specify path of the runtime hugify library",
    "std::string"> {
  let Registry = BoltRuntimeLibsOptsReg;
  let Category = BoltOptCat;
}

def BOLTRT_RuntimeInstrumentationLib : Option<
    "runtime-instrumentation-lib",
    "specify path of the runtime instrumentation library",
    "std::string"> {
  let Registry = BoltRuntimeLibsOptsReg;
  let Category = BoltOptCat;
}
