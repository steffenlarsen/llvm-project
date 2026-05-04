//===- MlirOptToolOptionsOptInfos.h - clv2 OptionInfo for mlir-opt -*- C++
//-*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_TOOLS_MLIR_OPT_MLIROPTTOOLOPTIONSOPTINFOS_H
#define MLIR_TOOLS_MLIR_OPT_MLIROPTTOOLOPTIONSOPTINFOS_H

#include "mlir/Tools/mlir-opt/MlirOptMain.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"

#define CLV2_OPTIONS_DECL
#include "mlir/Tools/mlir-opt/MlirOptToolOptionsOptInfos.inc"
#undef CLV2_OPTIONS_DECL

#define CLV2_OPTIONS_GETTERS
#include "mlir/Tools/mlir-opt/MlirOptToolOptionsOptInfos.inc"
#undef CLV2_OPTIONS_GETTERS

#endif // MLIR_TOOLS_MLIR_OPT_MLIROPTTOOLOPTIONSOPTINFOS_H
