//===- MultiTargetInternal.h - Helpers shared by multi-target AST code ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Helpers the files implementing multi-target compilation in the AST library
// share. Not installed.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_AST_MULTITARGETINTERNAL_H
#define LLVM_CLANG_LIB_AST_MULTITARGETINTERNAL_H

#include "clang/AST/ASTContext.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Hashing.h"
#include <string>

namespace clang {

class CXXRecordDecl;
class Decl;
class FunctionDecl;
class NamedDecl;
class RecordDecl;
class SourceManager;
class Stmt;
struct PrintingPolicy;

namespace multitarget {

// Shapes, see TargetShape.cpp.
llvm::hash_code bodyShape(const SourceManager &SM, const Stmt *S);
llvm::hash_code defaultArgumentShape(const SourceManager &SM,
                                     const NamedDecl *Param);
llvm::hash_code definitionShape(const SourceManager &SM,
                                const FunctionDecl *Def);
llvm::hash_code recordShape(const ASTContext &Ctx, const CXXRecordDecl *RD,
                            bool StaticInitializers = true);
std::string layoutAttributes(const ASTContext &Ctx, const RecordDecl *RD);
std::string dependentUsingScope(const ASTContext &Ctx, const Decl *D);

// Target copies, see TargetCopies.cpp.
bool isWidenedCopy(const Decl *D);
const NamedDecl *copyForVariant(ArrayRef<const NamedDecl *> Copies,
                                unsigned Variant);
bool isHostsCopy(const ASTContext &Ctx, const NamedDecl *Copy);
bool isDevicesCopy(const ASTContext &Ctx, const NamedDecl *Copy);
TargetPartition sourceTargetPartition(const ASTContext &Ctx,
                                      const NamedDecl *ND);

// Printing, see DeclFingerprint.cpp.
void printTemplateArgs(raw_ostream &OS, const NamedDecl *D,
                       const PrintingPolicy &Policy);

} // namespace multitarget
} // namespace clang

#endif // LLVM_CLANG_LIB_AST_MULTITARGETINTERNAL_H
