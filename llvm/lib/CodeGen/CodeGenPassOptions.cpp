//===- CodeGenPassOptions.cpp - CodeGen pass option bridge
//-----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/CodeGenPassOptionsOptInfos.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/OptionsContext.h"
using namespace llvm;
using namespace llvm::clv2;

/// Helper: look up a registry view from an OptionsContext, falling back to
template <const auto *Reg>
static const typename std::remove_pointer_t<decltype(Reg)>::ParsedOptionsT *
getViewOrRuntime(const OptionsContext &Ctx) {
  if (auto *V = Ctx.getViewPtr<Reg>())
    return V;
  return nullptr;
}

const cgpass_opts::CGPassAsmPrintRegOpts *
cgpass_opts::getCGPassAsmPrintReg(const OptionsContext &Ctx) {
  return getViewOrRuntime<&CGPassAsmPrintReg>(Ctx);
}

const cgpass_opts::CGPassCore1RegOpts *
cgpass_opts::getCGPassCore1Reg(const OptionsContext &Ctx) {
  return getViewOrRuntime<&CGPassCore1Reg>(Ctx);
}

const cgpass_opts::CGPassCore2RegOpts *
cgpass_opts::getCGPassCore2Reg(const OptionsContext &Ctx) {
  return getViewOrRuntime<&CGPassCore2Reg>(Ctx);
}

const cgpass_opts::CGPassGISelRegOpts *
cgpass_opts::getCGPassGISelReg(const OptionsContext &Ctx) {
  return getViewOrRuntime<&CGPassGISelReg>(Ctx);
}

const cgpass_opts::CGPassMachine1RegOpts *
cgpass_opts::getCGPassMachine1Reg(const OptionsContext &Ctx) {
  return getViewOrRuntime<&CGPassMachine1Reg>(Ctx);
}

const cgpass_opts::CGPassMachine2RegOpts *
cgpass_opts::getCGPassMachine2Reg(const OptionsContext &Ctx) {
  return getViewOrRuntime<&CGPassMachine2Reg>(Ctx);
}

const cgpass_opts::CGPassRegAllocRegOpts *
cgpass_opts::getCGPassRegAllocReg(const OptionsContext &Ctx) {
  return getViewOrRuntime<&CGPassRegAllocReg>(Ctx);
}

const cgpass_opts::CGPassSched1RegOpts *
cgpass_opts::getCGPassSched1Reg(const OptionsContext &Ctx) {
  return getViewOrRuntime<&CGPassSched1Reg>(Ctx);
}

const cgpass_opts::CGPassSched2RegOpts *
cgpass_opts::getCGPassSched2Reg(const OptionsContext &Ctx) {
  return getViewOrRuntime<&CGPassSched2Reg>(Ctx);
}

const cgpass_opts::CGPassSelDAGRegOpts *
cgpass_opts::getCGPassSelDAGReg(const OptionsContext &Ctx) {
  return getViewOrRuntime<&CGPassSelDAGReg>(Ctx);
}
