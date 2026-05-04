//===- X86Options.cpp - X86 target option bridge --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Target/X86/X86OptionsOptInfos.h"
using namespace llvm;
using namespace llvm::clv2;
