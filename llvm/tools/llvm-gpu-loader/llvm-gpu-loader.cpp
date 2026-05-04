//===-- Main entry into the loader interface ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This utility is used to launch standard programs onto the GPU in conjunction
// with the LLVM 'libc' project. It is designed to mimic a standard emulator
// workflow, allowing for unit tests to be run on the GPU directly.
//
//===----------------------------------------------------------------------===//

#include "llvm-gpu-loader.h"

#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Object/ELF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/WithColor.h"
#include "llvm/TargetParser/Triple.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace llvm;
using namespace llvm::clv2;

static constexpr OptionCategory LoaderCategory{"loader options"};

static constexpr OptionInfo<unsigned> ThreadsX{
    "threads-x", "Number of threads in the 'x' dimension", Init{1u},
    cat(LoaderCategory)};
static constexpr OptionInfo<unsigned> ThreadsY{
    "threads-y", "Number of threads in the 'y' dimension", Init{1u},
    cat(LoaderCategory)};
static constexpr OptionInfo<unsigned> ThreadsZ{
    "threads-z", "Number of threads in the 'z' dimension", Init{1u},
    cat(LoaderCategory)};
static constexpr AliasInfo ThreadsA{"threads", "threads-x"};

static constexpr OptionInfo<unsigned> BlocksX{
    "blocks-x", "Number of blocks in the 'x' dimension", Init{1u},
    cat(LoaderCategory)};
static constexpr OptionInfo<unsigned> BlocksY{
    "blocks-y", "Number of blocks in the 'y' dimension", Init{1u},
    cat(LoaderCategory)};
static constexpr OptionInfo<unsigned> BlocksZ{
    "blocks-z", "Number of blocks in the 'z' dimension", Init{1u},
    cat(LoaderCategory)};
static constexpr AliasInfo BlocksA{"blocks", "blocks-x"};

static constexpr OptionInfo<std::string> File{
    "file", "<gpu executable>", Positional{}, Required, cat(LoaderCategory)};
static constexpr ListOptionInfo<std::string> Args{
    "args", "<program arguments>...", ConsumeAfter, cat(LoaderCategory)};

static constexpr ListOptionInfo<std::string> Kernels{
    "kernel", "Launch '<name>(void)' instead of the 'main' entry point.",
    value_desc("name"), cat(LoaderCategory)};

static constexpr OptionsRegistry<&ThreadsX, &ThreadsY, &ThreadsZ, &ThreadsA,
                                 &BlocksX, &BlocksY, &BlocksZ, &BlocksA, &File,
                                 &Args, &Kernels>
    GpuLoaderReg;

[[noreturn]] static void handleError(Error E) {
  outs().flush();
  logAllUnhandledErrors(std::move(E), WithColor::error(errs(), "loader"));
  exit(EXIT_FAILURE);
}

[[noreturn]] static void handleError(ol_result_t Err, unsigned Line) {
  fprintf(stderr, "%s:%d %s\n", __FILE__, Line, Err->Details);
  exit(EXIT_FAILURE);
}

#define OFFLOAD_ERR(X)                                                         \
  if (ol_result_t Err = X)                                                     \
    handleError(Err, __LINE__);

static void *copyArgumentVector(int Argc, const char **Argv,
                                ol_device_handle_t Device) {
  size_t ArgSize = sizeof(char *) * (Argc + 1);
  size_t StringLen = 0;
  for (int i = 0; i < Argc; ++i)
    StringLen += strlen(Argv[i]) + 1;

  // We allocate enough space for a null terminated array and all the strings.
  void *DevArgv;
  OFFLOAD_ERR(olMemAllocHost(Device, ArgSize + StringLen, &DevArgv));
  if (!DevArgv)
    handleError(
        createStringError("Failed to allocate memory for environment."));

  // Store the strings linerally in the same memory buffer.
  void *DevString = reinterpret_cast<uint8_t *>(DevArgv) + ArgSize;
  for (int i = 0; i < Argc; ++i) {
    size_t size = strlen(Argv[i]) + 1;
    std::memcpy(DevString, Argv[i], size);
    static_cast<void **>(DevArgv)[i] = DevString;
    DevString = reinterpret_cast<uint8_t *>(DevString) + size;
  }

  // Ensure the vector is null terminated.
  reinterpret_cast<void **>(DevArgv)[Argc] = nullptr;
  return DevArgv;
}

void *copyEnvironment(const char **Envp, ol_device_handle_t Device) {
  int Envc = 0;
  for (const char **Env = Envp; *Env != 0; ++Env)
    ++Envc;

  return copyArgumentVector(Envc, Envp, Device);
}

ol_device_handle_t findDevice(MemoryBufferRef Binary) {
  ol_device_handle_t Device = nullptr;
  std::tuple Data = std::make_tuple(&Device, &Binary);
  OFFLOAD_ERR(olIterateDevices(
      [](ol_device_handle_t Device, void *UserData) {
        auto &[Output, Binary] = *reinterpret_cast<decltype(Data) *>(UserData);
        bool IsValid = false;
        OFFLOAD_ERR(olIsValidBinary(Device, Binary->getBufferStart(),
                                    Binary->getBufferSize(), &IsValid));
        if (!IsValid)
          return true;

        *Output = Device;
        return false;
      },
      &Data));
  return Device;
}

ol_device_handle_t getHostDevice() {
  ol_device_handle_t Device;
  OFFLOAD_ERR(olIterateDevices(
      [](ol_device_handle_t Device, void *UserData) {
        ol_platform_handle_t Platform;
        olGetDeviceInfo(Device, OL_DEVICE_INFO_PLATFORM, sizeof(Platform),
                        &Platform);
        ol_platform_backend_t Backend;
        olGetPlatformInfo(Platform, OL_PLATFORM_INFO_BACKEND, sizeof(Backend),
                          &Backend);

        auto &Output = *reinterpret_cast<decltype(Device) *>(UserData);
        if (Backend == OL_PLATFORM_BACKEND_HOST) {
          Output = Device;
          return false;
        }
        return true;
      },
      &Device));
  return Device;
}

template <typename... Args>
void launchKernel(ol_queue_handle_t Queue, ol_device_handle_t Device,
                  ol_program_handle_t Program, const char *Name,
                  ol_kernel_launch_size_args_t LaunchArgs,
                  Args &...KernelArgs) {
  ol_symbol_handle_t Kernel;
  OFFLOAD_ERR(olGetSymbol(Program, Name, OL_SYMBOL_KIND_KERNEL, &Kernel));

  if constexpr (sizeof...(Args) == 0) {
    OFFLOAD_ERR(olLaunchKernel(Queue, Device, Kernel, &LaunchArgs, nullptr, 0,
                               nullptr, nullptr));
  } else {
    void *ArgPtrs[] = {static_cast<void *>(&KernelArgs)...};
    size_t ArgSizes[] = {sizeof(KernelArgs)...};
    OFFLOAD_ERR(olLaunchKernel(Queue, Device, Kernel, &LaunchArgs, nullptr,
                               sizeof...(Args), ArgPtrs, ArgSizes));
  }
}

int main(int argc, const char **argv, const char **envp) {
  sys::PrintStackTraceOnErrorSignal(argv[0]);

  clv2::OptionParser P;
  P.add<&GpuLoaderReg>();
  RegisterCoreLLVMOptions(P);
  P.hideUnrelatedOptions({&LoaderCategory});
  auto OptsCtx = P.parse(
      argc, argv,
      "A utility used to launch unit tests built for a GPU target. This is\n"
      "intended to provide an interface similar to cross-compiling "
      "emulators\n");
  auto *Opts = OptsCtx->getViewPtr<&GpuLoaderReg>();

  const std::string &FileVal = Opts->get<&File>();
  const std::vector<std::string> &ArgsVal = Opts->get<&Args>();
  unsigned BlocksXVal = Opts->get<&BlocksX>();
  unsigned BlocksYVal = Opts->get<&BlocksY>();
  unsigned BlocksZVal = Opts->get<&BlocksZ>();
  unsigned ThreadsXVal = Opts->get<&ThreadsX>();
  unsigned ThreadsYVal = Opts->get<&ThreadsY>();
  unsigned ThreadsZVal = Opts->get<&ThreadsZ>();
  const std::vector<std::string> &KernelsVal = Opts->get<&Kernels>();

  if (Error Err = loadLLVMOffload())
    handleError(std::move(Err));

  ErrorOr<std::unique_ptr<MemoryBuffer>> ImageOrErr =
      MemoryBuffer::getFileOrSTDIN(FileVal);
  if (std::error_code EC = ImageOrErr.getError())
    handleError(errorCodeToError(EC));
  MemoryBufferRef Image = **ImageOrErr;

  ol_platform_backend_t Backend = OL_PLATFORM_BACKEND_UNKNOWN;
  ol_init_args_t InitArgs = OL_INIT_ARGS_INIT;

  file_magic Magic = identify_magic(Image.getBuffer());
  if (Magic >= file_magic::elf && Magic <= file_magic::elf_core) {
    Expected<object::ELFFile<object::ELF64LE>> ElfOrErr =
        object::ELFFile<object::ELF64LE>::create(Image.getBuffer());
    if (!ElfOrErr)
      handleError(ElfOrErr.takeError());

    switch (ElfOrErr->getHeader().e_machine) {
    case ELF::EM_AMDGPU:
      Backend = OL_PLATFORM_BACKEND_AMDGPU;
      break;
    case ELF::EM_CUDA:
      Backend = OL_PLATFORM_BACKEND_CUDA;
      break;
    default:
      handleError(createStringError(
          "unhandled ELF architecture: %s",
          ELF::convertEMachineToArchName(ElfOrErr->getHeader().e_machine)
              .data()));
    }
  }

  if (Backend != OL_PLATFORM_BACKEND_UNKNOWN) {
    InitArgs.NumPlatforms = 1;
    InitArgs.Platforms = &Backend;
  }

  SmallVector<const char *> NewArgv = {FileVal.c_str()};
  llvm::transform(ArgsVal, std::back_inserter(NewArgv),
                  [](const std::string &Arg) { return Arg.c_str(); });

  OFFLOAD_ERR(olInit(&InitArgs));
  ol_device_handle_t Device = findDevice(Image);
  if (!Device)
    handleError(createStringError("No compatible device was found"));
  ol_device_handle_t Host = getHostDevice();
  assert(Host && "Host device should always be present");

  ol_context_handle_t Context;
  OFFLOAD_ERR(olCreateContext(1, &Device, &Context));

  ol_program_handle_t Program;
  OFFLOAD_ERR(olCreateProgram(Device, Image.getBufferStart(),
                              Image.getBufferSize(), &Program));

  ol_queue_handle_t Queue;
  OFFLOAD_ERR(olCreateQueue(Context, Device, &Queue));

  int DevArgc = static_cast<int>(NewArgv.size());
  void *DevArgv = copyArgumentVector(NewArgv.size(), NewArgv.begin(), Device);
  void *DevEnvp = copyEnvironment(envp, Device);

  void *DevRet;
  int Zero = 0;
  OFFLOAD_ERR(olMemAlloc(Device, OL_ALLOC_TYPE_DEVICE, sizeof(int), &DevRet));
  OFFLOAD_ERR(olMemcpy(Queue, DevRet, Device, &Zero, Host, sizeof(int)));

  uint32_t Dims = (BlocksZVal > 1) ? 3 : (BlocksYVal > 1) ? 2 : 1;
  ol_kernel_launch_size_args_t StartLaunch{
      Dims,
      {BlocksXVal, BlocksYVal, BlocksZVal},
      {ThreadsXVal, ThreadsYVal, ThreadsZVal},
      /*SharedMemBytes=*/0};
  if (!KernelsVal.empty()) {
    // Launch the user-specified kernels in order. These must take no arguments.
    for (const std::string &Kernel : KernelsVal)
      launchKernel(Queue, Device, Program, Kernel.c_str(), StartLaunch);
  } else {
    // The '_begin' and '_end' kernels perform libc startup and teardown. Global
    // constructors and destructors are handled automatically by the runtime.
    ol_kernel_launch_size_args_t BeginLaunch{1, {1, 1, 1}, {1, 1, 1}, 0};
    launchKernel(Queue, Device, Program, "_begin", BeginLaunch, DevArgc,
                 DevArgv, DevEnvp);
    OFFLOAD_ERR(olSyncQueue(Queue));

    launchKernel(Queue, Device, Program, "_start", StartLaunch, DevArgc,
                 DevArgv, DevEnvp, DevRet);

    ol_kernel_launch_size_args_t EndLaunch{1, {1, 1, 1}, {1, 1, 1}, 0};
    launchKernel(Queue, Device, Program, "_end", EndLaunch);
  }

  int Ret;
  OFFLOAD_ERR(olMemcpy(Queue, &Ret, Host, DevRet, Device, sizeof(int)));
  OFFLOAD_ERR(olSyncQueue(Queue));

  OFFLOAD_ERR(olMemFree(DevRet));
  OFFLOAD_ERR(olMemFree(DevArgv));
  OFFLOAD_ERR(olMemFree(DevEnvp));
  OFFLOAD_ERR(olDestroyQueue(Queue));
  OFFLOAD_ERR(olDestroyContext(Context));
  OFFLOAD_ERR(olDestroyProgram(Program));
  OFFLOAD_ERR(olShutDown());

  return Ret;
}
