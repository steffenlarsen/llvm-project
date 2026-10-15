//===- KernelBindingTable.h - Host<->device kernel bindings -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Resolves the host device-stub <-> device kernel correspondence inside a
// cir.offload.container, along with the host call sites that launch each
// kernel. The bridge is the `cu.kernel_name` attribute, whose value is the
// device kernel's mangled symbol name; both the host stub and the calls to it
// carry it.
//
// Usable as an MLIR analysis: getAnalysis<KernelBindingTable>() on a pass
// anchored at the offload container module.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_CIR_DIALECT_TRANSFORMS_OFFLOADOPT_KERNELBINDINGTABLE_H
#define LLVM_CLANG_CIR_DIALECT_TRANSFORMS_OFFLOADOPT_KERNELBINDINGTABLE_H

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace llvm {
class raw_ostream;
} // namespace llvm

namespace cir {

// One host-side launch of a kernel: the call to the device stub, which carries
// the kernel arguments, and the `__cudaPushCallConfiguration` /
// `__hipPushCallConfiguration` call preceding it, which carries the geometry.
//
// Only the two anchor calls are stored. Argument values, launch dimensions and
// their constants are read back out of the IR on demand, so a descriptor can
// never report a value the IR has since stopped holding.
struct LaunchSite {
  cir::CallOp stubCall;

  // Null when the geometry could not be traced: the launch was not emitted in
  // the shape CIRGen produces, or the CFG has already been flattened.
  cir::CallOp pushConfigCall;

  llvm::StringRef getKernelName() const;

  unsigned getNumArgs() const;
  mlir::Value getArg(unsigned i) const;
  // The constant passed for argument `i`, or null if it is not a constant.
  mlir::TypedAttr getConstArg(unsigned i) const;

  // A launch dimension, recovered by walking the pushed value back to its dim3
  // constructor. For the temporary post-ABI path, this first reverses the
  // x86_64 SysV coerce-slot flattening.
  //
  // Failing to recover a dimension and recovering a dimension that turns out to
  // be a runtime value are different answers: `isTraced` reports the first,
  // `isFullyConstant` the second. A component is null when it was not traced.
  struct Dim3 {
    mlir::Value x, y, z;
    mlir::TypedAttr constX() const;
    mlir::TypedAttr constY() const;
    mlir::TypedAttr constZ() const;
    bool isTraced() const;
    bool isFullyConstant() const;
  };

  Dim3 getGridDim() const;
  Dim3 getBlockDim() const;

  mlir::Value getSharedMemBytes() const;
  mlir::TypedAttr getConstSharedMem() const;

  mlir::Value getStream() const;
  bool isDefaultStream() const;

  bool hasGeometry() const {
    return pushConfigCall != nullptr;
  }
};

// Binding for one kernel, keyed by its device mangled name. A name may resolve
// in several device modules (multi-arch); the owning module of each kernel is
// recoverable via k->getParentOfType<mlir::ModuleOp>().
//
// `launchSites` are the host calls to `hostStub`, which carry the same
// `cu.kernel_name` key as the stub itself.
struct KernelBinding {
  cir::FuncOp hostStub;
  llvm::SmallVector<cir::FuncOp, 2> deviceKernels;
  llvm::SmallVector<LaunchSite, 2> launchSites;
};

// The kernel `op` launches, or a null attribute if it is not a launch site.
// This is what the table records as a launch site, and what a pass must accept
// when checking that it can see every launch of a stub: the two answers come
// from here so they cannot drift apart.
//
// A launch is a call to a host stub. CIRGen tags the calls a `<<<>>>` emits
// with the kernel they launch; a call the tag is missing from, because it was
// made direct after the fact, launches whatever kernel its callee is the stub
// of.
cir::CUDAKernelNameAttr getLaunchedKernel(mlir::Operation *op,
                                          mlir::SymbolTableCollection &symbols);

// Whether any reference to `fn` within `scope` lives outside `fn`'s own body. A
// device stub hands its own address to cudaLaunchKernel, so it always
// references itself; that self-reference is not a use. Conservatively true when
// the uses cannot be enumerated.
bool hasUseOutsideSelf(cir::FuncOp fn, mlir::Operation *scope);

class KernelBindingTable {
public:
  // `container` is the offload container module, i.e. a module carrying the
  // `cir.offload.container` unit attribute. The host and device modules are
  // recovered from it via the `cir` container helpers.
  explicit KernelBindingTable(mlir::ModuleOp container);

  const KernelBinding *lookup(llvm::StringRef kernelName) const;

  // Device kernels bound to a host stub. The stub carries the `cu.kernel_name`
  // key itself, so a pass holding a launch callee can resolve device kernels
  // without re-deriving the mangled name. Empty if the func is not a stub.
  llvm::ArrayRef<cir::FuncOp> getDeviceFuncs(cir::FuncOp hostFn) const;

  // Host calls launching the named kernel. Empty if the name is unbound or the
  // kernel is never launched in this TU.
  llvm::ArrayRef<cir::LaunchSite>
  getLaunchSites(llvm::StringRef kernelName) const;

  // Whether the recorded launch sites of the named kernel are all of them, so a
  // pass may draw conclusions from them about the kernel itself. False when
  // another TU can call the stub, when a reference to it is not a launch this
  // table recorded, or when the name is unbound.
  bool allLaunchSitesVisible(llvm::StringRef kernelName) const;

  bool empty() const { return bindings.empty(); }
  size_t size() const { return bindings.size(); }
  auto begin() const { return bindings.begin(); }
  auto end() const { return bindings.end(); }

  void print(llvm::raw_ostream &os) const;

private:
  // Keys are StringRefs into the `cu.kernel_name` attribute storage, so the
  // table's lifetime is tied to the container IR it was built from.
  llvm::MapVector<llvm::StringRef, KernelBinding> bindings;
  // The scope the stubs and launch sites live in, so a visibility question
  // about a binding does not depend on the caller passing the right module.
  mlir::ModuleOp hostModule;
};

} // namespace cir

#endif // LLVM_CLANG_CIR_DIALECT_TRANSFORMS_OFFLOADOPT_KERNELBINDINGTABLE_H