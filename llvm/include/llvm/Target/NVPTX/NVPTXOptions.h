#ifndef LLVM_TARGET_NVPTX_NVPTXOPTIONS_H
#define LLVM_TARGET_NVPTX_NVPTXOPTIONS_H
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>
#include <string>
#include <vector>
namespace llvm {
namespace opt {
class Arg;
class OptTable;
} // namespace opt
} // namespace llvm
#define OPTIONS_STRUCT_DECL
#include "llvm/Target/NVPTX/NVPTXOptions.inc"
#undef OPTIONS_STRUCT_DECL
#endif
