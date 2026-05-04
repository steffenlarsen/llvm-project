//===- llvm-ir2vec.cpp - IR2Vec/MIR2Vec Embedding Generation Tool --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the IR2Vec and MIR2Vec embedding generation tool.
///
/// This tool supports two modes:
/// - LLVM IR mode (-mode=llvm): Process LLVM IR
/// - Machine IR mode (-mode=mir): Process Machine IR
///
/// Available subcommands:
///
/// 1. Triplet Generation (triplets):
///    Generates numeric triplets (head, tail, relation) for vocabulary
///    training. Output format: MAX_RELATION=N header followed by
///    head\ttail\trelation lines. Relations: 0=Type, 1=Next, 2+=Arg0,Arg1,...
///
///    For LLVM IR:
///      llvm-ir2vec triplets input.bc -o train2id.txt
///
///    For Machine IR:
///      llvm-ir2vec triplets -mode=mir input.mir -o train2id.txt
///
/// 2. Entity Mappings (entities):
///    Generates entity mappings for vocabulary training.
///    Output format: <total_entities> header followed by entity\tid lines.
///
///    For LLVM IR:
///      llvm-ir2vec entities input.bc -o entity2id.txt
///
///    For Machine IR:
///      llvm-ir2vec entities -mode=mir input.mir -o entity2id.txt
///
/// 3. Embedding Generation (embeddings):
///    Generates IR2Vec/MIR2Vec embeddings using a trained vocabulary.
///
///    For LLVM IR:
///      llvm-ir2vec embeddings --ir2vec-vocab-path=vocab.json
///        --ir2vec-kind=<kind> --level=<level> input.bc -o embeddings.txt
///      Kind: --ir2vec-kind=symbolic (default), --ir2vec-kind=flow-aware
///
///    For Machine IR:
///      llvm-ir2vec embeddings -mode=mir --mir2vec-vocab-path=vocab.json
///        --level=<level> input.mir -o embeddings.txt
///
///    Levels: --level=inst (instructions), --level=bb (basic blocks),
///    --level=func (functions) (See IR2Vec.cpp/MIR2Vec.cpp for more embedding
///    generation options)
///
//===----------------------------------------------------------------------===//

#include "lib/Utils.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Analysis/AnalysisOptionsOptInfos.h"
#include "llvm/Analysis/IR2Vec.h"
#include "llvm/CodeGen/CodeGenPassOptionsOptInfos.h"
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/CodeGen/CommandFlagsOptInfos.h"
#include "llvm/CodeGen/MIR2Vec.h"
#include "llvm/CodeGen/MIRParser/MIRParser.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassInstrumentation.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"

#define DEBUG_TYPE "ir2vec"

namespace llvm {

enum IRKind {
  LLVMIR = 0, ///< LLVM IR
  MIR         ///< Machine IR
};

// clv2 option descriptors

inline constexpr clv2::OptionCategory CommonCategory{
    "Common Options", "Options applicable to both IR2Vec and MIR2Vec modes"};

inline constexpr clv2::EnumVal<IRKind> IRKindVals[] = {
    {"llvm", LLVMIR, "Process LLVM IR"},
    {"mir", MIR, "Process Machine IR"},
};
inline constexpr auto IRModeOpt =
    clv2::makeEnumOption<IRKind>("mode", "Tool operation mode:", IRKindVals,
                                 clv2::Init{LLVMIR}, clv2::cat(CommonCategory));

inline constexpr clv2::OptionInfo<std::string> OutputFilenameOpt{
    "o", "Output filename", clv2::value_desc("filename"), clv2::Init{"-"},
    clv2::cat(CommonCategory)};

// Subcommand shared positional
inline constexpr clv2::OptionInfo<std::string> InputFilenameOpt{
    "input", "<input bitcode/MIR file or '-' for stdin>", clv2::Positional{},
    clv2::Init{"-"}};

// Embedding-specific options
inline constexpr clv2::OptionInfo<std::string> FunctionNameOpt{
    "function", "Process specific function only", clv2::value_desc("name"),
    clv2::Init{""}};

inline constexpr clv2::EnumVal<EmbeddingLevel> LevelVals[] = {
    {"inst", InstructionLevel, "Generate instruction-level embeddings"},
    {"bb", BasicBlockLevel, "Generate basic block-level embeddings"},
    {"func", FunctionLevel, "Generate function-level embeddings"},
};
inline constexpr auto LevelOpt = clv2::makeEnumOption<EmbeddingLevel>(
    "level", "Embedding generation level:", LevelVals,
    clv2::Init{FunctionLevel});

// Subcommands
inline constexpr clv2::SubCommandInfo<&InputFilenameOpt> TripletsCmd{
    "triplets", "Generate triplets for vocabulary training"};
inline constexpr clv2::SubCommandInfo<&InputFilenameOpt> EntitiesCmd{
    "entities", "Generate entity mappings for vocabulary training"};
inline constexpr clv2::SubCommandInfo<&InputFilenameOpt, &FunctionNameOpt,
                                      &LevelOpt>
    EmbeddingsCmd{"embeddings", "Generate embeddings using trained vocabulary"};

inline constexpr clv2::OptionsRegistry<
    &IRModeOpt, &OutputFilenameOpt, &TripletsCmd, &EntitiesCmd, &EmbeddingsCmd>
    IR2VecToolReg;

namespace ir2vec {

/// Process the module and generate output based on selected subcommand.
/// isEmbeddings: true = embeddings mode, false = triplets mode.
static Error processModule(Module &M, raw_ostream &OS, bool isEmbeddings,
                           StringRef FuncName, EmbeddingLevel Lvl) {
  const auto &Ctx = M.getContext().getOptionsContext();
  IR2VecTool Tool(M);

  if (isEmbeddings) {
    // Initialize vocabulary for embedding generation
    // Note: Requires --ir2vec-vocab-path option to be set
    // and this value will be populated in the var VocabFile
    if (ir2vec::getVocabFile(Ctx).empty()) {
      return createStringError(
          errc::invalid_argument,
          "IR2Vec vocabulary file path not specified; "
          "You may need to set it using --ir2vec-vocab-path");
    }

    std::shared_ptr<Vocabulary> Vocab;
    if (auto Err =
            ir2vec::loadVocabulary(ir2vec::getVocabFile(Ctx)).moveInto(Vocab))
      return Err;
    if (auto Err = Tool.setVocabulary(std::move(Vocab)))
      return Err;

    if (!FuncName.empty()) {
      // Process single function
      if (const Function *F = M.getFunction(FuncName))
        Tool.writeEmbeddingsToStream(*F, OS, Lvl);
      else
        return createStringError(errc::invalid_argument,
                                 "Function '%s' not found",
                                 FuncName.str().c_str());
    } else {
      // Process all functions
      Tool.writeEmbeddingsToStream(OS, Lvl);
    }
  } else {
    // Both triplets and entities use triplet generation
    Tool.writeTripletsToStream(OS);
  }
  return Error::success();
}
} // namespace ir2vec

namespace mir2vec {

/// Setup MIR context from input file
static Error setupMIRContext(const std::string &InputFile, MIRContext &Ctx) {
  SMDiagnostic Err;

  auto MIR = createMIRParserFromFile(InputFile, Err, Ctx.Context);
  if (!MIR) {
    Err.print(ToolName, errs());
    return createStringError(errc::invalid_argument,
                             "Failed to parse MIR file");
  }

  auto SetDataLayout = [&](StringRef DataLayoutTargetTriple,
                           StringRef OldDLStr) -> std::optional<std::string> {
    std::string IRTargetTriple = DataLayoutTargetTriple.str();
    Triple TheTriple = Triple(IRTargetTriple);
    if (TheTriple.getTriple().empty())
      TheTriple.setTriple(sys::getDefaultTargetTriple());

    auto TMOrErr = codegen::createTargetMachineForTriple(
        TheTriple,
        /*OptsCtx=*/llvm::clv2::defaultOptionsContext());
    if (!TMOrErr) {
      Err.print(ToolName, errs());
      exit(1); // Match original behavior
    }
    Ctx.TM = std::move(*TMOrErr);
    return Ctx.TM->createDataLayout().getStringRepresentation();
  };

  Ctx.M = MIR->parseIRModule(SetDataLayout);
  if (!Ctx.M) {
    Err.print(ToolName, errs());
    return createStringError(errc::invalid_argument,
                             "Failed to parse IR module");
  }

  Ctx.MMI = std::make_unique<MachineModuleInfo>(Ctx.TM.get());
  if (!Ctx.MMI || MIR->parseMachineFunctions(*Ctx.M, *Ctx.MMI)) {
    Err.print(ToolName, errs());
    return createStringError(errc::invalid_argument,
                             "Failed to parse machine functions");
  }

  return Error::success();
}

/// Generic vocabulary initialization and processing
template <typename ProcessFunc>
static Error processWithVocabulary(MIRContext &Ctx, raw_ostream &OS,
                                   bool useLayoutVocab, ProcessFunc processFn) {
  MIR2VecTool Tool(*Ctx.MMI);

  // Initialize appropriate vocabulary type
  bool success = useLayoutVocab ? Tool.initializeVocabularyForLayout(*Ctx.M)
                                : Tool.initializeVocabulary(*Ctx.M);

  if (!success) {
    WithColor::error(errs(), ToolName)
        << "Failed to initialize MIR2Vec vocabulary"
        << (useLayoutVocab ? " for layout" : "") << ".\n";
    return createStringError(errc::invalid_argument,
                             "Vocabulary initialization failed");
  }

  assert(Tool.getVocabulary() &&
         "MIR2Vec vocabulary should be initialized at this point");

  LLVM_DEBUG(dbgs() << "MIR2Vec vocabulary loaded successfully.\n"
                    << "Vocabulary dimension: "
                    << Tool.getVocabulary()->getDimension() << "\n"
                    << "Vocabulary size: "
                    << Tool.getVocabulary()->getCanonicalSize() << "\n");

  // Execute the specific processing logic
  return processFn(Tool);
}

/// Process module for triplet generation
static Error processModuleForTriplets(MIRContext &Ctx, raw_ostream &OS) {
  return processWithVocabulary(Ctx, OS, /*useLayoutVocab=*/true,
                               [&](MIR2VecTool &Tool) -> Error {
                                 Tool.writeTripletsToStream(*Ctx.M, OS);
                                 return Error::success();
                               });
}

/// Process module for entity generation
static Error processModuleForEntities(MIRContext &Ctx, raw_ostream &OS) {
  return processWithVocabulary(Ctx, OS, /*useLayoutVocab=*/true,
                               [&](MIR2VecTool &Tool) -> Error {
                                 Tool.writeEntitiesToStream(OS);
                                 return Error::success();
                               });
}

/// Process module for embedding generation
static Error processModuleForEmbeddings(MIRContext &Ctx, raw_ostream &OS,
                                        StringRef FuncName,
                                        EmbeddingLevel Lvl) {
  return processWithVocabulary(
      Ctx, OS, /*useLayoutVocab=*/false, [&](MIR2VecTool &Tool) -> Error {
        if (!FuncName.empty()) {
          // Process single function
          Function *F = Ctx.M->getFunction(FuncName);
          if (!F) {
            WithColor::error(errs(), ToolName)
                << "Function '" << FuncName << "' not found\n";
            return createStringError(errc::invalid_argument,
                                     "Function not found");
          }

          MachineFunction *MF = Ctx.MMI->getMachineFunction(*F);
          if (!MF) {
            WithColor::error(errs(), ToolName)
                << "No MachineFunction for " << FuncName << "\n";
            return createStringError(errc::invalid_argument,
                                     "No MachineFunction");
          }

          Tool.writeEmbeddingsToStream(*MF, OS, Lvl);
        } else {
          // Process all functions
          Tool.writeEmbeddingsToStream(*Ctx.M, OS, Lvl);
        }
        return Error::success();
      });
}

enum class ActiveSubCmd { Triplets, Entities, Embeddings };

/// Main entry point for MIR processing
static Error processModule(const std::string &InputFile, raw_ostream &OS,
                           ActiveSubCmd Cmd, StringRef FuncName,
                           EmbeddingLevel Lvl,
                           const clv2::OptionsContext &OptsCtx) {
  MIRContext Ctx(OptsCtx);

  // Setup MIR context (parse file, setup target machine, etc.)
  if (auto Err = setupMIRContext(InputFile, Ctx))
    return Err;

  // Process based on subcommand
  switch (Cmd) {
  case ActiveSubCmd::Triplets:
    return processModuleForTriplets(Ctx, OS);
  case ActiveSubCmd::Entities:
    return processModuleForEntities(Ctx, OS);
  case ActiveSubCmd::Embeddings:
    return processModuleForEmbeddings(Ctx, OS, FuncName, Lvl);
  }
  llvm_unreachable("unhandled subcommand");
}

} // namespace mir2vec

} // namespace llvm

int main(int argc, char **argv) {
  using namespace llvm;
  using namespace llvm::ir2vec;
  using namespace llvm::mir2vec;

  InitLLVM X(argc, argv);

  clv2::OptionParser P;
  P.add<&IR2VecToolReg>();
  P.add<&clv2::AnalysisOptsReg>();
  RegisterCoreLLVMOptions(P);
  P.enableGlobalDynamicEntries();
  const clv2::OptionCategory *Cats[] = {&CommonCategory, &clv2::IR2VecCategory,
                                        &clv2::MIR2VecCategory};
  P.hideUnrelatedOptions(Cats);
  P.showOptions({"mir2vec-common-operand-weight", "mir2vec-kind",
                 "mir2vec-opc-weight", "mir2vec-print-all-vocab-entries",
                 "mir2vec-reg-operand-weight", "mir2vec-vocab-path"});
  auto OptsCtx = P.parse(
      argc, argv,
      "IR2Vec/MIR2Vec - Embedding Generation Tool\n"
      "Generates embeddings for a given LLVM IR or MIR and "
      "supports triplet generation for vocabulary "
      "training and embedding generation.\n\n"
      "See https://llvm.org/docs/CommandGuide/llvm-ir2vec.html for more "
      "information.\n");
  auto *Opts = OptsCtx->getViewPtr<&IR2VecToolReg>();

  // Analysis options need no forwarding: the Module's LLVMContext below is
  // built from *OptsCtx, so ir2vec's getters read the parsed values directly.

  auto IRMode = Opts->get<&IRModeOpt>();
  std::string OutputFile = Opts->get<&OutputFilenameOpt>();

  std::error_code EC;
  raw_fd_ostream OS(OutputFile, EC);
  if (EC) {
    WithColor::error(errs(), ToolName)
        << "opening output file: " << EC.message() << "\n";
    return 1;
  }

  // Determine which subcommand is active.
  ActiveSubCmd Cmd;
  std::string InputFile;
  std::string FuncName;
  EmbeddingLevel Lvl = FunctionLevel;

  if (Opts->isActive<&TripletsCmd>()) {
    Cmd = ActiveSubCmd::Triplets;
    InputFile = Opts->getSubOptions<&TripletsCmd>().get<&InputFilenameOpt>();
  } else if (Opts->isActive<&EntitiesCmd>()) {
    Cmd = ActiveSubCmd::Entities;
    InputFile = Opts->getSubOptions<&EntitiesCmd>().get<&InputFilenameOpt>();
  } else if (Opts->isActive<&EmbeddingsCmd>()) {
    Cmd = ActiveSubCmd::Embeddings;
    auto &ESub = Opts->getSubOptions<&EmbeddingsCmd>();
    InputFile = ESub.get<&InputFilenameOpt>();
    FuncName = ESub.get<&FunctionNameOpt>();
    Lvl = ESub.get<&LevelOpt>();
  } else {
    errs() << "No subcommand specified. Use --help for usage.\n";
    return 1;
  }

  if (IRMode == IRKind::LLVMIR) {
    if (Cmd == ActiveSubCmd::Entities) {
      // Just dump entity mappings without processing any IR
      IR2VecTool::writeEntitiesToStream(OS);
      return 0;
    }

    // Parse the input LLVM IR file or stdin
    SMDiagnostic Err;
    LLVMContext Context(*OptsCtx);
    std::unique_ptr<Module> M = parseIRFile(InputFile, Err, Context);
    if (!M) {
      Err.print(ToolName, errs());
      return 1;
    }

    bool isEmbeddings = (Cmd == ActiveSubCmd::Embeddings);
    if (Error Err =
            ir2vec::processModule(*M, OS, isEmbeddings, FuncName, Lvl)) {
      handleAllErrors(std::move(Err), [&](const ErrorInfoBase &EIB) {
        WithColor::error(errs(), ToolName) << EIB.message() << "\n";
      });
      return 1;
    }
    return 0;
  }
  if (IRMode == IRKind::MIR) {
    // Initialize targets for Machine IR processing
    InitializeAllTargets();
    InitializeAllTargetMCs();
    InitializeAllAsmParsers();
    InitializeAllAsmPrinters();

    if (Error Err = mir2vec::processModule(InputFile, OS, Cmd, FuncName, Lvl,
                                           *OptsCtx)) {
      handleAllErrors(std::move(Err), [&](const ErrorInfoBase &EIB) {
        WithColor::error(errs(), ToolName) << EIB.message() << "\n";
      });
      return 1;
    }

    return 0;
  }

  return 0;
}
