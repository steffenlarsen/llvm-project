//===- WebAssemblyOptions.td - WebAssembly options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def WebAssemblyOptsReg : Registry<"llvm::wasm_opts"> {
  // Getter bodies are compiled once, in WebAssemblyOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

def WASM_DisableEHPadSort : Option<
    "wasm-disable-ehpad-sort",
    "WebAssembly: Disable EH pad-first sort order. Testing purpose only.",
    "bool"> {
  let Registry = WebAssemblyOptsReg;
  let Default = "0";
  let ReallyHidden = 1;
}

def WASM_EHAllowlist : ListOption<
    "emscripten-cxx-exceptions-allowed",
    "The list of function names in which Emscripten-style "
    "exception handling is enabled (see emscripten "
    "EMSCRIPTEN_CATCHING_ALLOWED options)",
    "std::string"> {
  let Registry = WebAssemblyOptsReg;
  let CommaSeparated = 1;
}

def WASM_KeepRegisters : Option<
    "wasm-keep-registers",
    "WebAssembly: output stack registers in"
    " instruction output for test purposes only.",
    "bool"> {
  let Registry = WebAssemblyOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def WASM_DisableFallthroughReturnOpt : Option<
    "disable-wasm-fallthrough-return-opt",
    "WebAssembly: Disable fallthrough-return optimizations.",
    "bool"> {
  let Registry = WebAssemblyOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def WASM_DisableExplicitLocals : Option<
    "wasm-disable-explicit-locals",
    "WebAssembly: output implicit locals in"
    " instruction output for test purposes only.",
    "bool"> {
  let Registry = WebAssemblyOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def WASM_EnableEmEH : Option<
    "enable-emscripten-cxx-exceptions",
    "WebAssembly Emscripten-style exception handling",
    "bool"> {
  let Registry = WebAssemblyOptsReg;
  let Default = "0";
}

def WASM_EnableEmSjLj : Option<
    "enable-emscripten-sjlj",
    "WebAssembly Emscripten-style setjmp/longjmp handling",
    "bool"> {
  let Registry = WebAssemblyOptsReg;
  let Default = "0";
}

def WASM_EnableEH : Option<
    "wasm-enable-eh",
    "WebAssembly exception handling",
    "bool"> {
  let Registry = WebAssemblyOptsReg;
}

def WASM_EnableSjLj : Option<
    "wasm-enable-sjlj",
    "WebAssembly setjmp/longjmp handling",
    "bool"> {
  let Registry = WebAssemblyOptsReg;
}

def WASM_UseLegacyEH : Option<
    "wasm-use-legacy-eh",
    "WebAssembly exception handling (legacy)",
    "bool"> {
  let Registry = WebAssemblyOptsReg;
  let Default = "1";
}
