//===- llvm-offload-wrapper: Create runtime registration code for devices -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Provides a utility for generating runtime registration code for device code.
// We take a binary image (CUDA fatbinary, HIP offload bundle, LLVM binary) and
// create a new IR module that calls the respective runtime to load it on the
// device.
//
//===----------------------------------------------------------------------===//

#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Frontend/Offloading/OffloadWrapper.h"
#include "llvm/Frontend/Offloading/Utility.h"
#include "llvm/Object/OffloadBinary.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/WithColor.h"
#include "llvm/TargetParser/Host.h"

using namespace llvm;
using namespace llvm::clv2;

static constexpr OptionCategory OffloadWrapperCategory{
    "llvm-offload-wrapper options"};

static constexpr EnumVal<object::OffloadKind> KindVals[] = {
    {"openmp", object::OFK_OpenMP, "Wrap OpenMP binaries"},
    {"cuda", object::OFK_Cuda, "Wrap CUDA binaries"},
    {"hip", object::OFK_HIP, "Wrap HIP binaries"},
    {"sycl", object::OFK_SYCL, "Wrap SYCL binaries"},
};

static constexpr auto Kind = makeEnumOption<object::OffloadKind>(
    "kind", "Wrap for offload kind:", KindVals, Required,
    cat(OffloadWrapperCategory));

static constexpr OptionInfo<std::string> OutputFile{
    "o", "Write output to <file>.", value_desc("file"),
    cat(OffloadWrapperCategory)};

static constexpr ListOptionInfo<std::string> InputFiles{
    "inputs",  "Wrap input from <file>",   Positional{}, value_desc("file"),
    OneOrMore, cat(OffloadWrapperCategory)};

static constexpr OptionInfo<bool> Relocatable{
    "relocatable",
    "Wrap for a relocatable offloading application (OpenMP only)",
    cat(OffloadWrapperCategory)};

// Default is set at runtime to sys::getDefaultTargetTriple().
static constexpr OptionInfo<std::string> TheTriple{
    "triple", "Target triple for the wrapper module",
    cat(OffloadWrapperCategory)};

static constexpr OptionsRegistry<&Kind, &OutputFile, &InputFiles, &Relocatable,
                                 &TheTriple>
    OffloadWrapperReg;

static Error wrapImages(object::OffloadKind OffloadKind, StringRef Triple,
                        StringRef OutFile,
                        ArrayRef<ArrayRef<char>> BuffersToWrap,
                        bool IsRelocatable,
                        const clv2::OptionsContext &OptsCtx) {
  if (BuffersToWrap.size() > 1 && (OffloadKind == llvm::object::OFK_Cuda ||
                                   OffloadKind == llvm::object::OFK_HIP))
    return createStringError(
        "CUDA / HIP offloading uses a single fatbinary or offload bundle");

  LLVMContext Context(OptsCtx);
  Module M("offload.wrapper.module", Context);
  M.setTargetTriple(llvm::Triple(Triple));

  switch (OffloadKind) {
  case llvm::object::OFK_OpenMP:
    if (Error Err = offloading::wrapOpenMPBinaries(
            M, BuffersToWrap, offloading::getOffloadEntryArray(M),
            /*Suffix=*/"", /*Relocatable=*/IsRelocatable))
      return Err;
    break;
  case llvm::object::OFK_Cuda:
    if (Error Err = offloading::wrapCudaBinary(
            M, BuffersToWrap.front(), offloading::getOffloadEntryArray(M),
            /*Suffix=*/"", /*EmitSurfacesAndTextures=*/false))
      return Err;
    break;
  case llvm::object::OFK_HIP:
    if (Error Err = offloading::wrapHIPBinary(
            M, BuffersToWrap.front(), offloading::getOffloadEntryArray(M)))
      return Err;
    break;
  case llvm::object::OFK_SYCL:
    if (Error Err = offloading::wrapSYCLBinaries(M, BuffersToWrap.front()))
      return Err;
    break;
  default:
    return createStringError(getOffloadKindName(OffloadKind) +
                             " wrapping is not supported");
  }

  int FD = -1;
  if (std::error_code EC = sys::fs::openFileForWrite(OutFile, FD))
    return errorCodeToError(EC);
  llvm::raw_fd_ostream OS(FD, true);
  WriteBitcodeToFile(M, OS);

  return Error::success();
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  clv2::OptionParser P;
  P.add<&OffloadWrapperReg>();
  RegisterAllLLVMOptions(P);
  P.hideUnrelatedOptions({&OffloadWrapperCategory});
  auto OptsCtx =
      P.parse(argc, argv,
              "Generate runtime registration code for a device binary image\n");
  auto *Opts = OptsCtx->getViewPtr<&OffloadWrapperReg>();

  auto ReportError = [argv](Error E) {
    logAllUnhandledErrors(std::move(E), WithColor::error(errs(), argv[0]));
    exit(EXIT_FAILURE);
  };

  // Default triple to host if not specified.
  std::string Triple = Opts->get<&TheTriple>();
  if (Triple.empty())
    Triple = sys::getDefaultTargetTriple();

  const std::vector<std::string> &Inputs = Opts->get<&InputFiles>();
  SmallVector<std::unique_ptr<MemoryBuffer>> Buffers;
  SmallVector<ArrayRef<char>> BuffersToWrap;
  for (StringRef Input : Inputs) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrErr =
        MemoryBuffer::getFileOrSTDIN(Input);
    if (std::error_code EC = BufferOrErr.getError())
      ReportError(createFileError(Input, EC));
    std::unique_ptr<MemoryBuffer> &Buffer =
        Buffers.emplace_back(std::move(*BufferOrErr));
    BuffersToWrap.emplace_back(
        ArrayRef<char>(Buffer->getBufferStart(), Buffer->getBufferSize()));
  }

  if (Error Err =
          wrapImages(Opts->get<&Kind>(), Triple, Opts->get<&OutputFile>(),
                     BuffersToWrap, Opts->get<&Relocatable>(), *OptsCtx))
    ReportError(std::move(Err));

  return EXIT_SUCCESS;
}
