//===- SpecializeLaunchWrappers.cpp - Clone kernel-launch wrappers --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A common CUDA/HIP idiom wraps the launch in a helper that takes the kernel
// as a parameter:
//
//   template <typename Kernel, typename... Args>
//   void launch(Kernel kernel, dim3 g, dim3 b, Args &&...args) {
//     kernel<<<g, b>>>(args...);
//   }
//
// `Kernel` deduces to a plain function-pointer type, so every kernel sharing a
// signature instantiates the *same* specialization and the callee really is a
// runtime value inside the wrapper. The launch is an indirect `cir.call`, which
// carries no `cu.kernel_name`, so `KernelBindingTable` never records it and
// every offload pass is blind to it.
//
// This pass clones such a wrapper once per distinct kernel it is called with,
// pins the kernel parameter inside the clone, and rewrites the now-known
// indirect call into the direct tagged call a source-level `<<<>>>` produces:
//
//   cir.call @__device_stub__k(%args) {cu.kernel_name =
//   #cir.cu.kernel_name<"k">}
//
// From there the launch is an ordinary binding-table entry and the existing
// specialisation passes see it. Call sites that do not pass a recognisable
// kernel keep calling the original, which is left untouched.
//
// Specialization is preferred to a general inliner: CIR does not implement
// DialectInlinerInterface, and cloning one wrapper is both smaller and easier
// to bound than inlining.
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelBindingTable.h"

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cir-offload-specialize-launch-wrappers"

namespace mlir {
#define GEN_PASS_DEF_OFFLOADSPECIALIZELAUNCHWRAPPERS
#include "clang/CIR/Dialect/Passes.h.inc"
} // namespace mlir

using namespace mlir;
using namespace cir;

namespace {

// The call that brackets a launch. A function without one launches nothing, so
// it is not a launch wrapper however it uses its parameters.
constexpr llvm::StringRef kPushCallConfiguration = "__hipPushCallConfiguration";

// Launch helpers are routinely layered two deep -- a generic helper over a
// per-algorithm one -- and pinning the outer kernel parameter is what exposes
// the inner wrapper. Iterate, but bounded: nesting deeper than this is not
// worth the compile time.
constexpr unsigned kMaxRounds = 4;

// Upper bound on clones, so a pathological input cannot blow up compile time
// or code size.
constexpr unsigned kMaxSpecializations = 512;

// Infix marking a wrapper clone. Also stops a clone from being specialised
// again on a later round: its own kernel parameter is already pinned, so it
// would otherwise match itself forever.
constexpr llvm::StringRef kWrapperCloneInfix = ".launchwrap.";

static Value stripCasts(Value v) {
  while (auto cast = v.getDefiningOp<cir::CastOp>()) {
    if (cast.getKind() != cir::CastKind::bitcast)
      break;
    v = cast.getSrc();
  }
  return v;
}

/// A kernel argument resolved back to the kernel it names.
struct ResolvedKernel {
  // The kernel's mangled name, empty when `v` does not name a kernel.
  llvm::StringRef name;
  // The expression to replicate when pinning. Its defining chain is only
  // `cir.get_global` and casts, so it can be rebuilt anywhere; the caller's
  // own value cannot be reused because it is not in scope in the clone.
  Value expr;

  explicit operator bool() const { return !name.empty(); }
};

/// Which kernel `v` names.
///
/// CIRGen hands the kernel around as the address of its handle global, which
/// it names with the kernel's mangled name (see KernelCloning.cpp). Callers
/// reach it either directly or through a local the kernel was assigned to --
/// `fattn_kernel_t k = flash_attn_ext_f16<...>;` -- and that slot survives
/// mem2reg, so the load has to be traced through here.
static ResolvedKernel resolveKernel(Value v,
                                    const llvm::StringMap<cir::FuncOp> &stubs,
                                    unsigned depth = 0) {
  if (!v || depth > 4)
    return {};
  Value stripped = stripCasts(v);

  if (auto get = stripped.getDefiningOp<cir::GetGlobalOp>()) {
    if (!stubs.count(get.getName()))
      return {};
    // Keep `v`, not `stripped`: the casts on top of the global are part of the
    // value the callee expects.
    return {get.getName(), v};
  }

  auto load = stripped.getDefiningOp<cir::LoadOp>();
  if (!load)
    return {};

  // A load of the handle global itself still identifies the kernel, but its
  // value is the global's contents rather than an expression over globals, so
  // it is replicated as the load it is.
  Value addr = stripCasts(load.getAddr());
  if (auto get = addr.getDefiningOp<cir::GetGlobalOp>())
    return stubs.count(get.getName()) ? ResolvedKernel{get.getName(), v}
                                      : ResolvedKernel{};

  // A local the kernel was stored into: the value to replicate is what was
  // stored, since the slot itself does not exist in the clone.
  auto alloca = addr.getDefiningOp<cir::AllocaOp>();
  if (!alloca)
    return {};
  cir::StoreOp uniqueStore;
  for (Operation *user : alloca.getResult().getUsers()) {
    if (auto store = dyn_cast<cir::StoreOp>(user)) {
      if (store.getValue() == alloca.getResult())
        return {}; // slot address escapes
      if (uniqueStore)
        return {}; // more than one reaching definition
      uniqueStore = store;
      continue;
    }
    if (isa<cir::LoadOp>(user))
      continue;
    return {};
  }
  if (!uniqueStore)
    return {};
  return resolveKernel(uniqueStore.getValue(), stubs, depth + 1);
}

/// Whether `replicateKernelExpr` will succeed for `v`. Asking first keeps the
/// caller from building a clone it then has to throw away, which would mean
/// erasing an operation the SymbolTable still has a pointer to.
static bool canReplicateKernelExpr(Value v, unsigned depth = 0) {
  if (!v || depth > 4)
    return false;
  if (v.getDefiningOp<cir::GetGlobalOp>())
    return true;
  if (auto cast = v.getDefiningOp<cir::CastOp>())
    return canReplicateKernelExpr(cast.getSrc(), depth + 1);
  if (auto load = v.getDefiningOp<cir::LoadOp>())
    return canReplicateKernelExpr(load.getAddr(), depth + 1);
  return false;
}

/// Rebuild `v`'s defining chain at `builder`. Only chains over globals can be
/// rebuilt; anything else returns null, and the caller must not pin.
static Value replicateKernelExpr(OpBuilder &builder, Location loc, Value v,
                                 unsigned depth = 0) {
  if (!v || depth > 4)
    return {};
  if (auto get = v.getDefiningOp<cir::GetGlobalOp>())
    return cir::GetGlobalOp::create(builder, loc, get.getType(), get.getName())
        .getResult();
  if (auto cast = v.getDefiningOp<cir::CastOp>()) {
    Value src = replicateKernelExpr(builder, loc, cast.getSrc(), depth + 1);
    if (!src)
      return {};
    return cir::CastOp::create(builder, loc, cast.getType(), cast.getKind(),
                               src)
        .getResult();
  }
  if (auto load = v.getDefiningOp<cir::LoadOp>()) {
    Value addr = replicateKernelExpr(builder, loc, load.getAddr(), depth + 1);
    if (!addr)
      return {};
    return cir::LoadOp::create(builder, loc, addr).getResult();
  }
  return {};
}

/// If `callee` is (transitively) the value of one of `fn`'s parameters, return
/// that parameter's index.
///
/// CIRGen spills parameters into an entry-block alloca and reloads them, so the
/// shape is: load <- slot <- single store of a block argument.
static std::optional<unsigned> traceCalleeToParam(cir::FuncOp fn,
                                                  Value callee) {
  callee = stripCasts(callee);

  auto asBlockArg = [&](Value v) -> std::optional<unsigned> {
    auto arg = dyn_cast<BlockArgument>(v);
    if (!arg || arg.getOwner() != &fn.getBody().front())
      return std::nullopt;
    return arg.getArgNumber();
  };

  if (auto direct = asBlockArg(callee))
    return direct;

  // The launched value is read back out of the handle, so peel loads until a
  // spill slot appears rather than expecting exactly one.
  for (unsigned depth = 0; depth < 4; ++depth) {
    auto load = callee.getDefiningOp<cir::LoadOp>();
    if (!load)
      return std::nullopt;
    Value addr = stripCasts(load.getAddr());
    if (auto arg = asBlockArg(addr))
      return arg;
    auto alloca = addr.getDefiningOp<cir::AllocaOp>();
    if (!alloca) {
      callee = addr;
      continue;
    }

    cir::StoreOp uniqueStore;
    for (Operation *user : alloca.getResult().getUsers()) {
      if (auto store = dyn_cast<cir::StoreOp>(user)) {
        if (store.getValue() == alloca.getResult())
          return std::nullopt; // slot address escapes
        if (uniqueStore)
          return std::nullopt; // more than one reaching definition
        uniqueStore = store;
        continue;
      }
      if (isa<cir::LoadOp>(user))
        continue;
      return std::nullopt;
    }
    if (!uniqueStore)
      return std::nullopt;
    Value stored = stripCasts(uniqueStore.getValue());
    if (auto arg = asBlockArg(stored))
      return arg;
    callee = stored;
  }
  return std::nullopt;
}

/// The type `call` calls through, or null if that is not a function pointer.
static cir::FuncType indirectCalleeType(cir::CallOp call) {
  auto ptrTy = dyn_cast<cir::PointerType>(call.getIndirectCall().getType());
  return ptrTy ? dyn_cast<cir::FuncType>(ptrTy.getPointee()) : cir::FuncType();
}

/// Every indirect call in `fn` that launches `stub` through parameter
/// `paramIdx`.
///
/// Tracing to the parameter is necessary but not sufficient: a wrapper may
/// call something else indirectly through the same value, and rewriting that
/// into a kernel launch would silently redirect it. Require the signature to
/// be the stub's as well, which is what actually makes it a launch of this
/// kernel.
static SmallVector<cir::CallOp>
indirectLaunches(cir::FuncOp fn, unsigned paramIdx, cir::FuncOp stub) {
  SmallVector<cir::CallOp> out;
  fn.walk([&](cir::CallOp call) {
    if (!call.isIndirect() || call.getNumResults() != 0)
      return;
    if (traceCalleeToParam(fn, call.getIndirectCall()) != paramIdx)
      return;
    cir::FuncType calleeTy = indirectCalleeType(call);
    if (!calleeTy || calleeTy != stub.getFunctionType())
      return;
    if (call.getArgOperands().size() != stub.getFunctionType().getNumInputs())
      return;
    out.push_back(call);
  });
  return out;
}

struct OffloadSpecializeLaunchWrappersPass
    : public impl::OffloadSpecializeLaunchWrappersBase<
          OffloadSpecializeLaunchWrappersPass> {
  void runOnOperation() override;

private:
  bool runOnce(ModuleOp host, unsigned &created);
};

/// Wrapper functions, mapped to the parameter that supplies the kernel.
static llvm::MapVector<cir::FuncOp, unsigned>
findWrappers(ModuleOp host, const llvm::StringMap<cir::FuncOp> &stubs) {
  llvm::MapVector<cir::FuncOp, unsigned> wrappers;

  host.walk([&](cir::FuncOp fn) {
    if (fn.isDeclaration() || fn.getBody().empty())
      return;
    if (fn.getSymName().contains(kWrapperCloneInfix))
      return;

    bool hasLaunchConfig = false;
    fn.walk([&](cir::CallOp call) {
      std::optional<llvm::StringRef> callee = call.getCallee();
      if (callee && *callee == kPushCallConfiguration)
        hasLaunchConfig = true;
    });
    if (!hasLaunchConfig)
      return;

    // One parameter must supply every indirect callee. If the function makes
    // an indirect call this cannot explain, pinning the parameter would not
    // make the function's launches visible, so leave it alone.
    std::optional<unsigned> paramIdx;
    bool ambiguous = false;
    fn.walk([&](cir::CallOp call) {
      if (!call.isIndirect())
        return;
      std::optional<unsigned> idx =
          traceCalleeToParam(fn, call.getIndirectCall());
      if (!idx || (paramIdx && *paramIdx != *idx)) {
        ambiguous = true;
        return;
      }
      paramIdx = idx;
    });
    if (!ambiguous && paramIdx)
      wrappers.insert({fn, *paramIdx});
  });

  // A wrapper need not contain the launch itself: it may forward the kernel to
  // another wrapper, which is what a library layering a generic launch helper
  // over a per-algorithm one produces. Propagate the property back to such
  // forwarders until it stops growing.
  for (bool grew = true; grew;) {
    grew = false;
    SmallVector<std::pair<cir::FuncOp, unsigned>> found;
    for (auto &[callee, calleeParamIdx] : wrappers) {
      std::optional<SymbolTable::UseRange> uses =
          SymbolTable::getSymbolUses(callee.getOperation(), host);
      if (!uses)
        continue;
      for (SymbolTable::SymbolUse use : *uses) {
        auto call = dyn_cast<cir::CallOp>(use.getUser());
        if (!call || call.isIndirect())
          continue;
        if (calleeParamIdx >= call.getArgOperands().size())
          continue;
        auto caller = call->getParentOfType<cir::FuncOp>();
        if (!caller || caller.getBody().empty() || wrappers.count(caller))
          continue;
        if (caller.getSymName().contains(kWrapperCloneInfix))
          continue;
        if (std::optional<unsigned> idx = traceCalleeToParam(
                caller, call.getArgOperands()[calleeParamIdx]))
          found.push_back({caller, *idx});
      }
    }
    for (auto &[fn, idx] : found)
      if (wrappers.insert({fn, idx}).second)
        grew = true;
  }

  return wrappers;
}

bool OffloadSpecializeLaunchWrappersPass::runOnce(ModuleOp host,
                                                  unsigned &created) {
  MLIRContext *ctx = &getContext();

  // Kernel name -> its host stub. The name is also that of the handle global
  // callers take the address of, which is how a kernel argument is recognised.
  llvm::StringMap<cir::FuncOp> stubs;
  host.walk([&](cir::FuncOp fn) {
    if (auto name = fn->getAttrOfType<cir::CUDAKernelNameAttr>(
            cir::CUDAKernelNameAttr::getMnemonic()))
      stubs[name.getKernelName().getValue()] = fn;
  });
  if (stubs.empty())
    return false;

  llvm::MapVector<cir::FuncOp, unsigned> wrappers = findWrappers(host, stubs);
  LLVM_DEBUG({
    llvm::dbgs() << "stubs=" << stubs.size() << " wrappers=" << wrappers.size()
                 << "\n";
    for (auto &[w, i] : wrappers)
      llvm::dbgs() << "  wrapper " << w.getSymName() << " param=" << i << "\n";
  });
  if (wrappers.empty())
    return false;

  SymbolTable symTable(host);
  bool changed = false;

  for (auto &[wrapper, paramIdx] : wrappers) {
    // Group the call sites by the kernel they pass, so one clone serves all
    // sites that agree.
    // Sites grouped by kernel, each group keeping one expression to replicate.
    struct KernelGroup {
      Value expr;
      SmallVector<cir::CallOp> sites;
    };
    llvm::MapVector<llvm::StringRef, KernelGroup> byKernel;
    std::optional<SymbolTable::UseRange> uses =
        symTable.getSymbolUses(wrapper.getOperation(), host);
    if (!uses)
      continue;
    for (SymbolTable::SymbolUse use : *uses) {
      auto call = dyn_cast<cir::CallOp>(use.getUser());
      if (!call || call.isIndirect())
        continue;
      if (paramIdx >= call.getArgOperands().size())
        continue;
      ResolvedKernel resolved =
          resolveKernel(call.getArgOperands()[paramIdx], stubs);
      if (!resolved)
        continue;
      KernelGroup &group = byKernel[resolved.name];
      if (!group.expr)
        group.expr = resolved.expr;
      group.sites.push_back(call);
    }

    for (auto &[kernelName, group] : byKernel) {
      if (created >= kMaxSpecializations) {
        LLVM_DEBUG(llvm::dbgs() << "  specialization cap reached, stopping\n");
        return changed;
      }

      std::string cloneName =
          (wrapper.getSymName() + kWrapperCloneInfix + kernelName).str();
      if (host.lookupSymbol(cloneName))
        continue;

      cir::FuncOp stub = stubs[kernelName];
      // Nothing to devirtualize means the wrapper does not actually launch
      // this kernel through its parameter, so cloning would only add code.
      if (indirectLaunches(wrapper, paramIdx, stub).empty())
        continue;

      // Decide whether the pin is possible before building anything. Cloning
      // first and erasing on failure would leave the SymbolTable holding a
      // pointer to a freed operation.
      Type paramTy = wrapper.getBody().front().getArgument(paramIdx).getType();
      if (!canReplicateKernelExpr(group.expr) ||
          group.expr.getType() != paramTy)
        continue;

      auto clone = cast<cir::FuncOp>(wrapper->clone());
      clone.setSymName(cloneName);
      // The clone exists only for the sites retargeted at it below, so it must
      // not keep the original's external linkage and be callable from
      // elsewhere with a different kernel.
      clone.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
      clone.setSymVisibility("private");
      symTable.insert(clone, Block::iterator(wrapper->getNextNode()));

      // Collect the launches before pinning: afterwards the callee no longer
      // traces to the parameter.
      SmallVector<cir::CallOp> launches =
          indirectLaunches(clone, paramIdx, stub);

      // Pin the parameter. Uses other than the launch matter too: a forwarder
      // passes it on, and making that argument constant is what exposes the
      // next wrapper down on the following round -- and `launch_fattn` hands
      // the same value to the occupancy API, so it has to stay the value the
      // caller actually passed rather than a re-derived equivalent.
      BlockArgument param = clone.getBody().front().getArgument(paramIdx);
      OpBuilder builder(ctx);
      builder.setInsertionPointToStart(&clone.getBody().front());
      Value pinned = replicateKernelExpr(builder, clone.getLoc(), group.expr);
      assert(pinned && pinned.getType() == param.getType() &&
             "canReplicateKernelExpr disagreed with replicateKernelExpr");
      param.replaceAllUsesWith(pinned);

      // Rewrite each launch into the direct, tagged call a source-level
      // `<<<>>>` produces, which is the shape KernelBindingTable records.
      for (cir::CallOp launch : launches) {
        OpBuilder callBuilder(launch);
        auto direct = cir::CallOp::create(
            callBuilder, launch.getLoc(), mlir::FlatSymbolRefAttr::get(stub),
            cir::VoidType::get(ctx), launch.getArgOperands());

        // Carry the original call's attributes over, as replaceCallWithTryCall
        // does. They are not decoration: an aggregate passed by value -- rope
        // hands the fused kernel four `uint3` and a `rope_corr_dims` -- carries
        // the ABI treatment of each argument here, and a call rebuilt without
        // them is lowered with a different calling sequence than the callee
        // expects.
        llvm::StringRef excludedAttrs[] = {
            cir::CIRDialect::getCalleeAttrName(), // set by create()
            cir::CIRDialect::getOperandSegmentSizesAttrName(),
        };
        for (mlir::NamedAttribute attr : launch->getAttrs()) {
          if (llvm::is_contained(excludedAttrs, attr.getName()))
            continue;
          direct->setAttr(attr.getName(), attr.getValue());
        }

        direct->setAttr(cir::CUDAKernelNameAttr::getMnemonic(),
                        cir::CUDAKernelNameAttr::get(
                            ctx, StringAttr::get(ctx, kernelName)));
        launch.erase();
      }

      for (cir::CallOp site : group.sites)
        site.setCalleeAttr(mlir::FlatSymbolRefAttr::get(clone));

      ++created;
      changed = true;
      LLVM_DEBUG(llvm::dbgs()
                 << "  " << wrapper.getSymName() << " -> " << cloneName << " ("
                 << group.sites.size() << " sites, " << launches.size()
                 << " launches)\n");
    }
  }

  return changed;
}

void OffloadSpecializeLaunchWrappersPass::runOnOperation() {
  cir::OffloadContainerOp container = getOperation();
  ModuleOp host = container.getHostModule();
  if (!host)
    return;

  unsigned created = 0;
  bool changed = false;
  for (unsigned round = 0; round < kMaxRounds; ++round) {
    if (!runOnce(host, created))
      break;
    changed = true;
  }

  if (!changed)
    markAllAnalysesPreserved();
}

} // namespace

std::unique_ptr<Pass> mlir::createOffloadSpecializeLaunchWrappersPass() {
  return std::make_unique<OffloadSpecializeLaunchWrappersPass>();
}
