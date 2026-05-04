//===- llvm/Support/CommandLineTokenizer.h - Tokenizer & response files ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Standalone command line tokenization and response file expansion utilities.
//
// These functions have NO dependency on cl::opt, cl::Option, or any of the
// cl:: registration machinery.  They were extracted from CommandLine.h so that
// lightweight clients (e.g. CommandLineV2.h) can use tokenization without
// pulling in the full cl:: option framework.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_COMMANDLINETOKENIZER_H
#define LLVM_SUPPORT_COMMANDLINETOKENIZER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/StringSaver.h"

#include "llvm/Support/Allocator.h"
#include <cstddef>

namespace llvm {

namespace vfs {
class FileSystem;
} // namespace vfs

namespace cl {

//===----------------------------------------------------------------------===//
// Standalone command line processing utilities.
//

/// Tokenizes a command line that can contain escapes and quotes.
//
/// The quoting rules match those used by GCC and other tools that use
/// libiberty's buildargv() or expandargv() utilities, and do not match bash.
/// They differ from buildargv() on treatment of backslashes that do not escape
/// a special character to make it possible to accept most Windows file paths.
///
/// \param [in] Source The string to be split on whitespace with quotes.
/// \param [in] Saver Delegates back to the caller for saving parsed strings.
/// \param [in] MarkEOLs true if tokenizing a response file and you want end of
/// lines and end of the response file to be marked with a nullptr string.
/// \param [out] NewArgv All parsed strings are appended to NewArgv.
LLVM_ABI void TokenizeGNUCommandLine(StringRef Source, StringSaver &Saver,
                                     SmallVectorImpl<const char *> &NewArgv,
                                     bool MarkEOLs = false);

/// Tokenizes a string of Windows command line arguments, which may contain
/// quotes and escaped quotes.
///
/// See MSDN docs for CommandLineToArgvW for information on the quoting rules.
/// http://msdn.microsoft.com/en-us/library/windows/desktop/17w5ykft(v=vs.85).aspx
///
/// For handling a full Windows command line including the executable name at
/// the start, see TokenizeWindowsCommandLineFull below.
///
/// \param [in] Source The string to be split on whitespace with quotes.
/// \param [in] Saver Delegates back to the caller for saving parsed strings.
/// \param [in] MarkEOLs true if tokenizing a response file and you want end of
/// lines and end of the response file to be marked with a nullptr string.
/// \param [out] NewArgv All parsed strings are appended to NewArgv.
LLVM_ABI void TokenizeWindowsCommandLine(StringRef Source, StringSaver &Saver,
                                         SmallVectorImpl<const char *> &NewArgv,
                                         bool MarkEOLs = false);

/// Tokenizes a Windows command line while attempting to avoid copies. If no
/// quoting or escaping was used, this produces substrings of the original
/// string. If a token requires unquoting, it will be allocated with the
/// StringSaver.
LLVM_ABI void
TokenizeWindowsCommandLineNoCopy(StringRef Source, StringSaver &Saver,
                                 SmallVectorImpl<StringRef> &NewArgv);

/// Tokenizes a Windows full command line, including command name at the start.
///
/// This uses the same syntax rules as TokenizeWindowsCommandLine for all but
/// the first token. But the first token is expected to be parsed as the
/// executable file name in the way CreateProcess would do it, rather than the
/// way the C library startup code would do it: CreateProcess does not consider
/// that \ is ever an escape character (because " is not a valid filename char,
/// hence there's never a need to escape it to be used literally).
///
/// Parameters are the same as for TokenizeWindowsCommandLine. In particular,
/// if you set MarkEOLs = true, then the first word of every line will be
/// parsed using the special rules for command names, making this function
/// suitable for parsing a file full of commands to execute.
LLVM_ABI void
TokenizeWindowsCommandLineFull(StringRef Source, StringSaver &Saver,
                               SmallVectorImpl<const char *> &NewArgv,
                               bool MarkEOLs = false);

/// String tokenization function type.  Should be compatible with either
/// Windows or Unix command line tokenizers.
using TokenizerCallback = void (*)(StringRef Source, StringSaver &Saver,
                                   SmallVectorImpl<const char *> &NewArgv,
                                   bool MarkEOLs);

/// Tokenizes content of configuration file.
///
/// \param [in] Source The string representing content of config file.
/// \param [in] Saver Delegates back to the caller for saving parsed strings.
/// \param [out] NewArgv All parsed strings are appended to NewArgv.
/// \param [in] MarkEOLs Added for compatibility with TokenizerCallback.
///
/// It works like TokenizeGNUCommandLine with ability to skip comment lines.
///
LLVM_ABI void tokenizeConfigFile(StringRef Source, StringSaver &Saver,
                                 SmallVectorImpl<const char *> &NewArgv,
                                 bool MarkEOLs = false);

/// Contains options that control response file expansion.
class ExpansionContext {
  /// Provides persistent storage for parsed strings.
  StringSaver Saver;

  /// Tokenization strategy. Typically Unix or Windows.
  TokenizerCallback Tokenizer;

  /// File system used for all file access when running the expansion.
  vfs::FileSystem *FS;

  /// Path used to resolve relative rsp files. If empty, the file system
  /// current directory is used instead.
  StringRef CurrentDir;

  /// Directories used for search of config files.
  ArrayRef<StringRef> SearchDirs;

  /// True if names of nested response files must be resolved relative to
  /// including file.
  bool RelativeNames = false;

  /// If true, mark end of lines and the end of the response file with nullptrs
  /// in the Argv vector.
  bool MarkEOLs = false;

  /// If true, body of config file is expanded.
  bool InConfigFile = false;

  llvm::Error expandResponseFile(StringRef FName,
                                 SmallVectorImpl<const char *> &NewArgv);

public:
  LLVM_ABI ExpansionContext(BumpPtrAllocator &A, TokenizerCallback T,
                            vfs::FileSystem *FS = nullptr);

  ExpansionContext &setMarkEOLs(bool X) {
    MarkEOLs = X;
    return *this;
  }

  ExpansionContext &setRelativeNames(bool X) {
    RelativeNames = X;
    return *this;
  }

  ExpansionContext &setCurrentDir(StringRef X) {
    CurrentDir = X;
    return *this;
  }

  ExpansionContext &setSearchDirs(ArrayRef<StringRef> X) {
    SearchDirs = X;
    return *this;
  }

  ExpansionContext &setVFS(vfs::FileSystem *X) {
    FS = X;
    return *this;
  }

  /// Looks for the specified configuration file.
  ///
  /// \param[in]  FileName Name of the file to search for.
  /// \param[out] FilePath File absolute path, if it was found.
  /// \return True if file was found.
  ///
  /// If the specified file name contains a directory separator, it is searched
  /// for by its absolute path. Otherwise looks for file sequentially in
  /// directories specified by SearchDirs field.
  LLVM_ABI bool findConfigFile(StringRef FileName,
                               SmallVectorImpl<char> &FilePath);

  /// Reads command line options from the given configuration file.
  ///
  /// \param [in] CfgFile Path to configuration file.
  /// \param [out] Argv Array to which the read options are added.
  /// \return true if the file was successfully read.
  ///
  /// It reads content of the specified file, tokenizes it and expands "@file"
  /// commands resolving file names in them relative to the directory where
  /// CfgFilename resides. It also expands "<CFGDIR>" to the base path of the
  /// current config file.
  LLVM_ABI Error readConfigFile(StringRef CfgFile,
                                SmallVectorImpl<const char *> &Argv);

  /// Expands constructs "@file" in the provided array of arguments recursively.
  LLVM_ABI Error expandResponseFiles(SmallVectorImpl<const char *> &Argv);
};

/// A convenience helper which concatenates the options specified by the
/// environment variable EnvVar and command line options, then expands
/// response files recursively.
/// \return true if all @files were expanded successfully or there were none.
LLVM_ABI bool expandResponseFiles(int Argc, const char *const *Argv,
                                  const char *EnvVar,
                                  SmallVectorImpl<const char *> &NewArgv);

/// A convenience helper which supports the typical use case of expansion
/// function call.
LLVM_ABI bool ExpandResponseFiles(StringSaver &Saver,
                                  TokenizerCallback Tokenizer,
                                  SmallVectorImpl<const char *> &Argv);

/// A convenience helper which concatenates the options specified by the
/// environment variable EnvVar and command line options, then expands response
/// files recursively. The tokenizer is a predefined GNU or Windows one.
/// \return true if all @files were expanded successfully or there were none.
LLVM_ABI bool expandResponseFiles(int Argc, const char *const *Argv,
                                  const char *EnvVar, StringSaver &Saver,
                                  SmallVectorImpl<const char *> &NewArgv);

} // end namespace cl

} // end namespace llvm

#endif // LLVM_SUPPORT_COMMANDLINETOKENIZER_H
