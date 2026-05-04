//===- BitcodeMemProfDefaults.h - memprof option defaults -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Split out so the generated clv2 options header can name this default without
// pulling in the bitcode writer.  The value is build-mode dependent, which a
// plain literal in the .td cannot express.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BITCODE_BITCODEMEMPROFDEFAULTS_H
#define LLVM_BITCODE_BITCODEMEMPROFDEFAULTS_H

namespace llvm::bitcode {

/// Default for --combined-index-memprof-context.  Assertions builds default it
/// on so the context-carrying records stay exercised in testing; release builds
/// leave it off to keep combined indexes small.
inline constexpr bool CombinedIndexMemProfContextDefault =
#ifdef NDEBUG
    false;
#else
    true;
#endif

} // namespace llvm::bitcode

#endif // LLVM_BITCODE_BITCODEMEMPROFDEFAULTS_H
