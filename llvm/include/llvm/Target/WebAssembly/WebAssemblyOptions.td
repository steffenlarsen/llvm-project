//===- WebAssemblyOptions.td - WebAssembly options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def WebAssemblyOptions : OptionsStruct<"llvm"> {}

defm WASM_DisableEHPadSort : BoolField<
    "wasm-disable-ehpad-sort", "false",
    "WebAssembly: Disable EH pad-first sort order. Testing purpose only.",
    /*hidden=*/1>;

defm WASM_EHAllowlist : ListField<
    "emscripten-cxx-exceptions-allowed", "std::string",
    "The list of function names in which Emscripten-style "
    "exception handling is enabled (see emscripten "
    "EMSCRIPTEN_CATCHING_ALLOWED options)">;

defm WASM_KeepRegisters : BoolField<
    "wasm-keep-registers", "false",
    "WebAssembly: output stack registers in"
    " instruction output for test purposes only.",
    /*hidden=*/1>;

defm WASM_DisableFallthroughReturnOpt : BoolField<
    "disable-wasm-fallthrough-return-opt", "false",
    "WebAssembly: Disable fallthrough-return optimizations.",
    /*hidden=*/1>;

defm WASM_DisableExplicitLocals : BoolField<
    "wasm-disable-explicit-locals", "false",
    "WebAssembly: output implicit locals in"
    " instruction output for test purposes only.",
    /*hidden=*/1>;

defm WASM_EnableEmEH : BoolField<
    "enable-emscripten-cxx-exceptions", "false",
    "WebAssembly Emscripten-style exception handling">;

defm WASM_EnableEmSjLj : BoolField<
    "enable-emscripten-sjlj", "false",
    "WebAssembly Emscripten-style setjmp/longjmp handling">;

defm WASM_EnableEH : BoolField<
    "wasm-enable-eh", "false",
    "WebAssembly exception handling">;

defm WASM_EnableSjLj : BoolField<
    "wasm-enable-sjlj", "false",
    "WebAssembly setjmp/longjmp handling">;

defm WASM_UseLegacyEH : BoolField<
    "wasm-use-legacy-eh", "true",
    "WebAssembly exception handling (legacy)">;
