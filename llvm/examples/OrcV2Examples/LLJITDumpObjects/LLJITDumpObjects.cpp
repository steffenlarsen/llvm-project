//===----- LLJITDumpObjects.cpp - How to dump JIT'd objects with LLJIT ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringMap.h"
#include "llvm/ExecutionEngine/Orc/DebugUtils.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ObjectTransformLayer.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#include "../ExampleModules.h"

using namespace llvm;
using namespace llvm::orc;

ExitOnError ExitOnErr;

static constexpr clv2::OptionInfo<bool> DumpJITdObjectsOpt{
    "dump-jitted-objects", "dump jitted objects", clv2::Init{true}};
static constexpr clv2::OptionInfo<std::string> DumpDirOpt{
    "dump-dir", "directory to dump objects to"};
static constexpr clv2::OptionInfo<std::string> DumpFileStemOpt{
    "dump-file-stem", "Override default dump names"};

static constexpr clv2::OptionsRegistry<&DumpJITdObjectsOpt, &DumpDirOpt,
                                       &DumpFileStemOpt>
    DumpObjectsReg;

int main(int argc, char *argv[]) {
  // Initialize LLVM.
  InitLLVM X(argc, argv);

  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();

  clv2::OptionParser P;
  P.add<&DumpObjectsReg>();
  RegisterAllLLVMOptions(P);
  auto OptsCtx = P.parse(argc, argv, "LLJITDumpObjects");
  auto *Opts = OptsCtx->getViewPtr<&DumpObjectsReg>();
  ExitOnErr.setBanner(std::string(argv[0]) + ": ");

  outs()
      << "Usage notes:\n"
         "  Use -debug-only=orc on debug builds to see log messages of objects "
         "being dumped\n"
         "  Specify -dump-dir to specify a dump directory\n"
         "  Specify -dump-file-stem to override the dump file stem\n"
         "  Specify -dump-jitted-objects=false to disable dumping\n";

  auto J = ExitOnErr(LLJITBuilder().create());

  if (Opts->get<&DumpJITdObjectsOpt>())
    J->getObjTransformLayer().setTransform(
        DumpObjects(std::string(Opts->get<&DumpDirOpt>()),
                    std::string(Opts->get<&DumpFileStemOpt>())));

  auto M = ExitOnErr(parseExampleModule(Add1Example, "add1"));

  ExitOnErr(J->addIRModule(std::move(M)));

  // Look up the JIT'd function, cast it to a function pointer, then call it.
  auto Add1Addr = ExitOnErr(J->lookup("add1"));
  int (*Add1)(int) = Add1Addr.toPtr<int(int)>();

  int Result = Add1(42);
  outs() << "add1(42) = " << Result << "\n";

  return 0;
}
