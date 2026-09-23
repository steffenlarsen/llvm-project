//===- BitcodeOptions.td - Bitcode options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def BitcodeOptions : OptionsStruct<"llvm"> {}

defm BC_MDIndexThreshold : OptionalValueField<"bitcode-mdindex-threshold",
    "unsigned", "25",
    "Number of metadatas above which we emit an index to enable lazy-loading",
    1>;

defm BC_PreserveBCUseListOrder : OptionalValueField<"preserve-bc-uselistorder",
    "bool", "true", "Preserve use-list order when writing LLVM bitcode.", 1>;

defm BC_ImportFullTypeDefinitions : BoolField<"import-full-type-definitions",
    "false", "Import full type definitions for ThinLTO.", 1>;

defm BC_DisableLazyLoading : BoolField<"disable-ondemand-mds-loading",
    "false",
    "Force disable the lazy-loading on-demand of metadata when loading "
    "bitcode for importing.",
    1>;

defm BC_ExpandConstantExprs : BoolField<"expand-constant-exprs", "false",
    "Expand constant expressions to instructions for testing purposes", 1>;

defm BC_FlushThreshold : ValueField<"bitcode-flush-threshold", "unsigned",
    "512", "The threshold (unit M) for flushing LLVM bitcode.", 1>;
