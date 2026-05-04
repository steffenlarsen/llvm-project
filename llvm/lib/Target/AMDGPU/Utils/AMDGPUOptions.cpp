//===- AMDGPUOptions.cpp - AMDGPU target option bridge ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Twine.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/AMDGPU/AMDGPUOptionsOptInfos.h"
using namespace llvm;
using namespace llvm::clv2;
