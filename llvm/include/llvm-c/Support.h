/*===-- llvm-c/Support.h - Support C Interface --------------------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This file defines the C interface to the LLVM support library.             *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_SUPPORT_H
#define LLVM_C_SUPPORT_H

#include "llvm-c/DataTypes.h"
#include "llvm-c/ExternC.h"
#include "llvm-c/Types.h"
#include "llvm-c/Visibility.h"

LLVM_C_EXTERN_C_BEGIN

/**
 * @addtogroup LLVMCCore
 *
 * @{
 */

/**
 * This function permanently loads the dynamic library at the given path.
 * It is safe to call this function multiple times for the same library.
 *
 * @see sys::DynamicLibrary::LoadLibraryPermanently()
  */
LLVM_C_ABI LLVMBool LLVMLoadLibraryPermanently(const char *Filename);

/**
 * This function parses the given arguments using the LLVM command line parser.
 * Note that the only stable thing about this function is its signature; you
 * cannot rely on any particular set of command line arguments being interpreted
 * the same way across LLVM versions.
 *
 * @see llvm::cl::ParseCommandLineOptions()
 */
LLVM_C_ABI void LLVMParseCommandLineOptions(int argc, const char *const *argv,
                                            const char *Overview);

/**
 * Parse command-line options and return an owned OptionsContext.
 * The caller must dispose with LLVMDisposeOptionsContext().
 * The returned context can be attached to an LLVMContext via
 * LLVMContextCreateWithOptions().
 *
 * Returns NULL if the arguments could not be parsed, or if a help or version
 * option was given -- in both cases there is nothing to dispose. As with
 * LLVMParseCommandLineOptions(), parse diagnostics are suppressed; help and
 * version text is still written to stdout. This function does not terminate
 * the process.
 */
LLVM_C_ABI LLVMOptionsContextRef LLVMParseCommandLineOptions2(
    int argc, const char *const *argv, const char *Overview);

/**
 * Dispose an OptionsContext created by LLVMParseCommandLineOptions2().
 */
LLVM_C_ABI void LLVMDisposeOptionsContext(LLVMOptionsContextRef Ctx);

/**
 * This function will search through all previously loaded dynamic
 * libraries for the symbol \p symbolName. If it is found, the address of
 * that symbol is returned. If not, null is returned.
 *
 * @see sys::DynamicLibrary::SearchForAddressOfSymbol()
 */
LLVM_C_ABI void *LLVMSearchForAddressOfSymbol(const char *symbolName);

/**
 * This functions permanently adds the symbol \p symbolName with the
 * value \p symbolValue.  These symbols are searched before any
 * libraries.
 *
 * @see sys::DynamicLibrary::AddSymbol()
 */
LLVM_C_ABI void LLVMAddSymbol(const char *symbolName, void *symbolValue);

/**
 * @}
 */

LLVM_C_EXTERN_C_END

#endif
