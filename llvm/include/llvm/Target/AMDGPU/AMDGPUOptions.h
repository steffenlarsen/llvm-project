#ifndef LLVM_TARGET_AMDGPU_AMDGPUOPTIONS_H
#define LLVM_TARGET_AMDGPU_AMDGPUOPTIONS_H
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/CGPassBuilderOption.h"
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
#include "llvm/Target/AMDGPU/AMDGPUOptions.inc"
#undef OPTIONS_STRUCT_DECL
#endif
