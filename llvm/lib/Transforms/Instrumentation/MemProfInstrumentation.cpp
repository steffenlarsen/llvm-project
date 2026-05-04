//===- MemProfInstrumentation.cpp - memory alloc and access instrumentation ==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemProf. Memory accesses are instrumented
// to increment the access count held in a shadow memory location, or
// alternatively to call into the runtime. Memory intrinsic calls (memmove,
// memcpy, memset) are changed to call the memory profiling runtime version
// instead.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/MemProfInstrumentation.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/ProfileData/MemProf.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Instrumentation/InstrumentationOptionsOptInfos.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;
using namespace llvm::memprof;

#define DEBUG_TYPE "memprof"

constexpr int LLVM_MEM_PROFILER_VERSION = 1;

// Size of memory mapped to a single shadow location.
constexpr uint64_t DefaultMemGranularity = 64;

// Size of memory mapped to a single histogram bucket.
constexpr uint64_t HistogramGranularity = 8;

// Scale from granularity down to shadow size.
constexpr uint64_t DefaultShadowScale = 3;

constexpr char MemProfModuleCtorName[] = "memprof.module_ctor";
constexpr uint64_t MemProfCtorAndDtorPriority = 1;
// On Emscripten, the system needs more than one priorities for constructors.
constexpr uint64_t MemProfEmscriptenCtorAndDtorPriority = 50;
constexpr char MemProfInitName[] = "__memprof_init";
constexpr char MemProfVersionCheckNamePrefix[] =
    "__memprof_version_mismatch_check_v";

constexpr char MemProfShadowMemoryDynamicAddress[] =
    "__memprof_shadow_memory_dynamic_address";

constexpr char MemProfFilenameVar[] = "__memprof_profile_filename";

constexpr char MemProfHistogramFlagVar[] = "__memprof_histogram";

#define MEMPROF_GETTER(VarName, DescName, Default)                             \
  static auto get##VarName(const Module &M) {                                  \
    if (auto *O = clv2::getView<&clv2::InstrumentationOptsReg>(                \
            M.getContext().getOptionsContext()))                               \
      if (O->specified<&clv2::DescName>())                                     \
        return O->get<&clv2::DescName>();                                      \
    return Default;                                                            \
  }

MEMPROF_GETTER(ClInsertVersionCheck, INST_MemprofGuardAgainstVersionMismatch,
               true)
MEMPROF_GETTER(ClInstrumentReads, INST_MemprofInstrumentReads, true)
MEMPROF_GETTER(ClInstrumentWrites, INST_MemprofInstrumentWrites, true)
MEMPROF_GETTER(ClInstrumentAtomics, INST_MemprofInstrumentAtomics, true)
MEMPROF_GETTER(ClUseCalls, INST_MemprofUseCallbacks, false)
MEMPROF_GETTER(ClMappingScale, INST_MemprofMappingScale,
               static_cast<int>(DefaultShadowScale))
MEMPROF_GETTER(ClMappingGranularity, INST_MemprofMappingGranularity,
               static_cast<int>(DefaultMemGranularity))
MEMPROF_GETTER(ClStack, INST_MemprofInstrumentStack, false)
MEMPROF_GETTER(ClDebug, INST_MemprofDebug, 0)
MEMPROF_GETTER(ClDebugMin, INST_MemprofDebugMin, -1)
MEMPROF_GETTER(ClDebugMax, INST_MemprofDebugMax, -1)
MEMPROF_GETTER(ClHistogram, INST_MemprofHistogram, false)

#undef MEMPROF_GETTER

// String getters need explicit return types (no macro).
static std::string getClMemoryAccessCallbackPrefix(const Module &M) {
  return clv2::getOptValIfSpecified<
      &clv2::InstrumentationOptsReg,
      &clv2::INST_MemprofMemoryAccessCallbackPrefix>(
      M.getContext().getOptionsContext(), "__memprof_");
}

static std::string getClDebugFunc(const Module &M) {
  return clv2::getOptValIfSpecified<&clv2::InstrumentationOptsReg,
                                    &clv2::INST_MemprofDebugFunc>(
      M.getContext().getOptionsContext(), std::string{});
}

static std::string getMemprofRuntimeDefaultOptions(const Module &M) {
  return clv2::getOptValIfSpecified<&clv2::InstrumentationOptsReg,
                                    &clv2::INST_MemprofRuntimeDefaultOptions>(
      M.getContext().getOptionsContext(), std::string{});
}

// Instrumentation statistics
STATISTIC(NumInstrumentedReads, "Number of instrumented reads");
STATISTIC(NumInstrumentedWrites, "Number of instrumented writes");
STATISTIC(NumSkippedStackReads, "Number of non-instrumented stack reads");
STATISTIC(NumSkippedStackWrites, "Number of non-instrumented stack writes");

namespace {

/// This struct defines the shadow mapping using the rule:
///   shadow = ((mem & mask) >> Scale) ADD DynamicShadowOffset.
struct ShadowMapping {
  ShadowMapping() = default;
  ShadowMapping(const Module &M) {
    Scale = getClMappingScale(M);
    Granularity =
        getClHistogram(M) ? HistogramGranularity : getClMappingGranularity(M);
    Mask = ~(Granularity - 1);
  }

  int Scale = 0;
  int Granularity = 0;
  uint64_t Mask = 0; // Computed as ~(Granularity-1)
};

static uint64_t getCtorAndDtorPriority(Triple &TargetTriple) {
  return TargetTriple.isOSEmscripten() ? MemProfEmscriptenCtorAndDtorPriority
                                       : MemProfCtorAndDtorPriority;
}

struct InterestingMemoryAccess {
  Value *Addr = nullptr;
  bool IsWrite;
  Type *AccessTy;
  Value *MaybeMask = nullptr;
};

/// Instrument the code in module to profile memory accesses.
class MemProfiler {
public:
  MemProfiler(Module &M) : Mod(M), Mapping(M) {
    C = &(M.getContext());
    LongSize = M.getDataLayout().getPointerSizeInBits();
    IntptrTy = Type::getIntNTy(*C, LongSize);
    PtrTy = PointerType::getUnqual(*C);
  }

  /// If it is an interesting memory access, populate information
  /// about the access and return a InterestingMemoryAccess struct.
  /// Otherwise return std::nullopt.
  std::optional<InterestingMemoryAccess>
  isInterestingMemoryAccess(Instruction *I) const;

  void instrumentMop(Instruction *I, const DataLayout &DL,
                     InterestingMemoryAccess &Access);
  void instrumentAddress(Instruction *OrigIns, Instruction *InsertBefore,
                         Value *Addr, bool IsWrite);
  void instrumentMaskedLoadOrStore(const DataLayout &DL, Value *Mask,
                                   Instruction *I, Value *Addr, Type *AccessTy,
                                   bool IsWrite);
  void instrumentMemIntrinsic(MemIntrinsic *MI);
  Value *memToShadow(Value *Shadow, IRBuilder<> &IRB);
  bool instrumentFunction(Function &F);
  bool maybeInsertMemProfInitAtFunctionEntry(Function &F);
  bool insertDynamicShadowAtFunctionEntry(Function &F);

private:
  void initializeCallbacks(Module &M);

  Module &Mod;
  LLVMContext *C;
  int LongSize;
  Type *IntptrTy;
  PointerType *PtrTy;
  ShadowMapping Mapping;

  // These arrays is indexed by AccessIsWrite
  FunctionCallee MemProfMemoryAccessCallback[2];

  FunctionCallee MemProfMemmove, MemProfMemcpy, MemProfMemset;
  Value *DynamicShadowOffset = nullptr;
};

class ModuleMemProfiler {
public:
  ModuleMemProfiler(Module &M) : Mapping(M) {
    TargetTriple = M.getTargetTriple();
  }

  bool instrumentModule(Module &);

private:
  Triple TargetTriple;
  ShadowMapping Mapping;
  Function *MemProfCtorFunction = nullptr;
};

} // end anonymous namespace

MemProfilerPass::MemProfilerPass() = default;

PreservedAnalyses MemProfilerPass::run(Function &F,
                                       AnalysisManager<Function> &AM) {
  Module &M = *F.getParent();
  assert((!getClHistogram(M) ||
          getClMappingGranularity(M) == (int)DefaultMemGranularity) &&
         "Memprof with histogram only supports default mapping granularity");
  MemProfiler Profiler(M);
  if (Profiler.instrumentFunction(F))
    return PreservedAnalyses::none();
  return PreservedAnalyses::all();
}

ModuleMemProfilerPass::ModuleMemProfilerPass() = default;

PreservedAnalyses ModuleMemProfilerPass::run(Module &M,
                                             AnalysisManager<Module> &AM) {

  ModuleMemProfiler Profiler(M);
  if (Profiler.instrumentModule(M))
    return PreservedAnalyses::none();
  return PreservedAnalyses::all();
}

Value *MemProfiler::memToShadow(Value *Shadow, IRBuilder<> &IRB) {
  // (Shadow & mask) >> scale
  Shadow = IRB.CreateAnd(Shadow, Mapping.Mask);
  Shadow = IRB.CreateLShr(Shadow, Mapping.Scale);
  // (Shadow >> scale) | offset
  assert(DynamicShadowOffset);
  return IRB.CreateAdd(Shadow, DynamicShadowOffset);
}

// Instrument memset/memmove/memcpy
void MemProfiler::instrumentMemIntrinsic(MemIntrinsic *MI) {
  IRBuilder<> IRB(MI);
  if (isa<MemTransferInst>(MI)) {
    IRB.CreateCall(isa<MemMoveInst>(MI) ? MemProfMemmove : MemProfMemcpy,
                   {MI->getOperand(0), MI->getOperand(1),
                    IRB.CreateIntCast(MI->getOperand(2), IntptrTy, false)});
  } else if (isa<MemSetInst>(MI)) {
    IRB.CreateCall(
        MemProfMemset,
        {MI->getOperand(0),
         IRB.CreateIntCast(MI->getOperand(1), IRB.getInt32Ty(), false),
         IRB.CreateIntCast(MI->getOperand(2), IntptrTy, false)});
  }
  MI->eraseFromParent();
}

std::optional<InterestingMemoryAccess>
MemProfiler::isInterestingMemoryAccess(Instruction *I) const {
  // Do not instrument the load fetching the dynamic shadow address.
  if (DynamicShadowOffset == I)
    return std::nullopt;

  InterestingMemoryAccess Access;

  if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
    if (!getClInstrumentReads(Mod))
      return std::nullopt;
    Access.IsWrite = false;
    Access.AccessTy = LI->getType();
    Access.Addr = LI->getPointerOperand();
  } else if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
    if (!getClInstrumentWrites(Mod))
      return std::nullopt;
    Access.IsWrite = true;
    Access.AccessTy = SI->getValueOperand()->getType();
    Access.Addr = SI->getPointerOperand();
  } else if (AtomicRMWInst *RMW = dyn_cast<AtomicRMWInst>(I)) {
    if (!getClInstrumentAtomics(Mod))
      return std::nullopt;
    Access.IsWrite = true;
    Access.AccessTy = RMW->getValOperand()->getType();
    Access.Addr = RMW->getPointerOperand();
  } else if (AtomicCmpXchgInst *XCHG = dyn_cast<AtomicCmpXchgInst>(I)) {
    if (!getClInstrumentAtomics(Mod))
      return std::nullopt;
    Access.IsWrite = true;
    Access.AccessTy = XCHG->getCompareOperand()->getType();
    Access.Addr = XCHG->getPointerOperand();
  } else if (auto *CI = dyn_cast<CallInst>(I)) {
    auto *F = CI->getCalledFunction();
    if (F && (F->getIntrinsicID() == Intrinsic::masked_load ||
              F->getIntrinsicID() == Intrinsic::masked_store)) {
      unsigned OpOffset = 0;
      if (F->getIntrinsicID() == Intrinsic::masked_store) {
        if (!getClInstrumentWrites(Mod))
          return std::nullopt;
        // Masked store has an initial operand for the value.
        OpOffset = 1;
        Access.AccessTy = CI->getArgOperand(0)->getType();
        Access.IsWrite = true;
      } else {
        if (!getClInstrumentReads(Mod))
          return std::nullopt;
        Access.AccessTy = CI->getType();
        Access.IsWrite = false;
      }

      auto *BasePtr = CI->getOperand(0 + OpOffset);
      Access.MaybeMask = CI->getOperand(1 + OpOffset);
      Access.Addr = BasePtr;
    }
  }

  if (!Access.Addr)
    return std::nullopt;

  // Do not instrument accesses from different address spaces; we cannot deal
  // with them.
  Type *PtrTy = cast<PointerType>(Access.Addr->getType()->getScalarType());
  if (PtrTy->getPointerAddressSpace() != 0)
    return std::nullopt;

  // Ignore swifterror addresses.
  // swifterror memory addresses are mem2reg promoted by instruction
  // selection. As such they cannot have regular uses like an instrumentation
  // function and it makes no sense to track them as memory.
  if (Access.Addr->isSwiftError())
    return std::nullopt;

  // Peel off GEPs and BitCasts.
  auto *Addr = Access.Addr->stripInBoundsOffsets();

  if (GlobalVariable *GV = dyn_cast<GlobalVariable>(Addr)) {
    // Do not instrument PGO counter updates.
    if (GV->hasSection()) {
      StringRef SectionName = GV->getSection();
      // Check if the global is in the PGO counters section.
      auto OF = I->getModule()->getTargetTriple().getObjectFormat();
      if (SectionName.ends_with(
              getInstrProfSectionName(IPSK_cnts, OF, /*AddSegmentInfo=*/false)))
        return std::nullopt;
    }

    // Do not instrument accesses to LLVM internal variables.
    if (GV->getName().starts_with("__llvm") ||
        GV->getName().starts_with(getInstrProfVarPrefix()))
      return std::nullopt;
  }

  return Access;
}

void MemProfiler::instrumentMaskedLoadOrStore(const DataLayout &DL, Value *Mask,
                                              Instruction *I, Value *Addr,
                                              Type *AccessTy, bool IsWrite) {
  auto *VTy = cast<FixedVectorType>(AccessTy);
  unsigned Num = VTy->getNumElements();
  auto *Zero = ConstantInt::get(IntptrTy, 0);
  for (unsigned Idx = 0; Idx < Num; ++Idx) {
    Value *InstrumentedAddress = nullptr;
    Instruction *InsertBefore = I;
    if (auto *Vector = dyn_cast<ConstantVector>(Mask)) {
      // dyn_cast as we might get UndefValue
      if (auto *Masked = dyn_cast<ConstantInt>(Vector->getOperand(Idx))) {
        if (Masked->isZero())
          // Mask is constant false, so no instrumentation needed.
          continue;
        // If we have a true or undef value, fall through to instrumentAddress.
        // with InsertBefore == I
      }
    } else {
      IRBuilder<> IRB(I);
      Value *MaskElem = IRB.CreateExtractElement(Mask, Idx);
      Instruction *ThenTerm = SplitBlockAndInsertIfThen(MaskElem, I, false);
      InsertBefore = ThenTerm;
    }

    IRBuilder<> IRB(InsertBefore);
    InstrumentedAddress =
        IRB.CreateGEP(VTy, Addr, {Zero, ConstantInt::get(IntptrTy, Idx)});
    instrumentAddress(I, InsertBefore, InstrumentedAddress, IsWrite);
  }
}

void MemProfiler::instrumentMop(Instruction *I, const DataLayout &DL,
                                InterestingMemoryAccess &Access) {
  // Skip instrumentation of stack accesses unless requested.
  if (!getClStack(Mod) && isa<AllocaInst>(getUnderlyingObject(Access.Addr))) {
    if (Access.IsWrite)
      ++NumSkippedStackWrites;
    else
      ++NumSkippedStackReads;
    return;
  }

  if (Access.IsWrite)
    NumInstrumentedWrites++;
  else
    NumInstrumentedReads++;

  if (Access.MaybeMask) {
    instrumentMaskedLoadOrStore(DL, Access.MaybeMask, I, Access.Addr,
                                Access.AccessTy, Access.IsWrite);
  } else {
    // Since the access counts will be accumulated across the entire allocation,
    // we only update the shadow access count for the first location and thus
    // don't need to worry about alignment and type size.
    instrumentAddress(I, I, Access.Addr, Access.IsWrite);
  }
}

void MemProfiler::instrumentAddress(Instruction *OrigIns,
                                    Instruction *InsertBefore, Value *Addr,
                                    bool IsWrite) {
  IRBuilder<> IRB(InsertBefore);
  Value *AddrLong = IRB.CreatePointerCast(Addr, IntptrTy);

  if (getClUseCalls(Mod)) {
    IRB.CreateCall(MemProfMemoryAccessCallback[IsWrite], AddrLong);
    return;
  }

  Type *ShadowTy =
      getClHistogram(Mod) ? Type::getInt8Ty(*C) : Type::getInt64Ty(*C);
  Type *ShadowPtrTy = PointerType::get(*C, 0);

  Value *ShadowPtr = memToShadow(AddrLong, IRB);
  Value *ShadowAddr = IRB.CreateIntToPtr(ShadowPtr, ShadowPtrTy);
  Value *ShadowValue = IRB.CreateLoad(ShadowTy, ShadowAddr);
  // If we are profiling with histograms, add overflow protection at 255.
  if (getClHistogram(Mod)) {
    Value *MaxCount = ConstantInt::get(Type::getInt8Ty(*C), 255);
    Value *Cmp = IRB.CreateICmpULT(ShadowValue, MaxCount);
    Instruction *IncBlock =
        SplitBlockAndInsertIfThen(Cmp, InsertBefore, /*Unreachable=*/false);
    IRB.SetInsertPoint(IncBlock);
  }
  Value *Inc = ConstantInt::get(ShadowTy, 1);
  ShadowValue = IRB.CreateAdd(ShadowValue, Inc);
  IRB.CreateStore(ShadowValue, ShadowAddr);
}

// Create the variable for the profile file name.
void createProfileFileNameVar(Module &M) {
  const MDString *MemProfFilename =
      dyn_cast_or_null<MDString>(M.getModuleFlag("MemProfProfileFilename"));
  if (!MemProfFilename)
    return;
  assert(!MemProfFilename->getString().empty() &&
         "Unexpected MemProfProfileFilename metadata with empty string");
  Constant *ProfileNameConst = ConstantDataArray::getString(
      M.getContext(), MemProfFilename->getString(), true);
  GlobalVariable *ProfileNameVar = new GlobalVariable(
      M, ProfileNameConst->getType(), /*isConstant=*/true,
      GlobalValue::WeakAnyLinkage, ProfileNameConst, MemProfFilenameVar);
  const Triple &TT = M.getTargetTriple();
  if (TT.supportsCOMDAT()) {
    ProfileNameVar->setLinkage(GlobalValue::ExternalLinkage);
    ProfileNameVar->setComdat(M.getOrInsertComdat(MemProfFilenameVar));
  }
}

// Set MemprofHistogramFlag as a Global variable in IR. This makes it accessible
// to the runtime, changing shadow count behavior.
void createMemprofHistogramFlagVar(Module &M) {
  const StringRef VarName(MemProfHistogramFlagVar);
  Type *IntTy1 = Type::getInt1Ty(M.getContext());
  auto MemprofHistogramFlag = new GlobalVariable(
      M, IntTy1, true, GlobalValue::WeakAnyLinkage,
      Constant::getIntegerValue(IntTy1, APInt(1, getClHistogram(M))), VarName);
  const Triple &TT = M.getTargetTriple();
  if (TT.supportsCOMDAT()) {
    MemprofHistogramFlag->setLinkage(GlobalValue::ExternalLinkage);
    MemprofHistogramFlag->setComdat(M.getOrInsertComdat(VarName));
  }
  appendToCompilerUsed(M, MemprofHistogramFlag);
}

void createMemprofDefaultOptionsVar(Module &M) {
  Constant *OptionsConst = ConstantDataArray::getString(
      M.getContext(), getMemprofRuntimeDefaultOptions(M), /*AddNull=*/true);
  GlobalVariable *OptionsVar =
      new GlobalVariable(M, OptionsConst->getType(), /*isConstant=*/true,
                         GlobalValue::WeakAnyLinkage, OptionsConst,
                         memprof::getMemprofOptionsSymbolName());
  const Triple &TT = M.getTargetTriple();
  if (TT.supportsCOMDAT()) {
    OptionsVar->setLinkage(GlobalValue::ExternalLinkage);
    OptionsVar->setComdat(M.getOrInsertComdat(OptionsVar->getName()));
  }
}

bool ModuleMemProfiler::instrumentModule(Module &M) {

  // Create a module constructor.
  std::string MemProfVersion = std::to_string(LLVM_MEM_PROFILER_VERSION);
  std::string VersionCheckName =
      getClInsertVersionCheck(M)
          ? (MemProfVersionCheckNamePrefix + MemProfVersion)
          : "";
  std::tie(MemProfCtorFunction, std::ignore) =
      createSanitizerCtorAndInitFunctions(M, MemProfModuleCtorName,
                                          MemProfInitName, /*InitArgTypes=*/{},
                                          /*InitArgs=*/{}, VersionCheckName);

  const uint64_t Priority = getCtorAndDtorPriority(TargetTriple);
  appendToGlobalCtors(M, MemProfCtorFunction, Priority);

  createProfileFileNameVar(M);

  createMemprofHistogramFlagVar(M);

  createMemprofDefaultOptionsVar(M);

  return true;
}

void MemProfiler::initializeCallbacks(Module &M) {
  IRBuilder<> IRB(*C);

  for (size_t AccessIsWrite = 0; AccessIsWrite <= 1; AccessIsWrite++) {
    const std::string TypeStr = AccessIsWrite ? "store" : "load";
    const std::string HistPrefix = getClHistogram(M) ? "hist_" : "";

    SmallVector<Type *, 2> Args1{1, IntptrTy};
    MemProfMemoryAccessCallback[AccessIsWrite] = M.getOrInsertFunction(
        getClMemoryAccessCallbackPrefix(M) + HistPrefix + TypeStr,
        FunctionType::get(IRB.getVoidTy(), Args1, false));
  }
  MemProfMemmove =
      M.getOrInsertFunction(getClMemoryAccessCallbackPrefix(M) + "memmove",
                            PtrTy, PtrTy, PtrTy, IntptrTy);
  MemProfMemcpy =
      M.getOrInsertFunction(getClMemoryAccessCallbackPrefix(M) + "memcpy",
                            PtrTy, PtrTy, PtrTy, IntptrTy);
  MemProfMemset =
      M.getOrInsertFunction(getClMemoryAccessCallbackPrefix(M) + "memset",
                            PtrTy, PtrTy, IRB.getInt32Ty(), IntptrTy);
}

bool MemProfiler::maybeInsertMemProfInitAtFunctionEntry(Function &F) {
  // For each NSObject descendant having a +load method, this method is invoked
  // by the ObjC runtime before any of the static constructors is called.
  // Therefore we need to instrument such methods with a call to __memprof_init
  // at the beginning in order to initialize our runtime before any access to
  // the shadow memory.
  // We cannot just ignore these methods, because they may call other
  // instrumented functions.
  if (F.getName().contains(" load]")) {
    FunctionCallee MemProfInitFunction =
        declareSanitizerInitFunction(*F.getParent(), MemProfInitName, {});
    IRBuilder<> IRB(&F.front(), F.front().begin());
    IRB.CreateCall(MemProfInitFunction, {});
    return true;
  }
  return false;
}

bool MemProfiler::insertDynamicShadowAtFunctionEntry(Function &F) {
  IRBuilder<> IRB(&F.front().front());
  Value *GlobalDynamicAddress = F.getParent()->getOrInsertGlobal(
      MemProfShadowMemoryDynamicAddress, IntptrTy);
  if (F.getParent()->getPICLevel() == PICLevel::NotPIC)
    cast<GlobalVariable>(GlobalDynamicAddress)->setDSOLocal(true);
  DynamicShadowOffset = IRB.CreateLoad(IntptrTy, GlobalDynamicAddress);
  return true;
}

bool MemProfiler::instrumentFunction(Function &F) {
  if (F.getLinkage() == GlobalValue::AvailableExternallyLinkage)
    return false;
  if (getClDebugFunc(Mod) == F.getName())
    return false;
  if (F.getName().starts_with("__memprof_"))
    return false;

  bool FunctionModified = false;

  // If needed, insert __memprof_init.
  // This function needs to be called even if the function body is not
  // instrumented.
  if (maybeInsertMemProfInitAtFunctionEntry(F))
    FunctionModified = true;

  LLVM_DEBUG(dbgs() << "MEMPROF instrumenting:\n" << F << "\n");

  initializeCallbacks(*F.getParent());

  SmallVector<Instruction *, 16> ToInstrument;

  // Fill the set of memory operations to instrument.
  for (auto &BB : F) {
    for (auto &Inst : BB) {
      if (isInterestingMemoryAccess(&Inst) || isa<MemIntrinsic>(Inst))
        ToInstrument.push_back(&Inst);
    }
  }

  if (ToInstrument.empty()) {
    LLVM_DEBUG(dbgs() << "MEMPROF done instrumenting: " << FunctionModified
                      << " " << F << "\n");

    return FunctionModified;
  }

  FunctionModified |= insertDynamicShadowAtFunctionEntry(F);

  int NumInstrumented = 0;
  for (auto *Inst : ToInstrument) {
    if (getClDebugMin(Mod) < 0 || getClDebugMax(Mod) < 0 ||
        (NumInstrumented >= getClDebugMin(Mod) &&
         NumInstrumented <= getClDebugMax(Mod))) {
      std::optional<InterestingMemoryAccess> Access =
          isInterestingMemoryAccess(Inst);
      if (Access)
        instrumentMop(Inst, F.getDataLayout(), *Access);
      else
        instrumentMemIntrinsic(cast<MemIntrinsic>(Inst));
    }
    NumInstrumented++;
  }

  if (NumInstrumented > 0)
    FunctionModified = true;

  LLVM_DEBUG(dbgs() << "MEMPROF done instrumenting: " << FunctionModified << " "
                    << F << "\n");

  return FunctionModified;
}
