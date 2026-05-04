//===- BoltContextTypes.td - clv2 context types for BOLT --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Shared by every BOLT option registry, including the ones that do not pull in
// BoltCategories.td.
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

// BOLT threads a BinaryContext everywhere, so it is the natural context type
// for BOLT options; the generated getters take one directly.
def CTX_BinaryContext : ContextType<"llvm::bolt::BinaryContext",
    "$.getOptionsContext()">;
