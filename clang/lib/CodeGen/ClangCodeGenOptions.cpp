//===- ClangCodeGenOptions.cpp - Clang CodeGen option bridge --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Intentionally empty — all ClangCodeGen options are now read directly via
// OptionsContext getters generated from ClangCodeGenOptions.td. No legacy
// global bridge is needed.
