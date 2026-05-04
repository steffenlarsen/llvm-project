//===- NewPMDriver.h - Function to drive opt with the new PM ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// A single function which is called to drive the opt behavior for the new
/// PassManager.
///
/// This is only in a separate TU with a header to avoid including all of the
/// old pass manager headers and the new pass manager headers into the same
/// file. Eventually all of the routines here will get folded back into
/// opt.cpp.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_OPT_NEWPMDRIVER_H
#define LLVM_TOOLS_OPT_NEWPMDRIVER_H

#include "llvm/ADT/ArrayRef.h"
#include <functional>
#include <string>

namespace llvm {
class PassBuilder;
class StringRef;
class Module;
class PassPlugin;
class TargetMachine;
class ToolOutputFile;
class TargetLibraryInfoImpl;
class raw_ostream;

namespace opt_tool {
enum OutputKind {
  OK_NoOutput,
  OK_OutputAssembly,
  OK_OutputBitcode,
  OK_OutputThinLTOBitcode,
};
enum class VerifierKind { None, InputOutput, EachPass };
enum PGOKind {
  NoPGO,
  InstrGen,
  InstrUse,
  SampleUse
};
enum CSPGOKind { NoCSPGO, CSInstrGen, CSInstrUse };
enum class DebugLogging { None, Normal, Verbose, Quiet };
enum class PGOColdFuncAttrKind { Default, OptSize, MinSize, OptNone };

/// All options consumed by NewPMDriver.
struct NPMOptions {
  // debugify
  bool DebugifyEach = false;
  std::string DebugifyExport;
  bool VerifyEachDebugInfoPreserve = false;
  std::string VerifyDIPreserveExport;

  // pass manager debug
  bool EnableLoopFusion = false;
  DebugLogging DebugPM = DebugLogging::None;

  // AA / EP pipelines
  std::string AAPipeline = "default";
  std::string PeepholeEPPipeline;
  std::string LateLoopOptimizationsEPPipeline;
  std::string LoopOptimizerEndEPPipeline;
  std::string ScalarOptimizerLateEPPipeline;
  std::string CGSCCOptimizerLateEPPipeline;
  std::string VectorizerStartEPPipeline;
  std::string VectorizerEndEPPipeline;
  std::string PipelineStartEPPipeline;
  std::string PipelineEarlySimplificationEPPipeline;
  std::string OptimizerEarlyEPPipeline;
  std::string OptimizerLastEPPipeline;
  std::string FullLinkTimeOptimizationEarlyEPPipeline;
  std::string FullLinkTimeOptimizationLastEPPipeline;

  bool DisablePipelineVerification = false;

  // PGO
  PGOKind PGOKindFlag = NoPGO;
  std::string ProfileFile;
  std::string MemoryProfileFile;
  CSPGOKind CSPGOKindFlag = NoCSPGO;
  std::string CSProfileGenFile;
  std::string ProfileRemappingFile;
  PGOColdFuncAttrKind PGOColdFuncAttr = PGOColdFuncAttrKind::Default;
  bool DebugInfoForProfiling = false;
  bool PseudoProbeForProfiling = false;

  bool DisableLoopUnrolling = false;
};
} // namespace opt_tool

void printPasses(raw_ostream &OS);

/// Driver function to run the new pass manager over a module.
///
/// This function only exists factored away from opt.cpp in order to prevent
/// inclusion of the new pass manager headers and the old headers into the same
/// file. It's interface is consequentially somewhat ad-hoc, but will go away
/// when the transition finishes.
///
/// ThinLTOLinkOut is only used when OK is OK_OutputThinLTOBitcode, and can be
/// nullptr.
bool runPassPipeline(
    StringRef Arg0, Module &M, TargetMachine *TM, TargetLibraryInfoImpl *TLII,
    ToolOutputFile *Out, ToolOutputFile *ThinLinkOut,
    ToolOutputFile *OptRemarkFile, StringRef PassPipeline,
    ArrayRef<PassPlugin> PassPlugins,
    ArrayRef<std::function<void(PassBuilder &)>> PassBuilderCallbacks,
    opt_tool::OutputKind OK, opt_tool::VerifierKind VK,
    bool ShouldPreserveAssemblyUseListOrder,
    bool ShouldPreserveBitcodeUseListOrder, bool EmitSummaryIndex,
    bool EmitModuleHash, bool EnableDebugify, bool VerifyDIPreserve,
    bool EnableProfcheck, bool UnifiedLTO, const opt_tool::NPMOptions &NPMOpts);
} // namespace llvm

#endif
