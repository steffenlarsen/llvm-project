//===- llvm/Support/BoolOrDefault.h - Tri-state boolean ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A tri-state boolean: unset, true, or false. Used by command-line options
// where "not specified" is distinct from "explicitly set to false".
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_BOOLORDEFAULT_H
#define LLVM_SUPPORT_BOOLORDEFAULT_H

namespace llvm {

enum class BoolOrDefault { BOU_UNSET = 0, BOU_TRUE, BOU_FALSE };

} // namespace llvm

// Aliases in the cl:: namespace.
namespace llvm::cl {
using boolOrDefault = llvm::BoolOrDefault;
} // namespace llvm::cl

#endif // LLVM_SUPPORT_BOOLORDEFAULT_H
