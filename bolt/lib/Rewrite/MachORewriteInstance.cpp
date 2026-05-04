//===- bolt/Rewrite/MachORewriteInstance.cpp - MachO rewriter -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "bolt/Rewrite/MachORewriteInstance.h"
#include "bolt/Core/BinaryContext.h"
#include "bolt/Core/BinaryEmitter.h"
#include "bolt/Core/BinaryFunction.h"
#include "bolt/Core/BoltCoreOptionsOptInfos.h"
#include "bolt/Core/JumpTable.h"
#include "bolt/Core/MCPlusBuilder.h"
#include "bolt/Passes/BoltPassesOptionsOptInfos.h"
#include "bolt/Passes/Instrumentation.h"
#include "bolt/Passes/PatchEntries.h"
#include "bolt/Profile/DataReader.h"
#include "bolt/Rewrite/BinaryPassManager.h"
#include "bolt/Rewrite/BoltRewriteOptionsOptInfos.h"
#include "bolt/Rewrite/ExecutableFileMemoryManager.h"
#include "bolt/Rewrite/JITLinkLinker.h"
#include "bolt/Rewrite/RewriteInstance.h"
#include "bolt/RuntimeLibs/InstrumentationRuntimeLibrary.h"
#include "bolt/Utils/BoltUtilsOptionsOptInfos.h"
#include "bolt/Utils/CommandLineOpts.h"
#include "bolt/Utils/Utils.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ToolOutputFile.h"
#include <memory>
#include <optional>

namespace opts {

using namespace llvm;
// InstrumentCalls is now read from OptionsContext in Instrumentation.cpp
extern bolt::JumpTableSupportLevel JumpTables;
} // namespace opts

namespace llvm {
namespace bolt {

#define DEBUG_TYPE "bolt"

Expected<std::unique_ptr<MachORewriteInstance>>
MachORewriteInstance::create(object::MachOObjectFile *InputFile,
                             StringRef ToolPath,
                             clv2::OptionsContext *OptsCtx) {
  Error Err = Error::success();
  auto MachORI =
      std::make_unique<MachORewriteInstance>(InputFile, ToolPath, Err, OptsCtx);
  if (Err)
    return std::move(Err);
  return std::move(MachORI);
}

MachORewriteInstance::MachORewriteInstance(object::MachOObjectFile *InputFile,
                                           StringRef ToolPath, Error &Err,
                                           clv2::OptionsContext *OptsCtx)
    : InputFile(InputFile), ToolPath(ToolPath) {
  ErrorAsOutParameter EAO(&Err);
  Relocation::Arch = InputFile->makeTriple().getArch();
  auto BCOrErr = BinaryContext::createBinaryContext(
      InputFile->makeTriple(), std::make_shared<orc::SymbolStringPool>(),
      InputFile->getFileName(), nullptr,
      /* IsPIC */ true, DWARFContext::create(*InputFile),
      {llvm::outs(), llvm::errs()}, OptsCtx);
  if (Error E = BCOrErr.takeError()) {
    Err = std::move(E);
    return;
  }
  BC = std::move(BCOrErr.get());
  BC->initializeTarget(std::unique_ptr<MCPlusBuilder>(
      createMCPlusBuilder(BC->TheTriple->getArch(), BC->MIA.get(),
                          BC->MII.get(), BC->MRI.get(), BC->STI.get())));
  bool Instrument = bolt_utils_opts::getInstrument(*BC);
  if (Instrument)
    BC->setRuntimeLibrary(std::make_unique<InstrumentationRuntimeLibrary>());
}

Error MachORewriteInstance::setProfile(StringRef Filename) {
  if (!sys::fs::exists(Filename))
    return errorCodeToError(make_error_code(errc::no_such_file_or_directory));

  if (ProfileReader) {
    // Already exists
    return make_error<StringError>(Twine("multiple profiles specified: ") +
                                       ProfileReader->getFilename() + " and " +
                                       Filename,
                                   inconvertibleErrorCode());
  }

  ProfileReader = std::make_unique<DataReader>(Filename);
  return Error::success();
}

void MachORewriteInstance::preprocessProfileData() {
  if (!ProfileReader)
    return;
  if (Error E = ProfileReader->preprocessProfile(*BC))
    report_error("cannot pre-process profile", std::move(E));
}

void MachORewriteInstance::processProfileDataPreCFG() {
  if (!ProfileReader)
    return;
  if (Error E = ProfileReader->readProfilePreCFG(*BC))
    report_error("cannot read profile pre-CFG", std::move(E));
}

void MachORewriteInstance::processProfileData() {
  if (!ProfileReader)
    return;
  if (Error E = ProfileReader->readProfile(*BC))
    report_error("cannot read profile", std::move(E));
}

void MachORewriteInstance::readSpecialSections() {
  for (const object::SectionRef &Section : InputFile->sections()) {
    Expected<StringRef> SectionName = Section.getName();
    ;
    check_error(SectionName.takeError(), "cannot get section name");
    // Only register sections with names.
    if (!SectionName->empty()) {
      BC->registerSection(Section);
      LLVM_DEBUG(
          dbgs() << "BOLT-DEBUG: registering section " << *SectionName
                 << " @ 0x" << Twine::utohexstr(Section.getAddress()) << ":0x"
                 << Twine::utohexstr(Section.getAddress() + Section.getSize())
                 << "\n");
    }
  }

  bool PrintSections = bolt_utils_opts::getPrintSections(*BC);
  if (PrintSections) {
    outs() << "BOLT-INFO: Sections from original binary:\n";
    BC->printSections(outs());
  }
}

namespace {

struct DataInCodeRegion {
  explicit DataInCodeRegion(DiceRef D) {
    D.getOffset(Offset);
    D.getLength(Length);
    D.getKind(Kind);
  }

  uint32_t Offset;
  uint16_t Length;
  uint16_t Kind;
};

std::vector<DataInCodeRegion> readDataInCode(const MachOObjectFile &O) {
  const MachO::linkedit_data_command DataInCodeLC =
      O.getDataInCodeLoadCommand();
  const uint32_t NumberOfEntries =
      DataInCodeLC.datasize / sizeof(MachO::data_in_code_entry);
  std::vector<DataInCodeRegion> DataInCode;
  DataInCode.reserve(NumberOfEntries);
  for (auto I = O.begin_dices(), E = O.end_dices(); I != E; ++I)
    DataInCode.emplace_back(*I);
  llvm::stable_sort(DataInCode, [](DataInCodeRegion LHS, DataInCodeRegion RHS) {
    return LHS.Offset < RHS.Offset;
  });
  return DataInCode;
}

std::optional<uint64_t> readStartAddress(const MachOObjectFile &O) {
  std::optional<uint64_t> StartOffset;
  std::optional<uint64_t> TextVMAddr;
  for (const object::MachOObjectFile::LoadCommandInfo &LC : O.load_commands()) {
    switch (LC.C.cmd) {
    case MachO::LC_MAIN: {
      MachO::entry_point_command LCMain = O.getEntryPointCommand(LC);
      StartOffset = LCMain.entryoff;
      break;
    }
    case MachO::LC_SEGMENT: {
      MachO::segment_command LCSeg = O.getSegmentLoadCommand(LC);
      StringRef SegmentName(LCSeg.segname,
                            strnlen(LCSeg.segname, sizeof(LCSeg.segname)));
      if (SegmentName == "__TEXT")
        TextVMAddr = LCSeg.vmaddr;
      break;
    }
    case MachO::LC_SEGMENT_64: {
      MachO::segment_command_64 LCSeg = O.getSegment64LoadCommand(LC);
      StringRef SegmentName(LCSeg.segname,
                            strnlen(LCSeg.segname, sizeof(LCSeg.segname)));
      if (SegmentName == "__TEXT")
        TextVMAddr = LCSeg.vmaddr;
      break;
    }
    default:
      continue;
    }
  }
  return (TextVMAddr && StartOffset)
             ? std::optional<uint64_t>(*TextVMAddr + *StartOffset)
             : std::nullopt;
}

} // anonymous namespace

void MachORewriteInstance::discoverFileObjects() {
  bool Instrument = bolt_utils_opts::getInstrument(*BC);
  std::vector<SymbolRef> FunctionSymbols;
  for (const SymbolRef &S : InputFile->symbols()) {
    SymbolRef::Type Type = cantFail(S.getType(), "cannot get symbol type");
    if (Type == SymbolRef::ST_Function)
      FunctionSymbols.push_back(S);
  }
  if (FunctionSymbols.empty())
    return;
  llvm::stable_sort(
      FunctionSymbols, [](const SymbolRef &LHS, const SymbolRef &RHS) {
        return cantFail(LHS.getValue()) < cantFail(RHS.getValue());
      });
  for (size_t Index = 0; Index < FunctionSymbols.size(); ++Index) {
    const uint64_t Address = cantFail(FunctionSymbols[Index].getValue());
    ErrorOr<BinarySection &> Section = BC->getSectionForAddress(Address);
    // TODO: It happens for some symbols (e.g. __mh_execute_header).
    // Add proper logic to handle them correctly.
    if (!Section) {
      errs() << "BOLT-WARNING: no section found for address " << Address
             << "\n";
      continue;
    }

    std::string SymbolName =
        cantFail(FunctionSymbols[Index].getName(), "cannot get symbol name")
            .str();
    // Uniquify names of local symbols.
    if (!(cantFail(FunctionSymbols[Index].getFlags()) & SymbolRef::SF_Global))
      SymbolName = NR.uniquify(SymbolName);

    section_iterator S = cantFail(FunctionSymbols[Index].getSection());
    uint64_t EndAddress = S->getAddress() + S->getSize();

    size_t NFIndex = Index + 1;
    // Skip aliases.
    while (NFIndex < FunctionSymbols.size() &&
           cantFail(FunctionSymbols[NFIndex].getValue()) == Address)
      ++NFIndex;
    if (NFIndex < FunctionSymbols.size() &&
        S == cantFail(FunctionSymbols[NFIndex].getSection()))
      EndAddress = cantFail(FunctionSymbols[NFIndex].getValue());

    const uint64_t SymbolSize = EndAddress - Address;
    const auto It = BC->getBinaryFunctions().find(Address);
    if (It == BC->getBinaryFunctions().end()) {
      BinaryFunction *Function = BC->createBinaryFunction(
          std::move(SymbolName), *Section, Address, SymbolSize);
      if (!Instrument)
        Function->setOutputAddress(Function->getAddress());

    } else {
      It->second.addAlternativeName(std::move(SymbolName));
    }
  }

  const std::vector<DataInCodeRegion> DataInCode = readDataInCode(*InputFile);

  for (auto &BFI : BC->getBinaryFunctions()) {
    BinaryFunction &Function = BFI.second;
    Function.setMaxSize(Function.getSize());

    ErrorOr<ArrayRef<uint8_t>> FunctionData = Function.getData();
    if (!FunctionData) {
      errs() << "BOLT-ERROR: corresponding section is non-executable or "
             << "empty for function " << Function << '\n';
      continue;
    }

    // Treat zero-sized functions as non-simple ones.
    if (Function.getSize() == 0) {
      Function.setSimple(false);
      continue;
    }

    // Offset of the function in the file.
    const auto *FileBegin =
        reinterpret_cast<const uint8_t *>(InputFile->getData().data());
    Function.setFileOffset(FunctionData->begin() - FileBegin);

    // Treat functions which contain data in code as non-simple ones.
    const auto It = std::lower_bound(
        DataInCode.cbegin(), DataInCode.cend(), Function.getFileOffset(),
        [](DataInCodeRegion D, uint64_t Offset) { return D.Offset < Offset; });
    if (It != DataInCode.cend() &&
        It->Offset + It->Length <=
            Function.getFileOffset() + Function.getMaxSize())
      Function.setSimple(false);
  }

  BC->StartFunctionAddress = readStartAddress(*InputFile);
}

void MachORewriteInstance::disassembleFunctions() {
  for (auto &BFI : BC->getBinaryFunctions()) {
    BinaryFunction &Function = BFI.second;
    if (!Function.isSimple())
      continue;
    BC->logBOLTErrorsAndQuitOnFatal(Function.disassemble());
    if (bolt_rewrite_opts::getPrintDisasm(*BC))
      Function.print(outs(), "after disassembly");
  }
}

void MachORewriteInstance::buildFunctionsCFG() {
  for (auto &BFI : BC->getBinaryFunctions()) {
    BinaryFunction &Function = BFI.second;
    if (!Function.isSimple())
      continue;
    BC->logBOLTErrorsAndQuitOnFatal(Function.buildCFG(/*AllocId*/ 0));
  }
}

void MachORewriteInstance::postProcessFunctions() {
  for (auto &BFI : BC->getBinaryFunctions()) {
    BinaryFunction &Function = BFI.second;
    if (Function.empty())
      continue;
    Function.postProcessCFG();
    if (bolt_rewrite_opts::getPrintCfg(*BC))
      Function.print(outs(), "after building cfg");
  }
}

void MachORewriteInstance::runOptimizationPasses() {
  bool Instrument = bolt_utils_opts::getInstrument(*BC);
  bool NeverPrint = bolt_rewrite_opts::getNeverPrint(*BC);
  bool PrintNormalized = bolt_rewrite_opts::getPrintNormalized(*BC);
  bool PrintReordered = bolt_rewrite_opts::getPrintReordered(*BC);
  bool PrintAfterBranchFixup = bolt_rewrite_opts::getPrintAfterBranchFixup(*BC);
  bool PrintFinalized = bolt_rewrite_opts::getPrintFinalized(*BC);
  BinaryFunctionPassManager Manager(*BC);
  if (Instrument) {
    Manager.registerPass(std::make_unique<PatchEntries>());
    Manager.registerPass(std::make_unique<Instrumentation>(NeverPrint));
  }

  Manager.registerPass(std::make_unique<ShortenInstructions>(NeverPrint));

  Manager.registerPass(std::make_unique<RemoveNops>(NeverPrint));

  Manager.registerPass(std::make_unique<NormalizeCFG>(PrintNormalized));

  Manager.registerPass(std::make_unique<ReorderBasicBlocks>(PrintReordered));
  Manager.registerPass(std::make_unique<FixupBranches>(PrintAfterBranchFixup));
  Manager.registerPass(std::make_unique<PopulateOutputFunctions>());
  // This pass should always run last.*
  Manager.registerPass(std::make_unique<FinalizeFunctions>(PrintFinalized));

  BC->logBOLTErrorsAndQuitOnFatal(Manager.runPasses());
}

void MachORewriteInstance::mapInstrumentationSection(
    StringRef SectionName, BOLTLinker::SectionMapper MapSection) {
  bool Instrument = bolt_utils_opts::getInstrument(*BC);
  if (!Instrument)
    return;
  ErrorOr<BinarySection &> Section = BC->getUniqueSectionByName(SectionName);
  if (!Section) {
    llvm::errs() << "Cannot find " + SectionName + " section\n";
    exit(1);
  }
  if (!Section->hasValidSectionID())
    return;
  MapSection(*Section, Section->getAddress());
}

void MachORewriteInstance::mapCodeSections(
    BOLTLinker::SectionMapper MapSection) {
  for (BinaryFunction *Function : BC->getAllBinaryFunctions()) {
    if (!Function->isEmitted())
      continue;
    if (Function->getOutputAddress() == 0)
      continue;
    ErrorOr<BinarySection &> FuncSection = Function->getCodeSection();
    if (!FuncSection)
      report_error(
          (Twine("Cannot find section for function ") + Function->getOneName())
              .str(),
          FuncSection.getError());

    FuncSection->setOutputAddress(Function->getOutputAddress());
    LLVM_DEBUG(
        dbgs() << "BOLT: mapping 0x"
               << Twine::utohexstr(FuncSection->getAllocAddress()) << " to 0x"
               << Twine::utohexstr(Function->getOutputAddress()) << '\n');
    MapSection(*FuncSection, Function->getOutputAddress());
    Function->setImageAddress(FuncSection->getAllocAddress());
    Function->setImageSize(FuncSection->getOutputSize());
  }

  bool Instrument = bolt_utils_opts::getInstrument(*BC);
  if (Instrument) {
    ErrorOr<BinarySection &> BOLT = BC->getUniqueSectionByName("__bolt");
    if (!BOLT) {
      llvm::errs() << "Cannot find __bolt section\n";
      exit(1);
    }
    uint64_t Addr = BOLT->getAddress();
    for (BinaryFunction *Function : BC->getAllBinaryFunctions()) {
      if (!Function->isEmitted())
        continue;
      if (Function->getOutputAddress() != 0)
        continue;
      ErrorOr<BinarySection &> FuncSection = Function->getCodeSection();
      assert(FuncSection && "cannot find section for function");
      Addr = llvm::alignTo(Addr, 4);
      FuncSection->setOutputAddress(Addr);
      MapSection(*FuncSection, Addr);
      Function->setFileOffset(Addr - BOLT->getAddress() +
                              BOLT->getInputFileOffset());
      Function->setImageAddress(FuncSection->getAllocAddress());
      Function->setImageSize(FuncSection->getOutputSize());
      BC->registerNameAtAddress(Function->getOneName(), Addr, 0, 0);
      Addr += FuncSection->getOutputSize();
    }
  }
}

void MachORewriteInstance::emitAndLink() {
  std::string OutputFilename = bolt_utils_opts::getO(*BC);
  std::error_code EC;
  std::unique_ptr<::llvm::ToolOutputFile> TempOut =
      std::make_unique<::llvm::ToolOutputFile>(OutputFilename + ".bolt.o", EC,
                                               sys::fs::OF_None);
  check_error(EC, "cannot create output object file");

  if (bolt_rewrite_opts::getKeepTmp(*BC))
    TempOut->keep();

  std::unique_ptr<buffer_ostream> BOS =
      std::make_unique<buffer_ostream>(TempOut->os());
  raw_pwrite_stream *OS = BOS.get();
  auto Streamer = BC->createStreamer(*OS);

  emitBinaryContext(*Streamer, *BC, getOrgSecPrefix());
  Streamer->finish();

  std::unique_ptr<MemoryBuffer> ObjectMemBuffer =
      MemoryBuffer::getMemBuffer(BOS->str(), "in-memory object file", false);
  std::unique_ptr<object::ObjectFile> Obj = cantFail(
      object::ObjectFile::createObjectFile(ObjectMemBuffer->getMemBufferRef()),
      "error creating in-memory object");
  assert(Obj && "createObjectFile cannot return nullptr");

  auto EFMM = std::make_unique<ExecutableFileMemoryManager>(*BC);
  EFMM->setNewSecPrefix(getNewSecPrefix());
  EFMM->setOrgSecPrefix(getOrgSecPrefix());

  Linker = std::make_unique<JITLinkLinker>(*BC, std::move(EFMM));
  Linker->loadObject(ObjectMemBuffer->getMemBufferRef(),
                     [this](auto MapSection) {
                       // Assign addresses to all sections. If key corresponds
                       // to the object created by ourselves, call our regular
                       // mapping function. If we are loading additional objects
                       // as part of runtime libraries for instrumentation,
                       // treat them as extra sections.
                       mapCodeSections(MapSection);
                       mapInstrumentationSection("__counters", MapSection);
                       mapInstrumentationSection("__tables", MapSection);
                     });

  // TODO: Refactor addRuntimeLibSections to work properly on Mach-O
  // and use it here.
  // if (auto *RtLibrary = BC->getRuntimeLibrary()) {
  //   RtLibrary->link(*BC, ToolPath, *Linker, [this](auto MapSection) {
  //     mapInstrumentationSection("I__setup", MapSection);
  //     mapInstrumentationSection("I__fini", MapSection);
  //     mapInstrumentationSection("I__data", MapSection);
  //     mapInstrumentationSection("I__text", MapSection);
  //     mapInstrumentationSection("I__cstring", MapSection);
  //     mapInstrumentationSection("I__literal16", MapSection);
  //   });
  // }
}

void MachORewriteInstance::writeInstrumentationSection(StringRef SectionName,
                                                       raw_fd_ostream &OS) {
  bool Instrument = bolt_utils_opts::getInstrument(*BC);
  if (!Instrument)
    return;
  ErrorOr<BinarySection &> Section = BC->getUniqueSectionByName(SectionName);
  if (!Section) {
    llvm::errs() << "Cannot find " + SectionName + " section\n";
    exit(1);
  }
  if (!Section->hasValidSectionID())
    return;
  assert(Section->getInputFileOffset() &&
         "Section input offset cannot be zero");
  assert(Section->getAllocAddress() && "Section alloc address cannot be zero");
  assert(Section->getOutputSize() && "Section output size cannot be zero");
  safePWrite(OS, reinterpret_cast<char *>(Section->getAllocAddress()),
             Section->getOutputSize(), Section->getInputFileOffset());
}

void MachORewriteInstance::rewriteFile() {
  bool Instrument = bolt_utils_opts::getInstrument(*BC);
  std::string OutputFilename = bolt_utils_opts::getO(*BC);
  std::error_code EC;
  Out = std::make_unique<ToolOutputFile>(OutputFilename, EC, sys::fs::OF_None);
  check_error(EC, "cannot create output executable file");
  raw_fd_ostream &OS = Out->os();
  OS << InputFile->getData();

  for (auto &BFI : BC->getBinaryFunctions()) {
    BinaryFunction &Function = BFI.second;
    if (!Function.isSimple())
      continue;
    assert(Function.isEmitted() && "Simple function has not been emitted");
    if (!Instrument && (Function.getImageSize() > Function.getMaxSize()))
      continue;
    if (opts::getVerbosity(*BC) >= 2)
      outs() << "BOLT: rewriting function \"" << Function << "\"\n";
    safePWrite(OS, reinterpret_cast<char *>(Function.getImageAddress()),
               Function.getImageSize(), Function.getFileOffset());
  }

  for (const BinaryFunction *Function : BC->getInjectedBinaryFunctions()) {
    safePWrite(OS, reinterpret_cast<char *>(Function->getImageAddress()),
               Function->getImageSize(), Function->getFileOffset());
  }

  writeInstrumentationSection("__counters", OS);
  writeInstrumentationSection("__tables", OS);

  // TODO: Refactor addRuntimeLibSections to work properly on Mach-O and
  // use it here.
  writeInstrumentationSection("I__setup", OS);
  writeInstrumentationSection("I__fini", OS);
  writeInstrumentationSection("I__data", OS);
  writeInstrumentationSection("I__text", OS);
  writeInstrumentationSection("I__cstring", OS);
  writeInstrumentationSection("I__literal16", OS);

  Out->keep();
  EC = sys::fs::setPermissions(
      OutputFilename, static_cast<sys::fs::perms>(sys::fs::perms::all_all &
                                                  ~sys::fs::getUmask()));
  check_error(EC, "cannot set permissions of output file");
}

void MachORewriteInstance::adjustCommandLineOptions() {
  // FIXME! Upstream change
  //   opts::CheckOverlappingElements = false;
  auto *UtilOpts = bolt_utils_opts::getBoltUtilsOpts(BC->getOptionsContext());
  bool AlignTextSpecified = UtilOpts->specified<&clv2::BOLT_AlignText>();
  if (!AlignTextSpecified) {
    if (auto *V = BC->getOptionsContext().getViewPtr<&clv2::BoltUtilsOptsReg>())
      V->get<&clv2::BOLT_AlignText>() = BC->PageAlign;
  }

  // Mirror alignment-related command line options onto BinaryContext so passes
  // and the emitter can read them via BC instead of touching opts::*.
  unsigned AlignFunctions = UtilOpts->get<&clv2::BOLT_AlignFunctions>();
  {
    BC->AlignText = UtilOpts->get<&clv2::BOLT_AlignText>();
    BC->AlignFunctions = AlignFunctions;
    BC->AlignBlocks = bolt::bolt_core_opts::getAlignBlocks(*BC);
    BC->AlignBlocksMinSize = bolt::bolt_passes_opts::getAlignBlocksMinSize(*BC);
    BC->AlignBlocksThreshold =
        bolt::bolt_passes_opts::getAlignBlocksThreshold(*BC);
    BC->AlignFunctionsMaxBytes =
        bolt::bolt_passes_opts::getAlignFunctionsMaxBytes(*BC);
    BC->BlockAlignment = bolt::bolt_passes_opts::getBlockAlignment(*BC);
    BC->PreserveBlocksAlignment =
        bolt::bolt_core_opts::getPreserveBlocksAlignment(*BC);
    BC->UseCompactAligner = bolt::bolt_passes_opts::getUseCompactAligner(*BC);
    BC->X86AlignBranchBoundaryHotOnly =
        bolt::bolt_core_opts::getX86AlignBranchBoundaryHotOnly(*BC);
  }

  bool InstrumentSpecified = UtilOpts->specified<&clv2::BOLT_Instrument>();
  if (InstrumentSpecified) {
    if (auto *V = BC->getOptionsContext().getViewPtr<&clv2::BoltUtilsOptsReg>())
      V->get<&clv2::BOLT_ForcePatch>() = true;
  }
  if (auto *V = BC->getOptionsContext().getViewPtr<&clv2::BoltCoreOptsReg>())
    V->get<&clv2::BOLTCORE_JumpTables>() =
        clv2::BoltCoreJumpTableSupportLevel::JTS_MOVE;
  // TODO: InstrumentCalls=false for MachO was handled by setting a global.
  // Now that Instrumentation reads from OptionsContext, this override
  // needs to be plumbed through the OptionsContext mechanism instead.
}

void MachORewriteInstance::run() {
  adjustCommandLineOptions();

  readSpecialSections();

  discoverFileObjects();

  preprocessProfileData();

  disassembleFunctions();

  processProfileDataPreCFG();

  buildFunctionsCFG();

  processProfileData();

  postProcessFunctions();

  runOptimizationPasses();

  emitAndLink();

  rewriteFile();
}

MachORewriteInstance::~MachORewriteInstance() {}

} // namespace bolt
} // namespace llvm
