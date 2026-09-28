//===- ConcurrentLinks.cpp --------------------------------------*- C++ -*-===//
//
// This file is licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// The ELF driver passes its context explicitly instead of installing it as the
// global lld context, so several ELF links may run at the same time in one
// process, each on its own thread. This is how ROCm's comgr uses LLD. Each link
// must report its diagnostics to its own streams, and a fatal error in one link
// must not affect the others.
//===----------------------------------------------------------------------===//

#ifndef LLD_DEFAULT_LD_LLD_IS_MINGW

#include "lld/Common/Driver.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Testing/Support/SupportHelpers.h"
#include "gmock/gmock.h"
#include <atomic>
#include <thread>

extern const char *TestMainArgv0;

LLD_HAS_DRIVER(elf)

namespace {
struct LinkResult {
  lld::Result result;
  std::string errs;
};
} // namespace

// --threads=1 keeps all of the link on the calling thread, as comgr does.
static LinkResult runLink(const char *inPath, const char *outPath) {
  std::vector<const char *> args{"ld.lld", "-shared", inPath,
                                 "-o",     outPath,   "--threads=1"};
  LinkResult r;
  llvm::raw_string_ostream errs(r.errs);
  r.result =
      lld::lldMain(args, llvm::nulls(), errs, {{lld::Gnu, &lld::elf::link}});
  return r;
}

static std::string inputPath(llvm::StringRef name) {
  llvm::SmallString<128> path =
      llvm::unittest::getInputFileDirectory(TestMainArgv0);
  llvm::sys::path::append(path, name);
  return std::string(path);
}

TEST(AsLib, ConcurrentELF) {
  const std::string kernels[] = {inputPath("kernel1.o"),
                                 inputPath("kernel2.o")};

  // A relocatable ELF64 object cut off inside its header is a fatal error.
  int truncatedFD;
  llvm::SmallString<128> truncated;
  ASSERT_FALSE(llvm::sys::fs::createTemporaryFile("truncated", "o", truncatedFD,
                                                  truncated));
  llvm::FileRemover removeTruncated(truncated);
  {
    std::string header(18, '\0');
    header.replace(0, 6, "\177ELF\2\1"); // ELFCLASS64, ELFDATA2LSB
    header[16] = 1;                      // ET_REL
    llvm::raw_fd_ostream os(truncatedFD, /*shouldClose=*/true);
    os << header;
  }

  // Without crash recovery, a fatal error exits the process.
  llvm::CrashRecoveryContext::Enable();
  llvm::scope_exit disableCrashRecovery(
      [] { llvm::CrashRecoveryContext::Disable(); });

  // The first link registers the LLVM targets, which is not thread-safe.
  {
    llvm::SmallString<128> out;
    ASSERT_FALSE(llvm::sys::fs::createTemporaryFile("warmup", "so", out));
    llvm::FileRemover removeOut(out);
    LinkResult r = runLink(kernels[0].c_str(), out.c_str());
    ASSERT_EQ(r.result.retCode, 0) << r.errs;
  }

  constexpr unsigned numThreads = 8;
  constexpr unsigned numIterations = 10;
  std::atomic<unsigned> failures{0};
  std::vector<std::thread> threads;
  for (unsigned t = 0; t < numThreads; ++t) {
    threads.emplace_back([&, t] {
      auto fail = [&](const llvm::Twine &msg) {
        ++failures;
        llvm::errs() << "thread " << t << ": " << msg << "\n";
      };
      for (unsigned i = 0; i < numIterations; ++i) {
        llvm::SmallString<128> out;
        if (llvm::sys::fs::createTemporaryFile("concurrent", "so", out)) {
          fail("cannot create a temporary file");
          return;
        }
        llvm::FileRemover removeOut(out);

        // Successful links must not see another link's diagnostics.
        const std::string &in = kernels[(t + i) % 2];
        LinkResult r = runLink(in.c_str(), out.c_str());
        if (r.result.retCode != 0 || !r.result.canRunAgain || !r.errs.empty())
          fail("link of " + in + " failed: " + r.errs);

        // A link that reports an ordinary error returns normally.
        std::string missing = (out + ".missing").str();
        r = runLink(missing.c_str(), out.c_str());
        if (r.result.retCode == 0 || !r.result.canRunAgain ||
            !llvm::StringRef(r.errs).contains("cannot open " + missing))
          fail("missing input: " + r.errs);

        // A fatal error unwinds the link, which must leave the other threads'
        // links intact.
        if (i % 3 == 0) {
          r = runLink(truncated.c_str(), out.c_str());
          if (r.result.retCode == 0 || r.result.canRunAgain ||
              !llvm::StringRef(r.errs).contains(truncated))
            fail("truncated input: " + r.errs);
        }
      }
    });
  }
  for (std::thread &thread : threads)
    thread.join();
  EXPECT_EQ(failures, 0u);
}
#endif
