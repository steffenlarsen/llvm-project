//===- SaveStatsMode.h - Where to write -save-stats output ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Split out of CommandFlags.h to break a cycle: the generated clv2 options
// header is typed on this enum, but CommandFlags.h includes that same options
// header.  Keeping the enum here lets the options header depend on just this.
//
// Stays in namespace codegen -- the enumerators (None, Cwd, Obj) are far too
// generic to sit at llvm:: scope.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SAVESTATSMODE_H
#define LLVM_CODEGEN_SAVESTATSMODE_H

namespace llvm::codegen {
enum SaveStatsMode { None, Cwd, Obj };
} // namespace llvm::codegen

#endif // LLVM_CODEGEN_SAVESTATSMODE_H
