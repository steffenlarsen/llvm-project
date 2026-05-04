//===- AggressiveInstCombineOptions.cpp - Option bridge
//--------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Transforms/AggressiveInstCombine/AggressiveInstCombineOptionsOptInfos.h"

using namespace llvm;
using namespace llvm::clv2;
