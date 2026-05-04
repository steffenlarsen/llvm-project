//===- VectorizeOptionsGetters.cpp - clv2 getter definitions --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The single translation unit that defines the generated option getters for
// VectorizeOptsReg.  Every other TU sees only their declarations.
//
// Getters defined `inline` in the generated header are the dominant build-time
// cost of clv2: each including TU parses every getter and eagerly instantiates
// getOptValOr<> -> ParsedOptions<N> -> index_of_v<N> for all of them, even
// though most are never called.  Compiling the bodies exactly once here keeps
// that cost off the ~840 TUs that merely read an option.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Vectorize/VectorizeOptionsOptInfos.h"

#define CLV2_OPTIONS_GETTER_DEFS
#include "llvm/Transforms/Vectorize/VectorizeOptionsOptInfos.inc"
#undef CLV2_OPTIONS_GETTER_DEFS
