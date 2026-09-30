//===- HostExecutor.cpp -----------------------------------------*- C++ -*-===//
//
// This file is licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Links run on an executor owned by the host application must produce the same
// output as links run on a pool of their own.
//===----------------------------------------------------------------------===//

// When this flag is on, we actually need the MinGW driver library, not the
// ELF one. Here our test only covers the case where the ELF driver is linked
// into the unit test binary.
#ifndef LLD_DEFAULT_LD_LLD_IS_MINGW

#include "lld/Common/Driver.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/Path.h"
#include "gmock/gmock.h"

LLD_HAS_DRIVER(elf)

static std::string getInput(const char *name) {
  llvm::SmallString<256> path;
  path.append(getenv("LLD_SRC_DIR"));
  llvm::sys::path::append(path, "unittests", "AsLibELF", "Inputs", name);
  return std::string(path);
}

// Links `input` into a shared object and returns its contents, or an empty
// string on failure.
static std::string link(const std::string &input,
                        llvm::parallel::Executor *executor,
                        const char *threadsArg = nullptr) {
  llvm::SmallString<128> output;
  if (llvm::sys::fs::createTemporaryFile("kernel", "hsaco", output))
    return "";
  llvm::FileRemover cleanup(output);

  std::vector<const char *> args{"ld.lld", "-shared", input.c_str(), "-o",
                                 output.c_str()};
  if (threadsArg)
    args.push_back(threadsArg);
  lld::Result r = lld::lldMain(args, llvm::outs(), llvm::errs(),
                               {{lld::Gnu, &lld::elf::link}}, executor);
  if (r.retCode || !r.canRunAgain)
    return "";

  auto buf = llvm::MemoryBuffer::getFile(output);
  if (!buf)
    return "";
  return (*buf)->getBuffer().str();
}

TEST(AsLib, HostExecutor) {
  std::string input = getInput("kernel1.o");
  std::string expected = link(input, /*executor=*/nullptr);
  ASSERT_FALSE(expected.empty());

  llvm::parallel::Executor executor(llvm::hardware_concurrency(4));
  for (const char *threadsArg :
       {(const char *)nullptr, "--threads=1", "--threads=2", "--threads=64"}) {
    SCOPED_TRACE(threadsArg ? threadsArg : "no --threads");
    EXPECT_EQ(link(input, &executor, threadsArg), expected);
  }
}

// Each link without a host executor creates and destroys its own pool, along
// with the objects its threads allocated.
TEST(AsLib, OwnedExecutor) {
  std::string input = getInput("kernel1.o");
  std::string expected = link(input, /*executor=*/nullptr);
  ASSERT_FALSE(expected.empty());

  for (const char *threadsArg : {(const char *)nullptr, "--threads=1",
                                 "--threads=2", (const char *)nullptr}) {
    SCOPED_TRACE(threadsArg ? threadsArg : "no --threads");
    EXPECT_EQ(link(input, /*executor=*/nullptr, threadsArg), expected);
  }
}
#endif
