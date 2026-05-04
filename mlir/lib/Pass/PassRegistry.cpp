//===- PassRegistry.cpp - Pass Registration Utilities ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Pass/PassRegistry.h"

#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"

#include <deque>
#include <optional>
#include <utility>

using namespace mlir;
using namespace detail;

/// Static mapping of all of the registered passes.
static llvm::ManagedStatic<llvm::StringMap<PassInfo>> passRegistry;

/// A mapping of the above pass registry entries to the corresponding TypeID
/// of the pass that they generate.
static llvm::ManagedStatic<llvm::StringMap<TypeID>> passRegistryTypeIDs;

/// Static mapping of all of the registered pass pipelines.
static llvm::ManagedStatic<llvm::StringMap<PassPipelineInfo>>
    passPipelineRegistry;

/// Utility to create a default registry function from a pass instance.
static PassRegistryFunction
buildDefaultRegistryFn(const PassAllocatorFunction &allocator) {
  return [=](OpPassManager &pm, StringRef options,
             function_ref<LogicalResult(const Twine &)> errorHandler) {
    std::unique_ptr<Pass> pass = allocator();
    LogicalResult result = pass->initializeOptions(options, errorHandler);

    std::optional<StringRef> pmOpName = pm.getOpName();
    std::optional<StringRef> passOpName = pass->getOpName();
    if ((pm.getNesting() == OpPassManager::Nesting::Explicit) && pmOpName &&
        passOpName && *pmOpName != *passOpName) {
      return errorHandler(llvm::Twine("Can't add pass '") + pass->getName() +
                          "' restricted to '" + *pass->getOpName() +
                          "' on a PassManager intended to run on '" +
                          pm.getOpAnchorName() + "', did you intend to nest?");
    }
    pm.addPass(std::move(pass));
    return result;
  };
}

/// Utility to print the help string for a specific option.
static void printOptionHelp(StringRef arg, StringRef desc, size_t indent,
                            size_t descIndent, bool isTopLevel) {
  size_t numSpaces = descIndent - indent - 4;
  llvm::outs().indent(indent)
      << "--" << llvm::left_justify(arg, numSpaces) << "-   " << desc << '\n';
}

//===----------------------------------------------------------------------===//
// PassRegistry
//===----------------------------------------------------------------------===//

/// Prints the passes that were previously registered and stored in passRegistry
void mlir::printRegisteredPasses() {
  size_t maxWidth = 0;
  for (auto &entry : *passRegistry)
    maxWidth = std::max(maxWidth, entry.second.getOptionWidth() + 4);

  // Functor used to print the ordered entries of a registration map.
  auto printOrderedEntries = [&](StringRef header, auto &map) {
    llvm::SmallVector<PassRegistryEntry *, 32> orderedEntries;
    for (auto &kv : map)
      orderedEntries.push_back(&kv.second);
    llvm::array_pod_sort(
        orderedEntries.begin(), orderedEntries.end(),
        [](PassRegistryEntry *const *lhs, PassRegistryEntry *const *rhs) {
          return (*lhs)->getPassArgument().compare((*rhs)->getPassArgument());
        });

    llvm::outs().indent(0) << header << ":\n";
    for (PassRegistryEntry *entry : orderedEntries)
      entry->printHelpStr(/*indent=*/2, maxWidth);
  };

  // Print the available passes.
  printOrderedEntries("Passes", *passRegistry);
}

/// Print the help information for this pass. This includes the argument,
/// description, and any pass options. `descIndent` is the indent that the
/// descriptions should be aligned.
void PassRegistryEntry::printHelpStr(size_t indent, size_t descIndent) const {
  printOptionHelp(getPassArgument(), getPassDescription(), indent, descIndent,
                  /*isTopLevel=*/true);
  // If this entry has options, print the help for those as well.
  optHandler([=](const PassOptions &options) {
    options.printHelp(indent, descIndent);
  });
}

/// Return the maximum width required when printing the options of this
/// entry.
size_t PassRegistryEntry::getOptionWidth() const {
  size_t maxLen = 0;
  optHandler([&](const PassOptions &options) mutable {
    maxLen = options.getOptionWidth() + 2;
  });
  return maxLen;
}

//===----------------------------------------------------------------------===//
// PassPipelineInfo
//===----------------------------------------------------------------------===//

void mlir::registerPassPipeline(
    StringRef arg, StringRef description, const PassRegistryFunction &function,
    std::function<void(function_ref<void(const PassOptions &)>)> optHandler) {
  PassPipelineInfo pipelineInfo(arg, description, function,
                                std::move(optHandler));
  bool inserted = passPipelineRegistry->try_emplace(arg, pipelineInfo).second;
#ifndef NDEBUG
  if (!inserted)
    report_fatal_error("Pass pipeline " + arg + " registered multiple times");
#endif
  (void)inserted;
}

//===----------------------------------------------------------------------===//
// PassInfo
//===----------------------------------------------------------------------===//

PassInfo::PassInfo(StringRef arg, StringRef description,
                   const PassAllocatorFunction &allocator)
    : PassRegistryEntry(
          arg, description, buildDefaultRegistryFn(allocator),
          // Use a temporary pass to provide an options instance.
          [=](function_ref<void(const PassOptions &)> optHandler) {
            optHandler(allocator()->passOptions);
          }) {}

void mlir::registerPass(const PassAllocatorFunction &function) {
  std::unique_ptr<Pass> pass = function();
  StringRef arg = pass->getArgument();
  if (arg.empty())
    llvm::report_fatal_error(llvm::Twine("Trying to register '") +
                             pass->getName() +
                             "' pass that does not override `getArgument()`");
  StringRef description = pass->getDescription();
  PassInfo passInfo(arg, description, function);
  passRegistry->try_emplace(arg, passInfo);

  // Verify that the registered pass has the same ID as any registered to this
  // arg before it.
  TypeID entryTypeID = pass->getTypeID();
  auto it = passRegistryTypeIDs->try_emplace(arg, entryTypeID).first;
  if (it->second != entryTypeID)
    llvm::report_fatal_error(
        "pass allocator creates a different pass than previously "
        "registered for pass " +
        arg);
}

/// Returns the pass info for the specified pass argument or null if unknown.
const PassInfo *mlir::PassInfo::lookup(StringRef passArg) {
  auto it = passRegistry->find(passArg);
  return it == passRegistry->end() ? nullptr : &it->second;
}

/// Returns the pass pipeline info for the specified pass pipeline argument or
/// null if unknown.
const PassPipelineInfo *mlir::PassPipelineInfo::lookup(StringRef pipelineArg) {
  auto it = passPipelineRegistry->find(pipelineArg);
  return it == passPipelineRegistry->end() ? nullptr : &it->second;
}

//===----------------------------------------------------------------------===//
// PassOptions
//===----------------------------------------------------------------------===//

/// Attempt to find the next occurance of character 'c' in the string starting
/// from the `index`-th position , omitting any occurances that appear within
/// intervening ranges or literals.
static size_t findChar(StringRef str, size_t index, char c) {
  for (size_t i = index, e = str.size(); i < e; ++i) {
    if (str[i] == c)
      return i;
    // Check for various range characters.
    if (str[i] == '{')
      i = findChar(str, i + 1, '}');
    else if (str[i] == '(')
      i = findChar(str, i + 1, ')');
    else if (str[i] == '[')
      i = findChar(str, i + 1, ']');
    else if (str[i] == '\"')
      i = str.find_first_of('\"', i + 1);
    else if (str[i] == '\'')
      i = str.find_first_of('\'', i + 1);
    if (i == StringRef::npos)
      return StringRef::npos;
  }
  return StringRef::npos;
}

/// Extract an argument from 'options' and update it to point after the arg.
/// Returns the cleaned argument string.
static StringRef extractArgAndUpdateOptions(StringRef &options,
                                            size_t argSize) {
  StringRef str = options.take_front(argSize).trim();
  options = options.drop_front(argSize).ltrim();

  // Early exit if there's no escape sequence.
  if (str.size() <= 1)
    return str;

  const auto escapePairs = {std::make_pair('\'', '\''),
                            std::make_pair('"', '"')};
  for (const auto &escape : escapePairs) {
    if (str.front() == escape.first && str.back() == escape.second) {
      // Drop the escape characters and trim.
      // Don't process additional escape sequences.
      return str.drop_front().drop_back().trim();
    }
  }

  // Arguments may be wrapped in `{...}`. Unlike the quotation markers that
  // denote literals, we respect scoping here. The outer `{...}` should not
  // be stripped in cases such as "arg={...},{...}", which can be used to denote
  // lists of nested option structs.
  if (str.front() == '{') {
    unsigned match = findChar(str, 1, '}');
    if (match == str.size() - 1)
      str = str.drop_front().drop_back().trim();
  }

  return str;
}

LogicalResult detail::pass_options::parseCommaSeparatedList(
    StringRef argName, StringRef optionStr,
    function_ref<LogicalResult(StringRef)> elementParseFn) {
  if (optionStr.empty())
    return success();

  size_t nextElePos = findChar(optionStr, 0, ',');
  while (nextElePos != StringRef::npos) {
    // Process the portion before the comma.
    if (failed(
            elementParseFn(extractArgAndUpdateOptions(optionStr, nextElePos))))
      return failure();

    // Drop the leading ','
    optionStr = optionStr.drop_front();
    nextElePos = findChar(optionStr, 0, ',');
  }
  return elementParseFn(
      extractArgAndUpdateOptions(optionStr, optionStr.size()));
}

/// Out of line virtual function to provide home for the class.
void detail::PassOptions::OptionBase::anchor() {}

/// Copy the option values from 'other'.
void detail::PassOptions::copyOptionValuesFrom(const PassOptions &other) {
  assert(options.size() == other.options.size());
  if (options.empty())
    return;
  for (auto optionsIt : llvm::zip(options, other.options))
    std::get<0>(optionsIt)->copyValueFrom(*std::get<1>(optionsIt));
}

/// Parse in the next argument from the given options string. Returns a tuple
/// containing [the key of the option, the value of the option, updated
/// `options` string pointing after the parsed option].
static std::tuple<StringRef, StringRef, StringRef>
parseNextArg(StringRef options) {
  // Try to process the given punctuation, properly escaping any contained
  // characters.
  auto tryProcessPunct = [&](size_t &currentPos, char punct) {
    if (options[currentPos] != punct)
      return false;
    size_t nextIt = options.find_first_of(punct, currentPos + 1);
    if (nextIt != StringRef::npos)
      currentPos = nextIt;
    return true;
  };

  // Parse the argument name of the option.
  StringRef argName;
  for (size_t argEndIt = 0, optionsE = options.size();; ++argEndIt) {
    // Check for the end of the full option.
    if (argEndIt == optionsE || options[argEndIt] == ' ') {
      argName = extractArgAndUpdateOptions(options, argEndIt);
      return std::make_tuple(argName, StringRef(), options);
    }

    // Check for the end of the name and the start of the value.
    if (options[argEndIt] == '=') {
      argName = extractArgAndUpdateOptions(options, argEndIt);
      options = options.drop_front();
      break;
    }
  }

  // Parse the value of the option.
  for (size_t argEndIt = 0, optionsE = options.size();; ++argEndIt) {
    // Handle the end of the options string.
    if (argEndIt == optionsE || options[argEndIt] == ' ') {
      StringRef value = extractArgAndUpdateOptions(options, argEndIt);
      return std::make_tuple(argName, value, options);
    }

    // Skip over escaped sequences.
    char c = options[argEndIt];
    if (tryProcessPunct(argEndIt, '\'') || tryProcessPunct(argEndIt, '"'))
      continue;
    // '{...}' is used to specify options to passes, properly escape it so
    // that we don't accidentally split any nested options.
    if (c == '{') {
      size_t braceCount = 1;
      for (++argEndIt; argEndIt != optionsE; ++argEndIt) {
        // Allow nested punctuation.
        if (tryProcessPunct(argEndIt, '\'') || tryProcessPunct(argEndIt, '"'))
          continue;
        if (options[argEndIt] == '{')
          ++braceCount;
        else if (options[argEndIt] == '}' && --braceCount == 0)
          break;
      }
      // Account for the increment at the top of the loop.
      --argEndIt;
    }
  }
  llvm_unreachable("unexpected control flow in pass option parsing");
}

LogicalResult detail::PassOptions::parseFromString(StringRef options,
                                                   raw_ostream &errorStream) {
  // NOTE: `options` is modified in place to always refer to the unprocessed
  // part of the string.
  while (!options.empty()) {
    StringRef key, value;
    std::tie(key, value, options) = parseNextArg(options);
    if (key.empty())
      continue;

    auto it = optionsMap.find(key);
    if (it == optionsMap.end()) {
      errorStream << "<Pass-Options-Parser>: no such option " << key << "\n";
      return failure();
    }
    if (it->second->parseValue(value))
      return failure();
  }

  return success();
}

/// Print the options held by this struct in a form that can be parsed via
/// 'parseFromString'.
void detail::PassOptions::print(raw_ostream &os) const {
  // If there are no options, there is nothing left to do.
  if (optionsMap.empty())
    return;

  // Sort the options to make the ordering deterministic.
  SmallVector<OptionBase *, 4> orderedOps(options.begin(), options.end());
  auto compareOptionArgs = [](OptionBase *const *lhs, OptionBase *const *rhs) {
    return (*lhs)->getArgStr().compare((*rhs)->getArgStr());
  };
  llvm::array_pod_sort(orderedOps.begin(), orderedOps.end(), compareOptionArgs);

  // Interleave the options with ' '.
  os << '{';
  llvm::interleave(
      orderedOps, os, [&](OptionBase *option) { option->print(os); }, " ");
  os << '}';
}

/// Print the help string for the options held by this struct. `descIndent` is
/// the indent within the stream that the descriptions should be aligned.
void detail::PassOptions::printHelp(size_t indent, size_t descIndent) const {
  // Sort the options to make the ordering deterministic.
  SmallVector<OptionBase *, 4> orderedOps(options.begin(), options.end());
  auto compareOptionArgs = [](OptionBase *const *lhs, OptionBase *const *rhs) {
    return (*lhs)->getArgStr().compare((*rhs)->getArgStr());
  };
  llvm::array_pod_sort(orderedOps.begin(), orderedOps.end(), compareOptionArgs);
  for (OptionBase *option : orderedOps) {
    llvm::outs().indent(indent);
    option->printOptionInfo(descIndent - indent);
  }
}

/// Return the maximum width required when printing the help string.
size_t detail::PassOptions::getOptionWidth() const {
  size_t max = 0;
  for (auto *option : options)
    max = std::max(max, option->getOptionWidth());
  return max;
}

//===----------------------------------------------------------------------===//
// OptionTypeHelper<OpPassManager>
//===----------------------------------------------------------------------===//

bool detail::pass_options::OptionTypeHelper<OpPassManager>::parse(
    StringRef str, OpPassManager &result) {
  FailureOr<OpPassManager> pipeline = parsePassPipeline(str);
  if (failed(pipeline))
    return true;
  result = std::move(*pipeline);
  return false;
}

void detail::pass_options::OptionTypeHelper<OpPassManager>::print(
    raw_ostream &os, const OpPassManager &value) {
  value.printAsTextualPipeline(os);
}

//===----------------------------------------------------------------------===//
// TextualPassPipeline Parser
//===----------------------------------------------------------------------===//

namespace {
/// This class represents a textual description of a pass pipeline.
class TextualPipeline {
public:
  /// Try to initialize this pipeline with the given pipeline text.
  /// `errorStream` is the output stream to emit errors to.
  LogicalResult initialize(StringRef text, raw_ostream &errorStream);

  /// Add the internal pipeline elements to the provided pass manager.
  LogicalResult
  addToPipeline(OpPassManager &pm,
                function_ref<LogicalResult(const Twine &)> errorHandler) const;

private:
  /// A functor used to emit errors found during pipeline handling. The first
  /// parameter corresponds to the raw location within the pipeline string. This
  /// should always return failure.
  using ErrorHandlerT = function_ref<LogicalResult(const char *, Twine)>;

  /// A struct to capture parsed pass pipeline names.
  ///
  /// A pipeline is defined as a series of names, each of which may in itself
  /// recursively contain a nested pipeline. A name is either the name of a pass
  /// (e.g. "cse") or the name of an operation type (e.g. "buitin.module"). If
  /// the name is the name of a pass, the InnerPipeline is empty, since passes
  /// cannot contain inner pipelines.
  struct PipelineElement {
    PipelineElement(StringRef name) : name(name) {}

    StringRef name;
    StringRef options;
    const PassRegistryEntry *registryEntry = nullptr;
    std::vector<PipelineElement> innerPipeline;
  };

  /// Parse the given pipeline text into the internal pipeline vector. This
  /// function only parses the structure of the pipeline, and does not resolve
  /// its elements.
  LogicalResult parsePipelineText(StringRef text, ErrorHandlerT errorHandler);

  /// Resolve the elements of the pipeline, i.e. connect passes and pipelines to
  /// the corresponding registry entry.
  LogicalResult
  resolvePipelineElements(MutableArrayRef<PipelineElement> elements,
                          ErrorHandlerT errorHandler);

  /// Resolve a single element of the pipeline.
  LogicalResult resolvePipelineElement(PipelineElement &element,
                                       ErrorHandlerT errorHandler);

  /// Add the given pipeline elements to the provided pass manager.
  LogicalResult
  addToPipeline(ArrayRef<PipelineElement> elements, OpPassManager &pm,
                function_ref<LogicalResult(const Twine &)> errorHandler) const;

  std::vector<PipelineElement> pipeline;
};

} // namespace

/// Try to initialize this pipeline with the given pipeline text. An option is
/// given to enable accurate error reporting.
LogicalResult TextualPipeline::initialize(StringRef text,
                                          raw_ostream &errorStream) {
  if (text.empty())
    return success();

  // Build a source manager to use for error reporting.
  llvm::SourceMgr pipelineMgr;
  pipelineMgr.AddNewSourceBuffer(
      llvm::MemoryBuffer::getMemBuffer(text, "MLIR Textual PassPipeline Parser",
                                       /*RequiresNullTerminator=*/false),
      SMLoc());
  auto errorHandler = [&](const char *rawLoc, Twine msg) {
    pipelineMgr.PrintMessage(errorStream, SMLoc::getFromPointer(rawLoc),
                             llvm::SourceMgr::DK_Error, msg);
    return failure();
  };

  // Parse the provided pipeline string.
  if (failed(parsePipelineText(text, errorHandler)))
    return failure();
  return resolvePipelineElements(pipeline, errorHandler);
}

/// Add the internal pipeline elements to the provided pass manager.
LogicalResult TextualPipeline::addToPipeline(
    OpPassManager &pm,
    function_ref<LogicalResult(const Twine &)> errorHandler) const {
  // Temporarily disable implicit nesting while we append to the pipeline. We
  // want the created pipeline to exactly match the parsed text pipeline, so
  // it's preferrable to just error out if implicit nesting would be required.
  OpPassManager::Nesting nesting = pm.getNesting();
  pm.setNesting(OpPassManager::Nesting::Explicit);
  llvm::scope_exit restore([&]() { pm.setNesting(nesting); });

  return addToPipeline(pipeline, pm, errorHandler);
}

/// Parse the given pipeline text into the internal pipeline vector. This
/// function only parses the structure of the pipeline, and does not resolve
/// its elements.
LogicalResult TextualPipeline::parsePipelineText(StringRef text,
                                                 ErrorHandlerT errorHandler) {
  SmallVector<std::vector<PipelineElement> *, 4> pipelineStack = {&pipeline};
  for (;;) {
    std::vector<PipelineElement> &pipeline = *pipelineStack.back();
    size_t pos = text.find_first_of(",(){");
    pipeline.emplace_back(/*name=*/text.substr(0, pos).trim());

    // If we have a single terminating name, we're done.
    if (pos == StringRef::npos)
      break;

    text = text.substr(pos);
    char sep = text[0];

    // Handle pulling ... from 'pass{...}' out as PipelineElement.options.
    if (sep == '{') {
      text = text.substr(1);

      // Skip over everything until the closing '}' and store as options.
      size_t close = StringRef::npos;
      for (unsigned i = 0, e = text.size(), braceCount = 1; i < e; ++i) {
        if (text[i] == '{') {
          ++braceCount;
          continue;
        }
        if (text[i] == '}' && --braceCount == 0) {
          close = i;
          break;
        }
      }

      // Check to see if a closing options brace was found.
      if (close == StringRef::npos) {
        return errorHandler(
            /*rawLoc=*/text.data() - 1,
            "missing closing '}' while processing pass options");
      }
      pipeline.back().options = text.substr(0, close);
      text = text.substr(close + 1);

      // Consume space characters that an user might add for readability.
      text = text.ltrim();

      // Skip checking for '(' because nested pipelines cannot have options.
    } else if (sep == '(') {
      text = text.substr(1);

      // Push the inner pipeline onto the stack to continue processing.
      pipelineStack.push_back(&pipeline.back().innerPipeline);
      continue;
    }

    // When handling the close parenthesis, we greedily consume them to avoid
    // empty strings in the pipeline.
    while (text.consume_front(")")) {
      // If we try to pop the outer pipeline we have unbalanced parentheses.
      if (pipelineStack.size() == 1)
        return errorHandler(/*rawLoc=*/text.data() - 1,
                            "encountered extra closing ')' creating unbalanced "
                            "parentheses while parsing pipeline");

      pipelineStack.pop_back();
      // Consume space characters that an user might add for readability.
      text = text.ltrim();
    }

    // Check if we've finished parsing.
    if (text.empty())
      break;

    // Otherwise, the end of an inner pipeline always has to be followed by
    // a comma, and then we can continue.
    if (!text.consume_front(","))
      return errorHandler(text.data(), "expected ',' after parsing pipeline");
  }

  // Check for unbalanced parentheses.
  if (pipelineStack.size() > 1)
    return errorHandler(
        text.data(),
        "encountered unbalanced parentheses while parsing pipeline");

  assert(pipelineStack.back() == &pipeline &&
         "wrong pipeline at the bottom of the stack");
  return success();
}

/// Resolve the elements of the pipeline, i.e. connect passes and pipelines to
/// the corresponding registry entry.
LogicalResult TextualPipeline::resolvePipelineElements(
    MutableArrayRef<PipelineElement> elements, ErrorHandlerT errorHandler) {
  for (auto &elt : elements)
    if (failed(resolvePipelineElement(elt, errorHandler)))
      return failure();
  return success();
}

/// Resolve a single element of the pipeline.
LogicalResult
TextualPipeline::resolvePipelineElement(PipelineElement &element,
                                        ErrorHandlerT errorHandler) {
  // If the inner pipeline of this element is not empty, this is an operation
  // pipeline.
  if (!element.innerPipeline.empty())
    return resolvePipelineElements(element.innerPipeline, errorHandler);

  // Otherwise, this must be a pass or pass pipeline.
  // Check to see if a pipeline was registered with this name.
  if ((element.registryEntry = PassPipelineInfo::lookup(element.name)))
    return success();

  // If not, then this must be a specific pass name.
  if ((element.registryEntry = PassInfo::lookup(element.name)))
    return success();

  // Emit an error for the unknown pass.
  auto *rawLoc = element.name.data();
  return errorHandler(rawLoc, "'" + element.name +
                                  "' does not refer to a "
                                  "registered pass or pass pipeline");
}

/// Add the given pipeline elements to the provided pass manager.
LogicalResult TextualPipeline::addToPipeline(
    ArrayRef<PipelineElement> elements, OpPassManager &pm,
    function_ref<LogicalResult(const Twine &)> errorHandler) const {
  for (auto &elt : elements) {
    if (elt.registryEntry) {
      if (failed(elt.registryEntry->addToPipeline(pm, elt.options,
                                                  errorHandler))) {
        return errorHandler("failed to add `" + elt.name + "` with options `" +
                            elt.options + "`");
      }
    } else if (failed(addToPipeline(elt.innerPipeline, pm.nest(elt.name),
                                    errorHandler))) {
      return errorHandler("failed to add `" + elt.name + "` with options `" +
                          elt.options + "` to inner pipeline");
    }
  }
  return success();
}

LogicalResult mlir::parsePassPipeline(StringRef pipeline, OpPassManager &pm,
                                      raw_ostream &errorStream) {
  TextualPipeline pipelineParser;
  if (failed(pipelineParser.initialize(pipeline, errorStream)))
    return failure();
  auto errorHandler = [&](Twine msg) {
    errorStream << msg << "\n";
    return failure();
  };
  if (failed(pipelineParser.addToPipeline(pm, errorHandler)))
    return failure();
  return success();
}

FailureOr<OpPassManager> mlir::parsePassPipeline(StringRef pipeline,
                                                 raw_ostream &errorStream) {
  pipeline = pipeline.trim();
  // Pipelines are expected to be of the form `<op-name>(<pipeline>)`.
  size_t pipelineStart = pipeline.find_first_of('(');
  if (pipelineStart == 0 || pipelineStart == StringRef::npos ||
      !pipeline.consume_back(")")) {
    errorStream << "expected pass pipeline to be wrapped with the anchor "
                   "operation type, e.g. 'builtin.module(...)'";
    return failure();
  }

  StringRef opName = pipeline.take_front(pipelineStart).rtrim();
  OpPassManager pm(opName);
  if (failed(parsePassPipeline(pipeline.drop_front(1 + pipelineStart), pm,
                               errorStream)))
    return failure();
  return pm;
}

//===----------------------------------------------------------------------===//
// PassNameParser
//===----------------------------------------------------------------------===//

namespace {
/// This struct represents the possible data entries in a parsed pass pipeline
/// list.
struct PassArgData {
  PassArgData() = default;
  PassArgData(const PassRegistryEntry *registryEntry, std::string options)
      : registryEntry(registryEntry), options(std::move(options)) {}

  const PassRegistryEntry *registryEntry{nullptr};
  std::string options;
};
} // namespace

/// The name for the command line option used for parsing the textual pass
/// pipeline.
#define PASS_PIPELINE_ARG "pass-pipeline"

//===----------------------------------------------------------------------===//
// PassPipelineCLParser
//===----------------------------------------------------------------------===//

namespace mlir {
namespace detail {
struct PassPipelineCLParserImpl {
  /// Per-pass parse state.  Pass names only exist at runtime, so each
  /// descriptor is built here; a deque keeps addresses stable because the
  /// parser holds a pointer to each RuntimeOption.
  struct PassOptState {
    PassPipelineCLParserImpl *Impl = nullptr;
    const PassRegistryEntry *Entry = nullptr;
    std::optional<llvm::clv2::RuntimeOption<std::string>> Opt;
  };

  /// Ctx is a PassOptState: record this pass plus any inline options.
  static bool addPassFromEntry(void *Ctx, const std::string &Val) {
    auto *St = static_cast<PassOptState *>(Ctx);
    St->Impl->passList.push_back(PassArgData(St->Entry, Val));
    return true;
  }

  /// Ctx is the parser: look the pass name up, failing the parse if unknown.
  static bool addPassByName(void *Ctx, const std::string &Val) {
    auto *Impl = static_cast<PassPipelineCLParserImpl *>(Ctx);
    // passRegistry is a file-scope ManagedStatic, not a member.
    auto It = passRegistry->find(Val);
    if (It == passRegistry->end())
      return false;
    Impl->passList.push_back(PassArgData(&It->second, {}));
    return true;
  }

  PassPipelineCLParserImpl(StringRef arg, StringRef description,
                           bool passNamesOnly) {
    if (passNamesOnly) {
      NamesOnlyOpt.emplace(
          arg, description, llvm::clv2::ValueRequired,
          llvm::clv2::CommaSeparated, llvm::clv2::value_desc("pass-arg"),
          llvm::clv2::CtxCallback<std::string>{&addPassByName, this});
      PendingEntries.push_back(NamesOnlyOpt->makeEntry());
    } else {
      auto registerEntry = [&](const PassRegistryEntry &entry) {
        // RuntimeOption is immovable, so emplace then fill.
        PassOptStates.emplace_back();
        PassOptState &St = PassOptStates.back();
        St.Impl = this;
        St.Entry = &entry;
        St.Opt.emplace(
            entry.getPassArgument(), entry.getPassDescription(),
            llvm::clv2::ValueOptional,
            llvm::clv2::CtxCallback<std::string>{&addPassFromEntry, &St});
        PendingEntries.push_back(St.Opt->makeEntry());
      };

      for (const auto &kv : *passRegistry)
        registerEntry(kv.second);
      for (const auto &kv : *passPipelineRegistry)
        registerEntry(kv.second);
    }
  }

  std::deque<PassOptState> PassOptStates;
  std::optional<llvm::clv2::RuntimeOption<std::string>> NamesOnlyOpt;

  void registerWith(llvm::clv2::OptionParser &P) {
    for (auto &E : PendingEntries)
      P.addDynamicEntry(std::move(E));
    PendingEntries.clear();
  }

  bool contains(const PassRegistryEntry *entry) const {
    return llvm::any_of(passList, [&](const PassArgData &data) {
      return data.registryEntry == entry;
    });
  }

  std::vector<PassArgData> passList;
  std::vector<llvm::clv2::detail::OptionEntry> PendingEntries;
  /// Shared destination + occurrence counter for --pass-pipeline and its
  /// alias; the owning parser mirrors these into its public members.
  std::string PipelineValue;
  unsigned PipelineCount = 0;
  /// The alias' name is a constructor argument, so its descriptor is built at
  /// runtime.  Held here (behind the impl's unique_ptr) so it stays put.
  std::optional<llvm::clv2::RuntimeOption<std::string>> AliasOpt;
};
} // namespace detail
} // namespace mlir

static constexpr llvm::clv2::OptionInfo<std::string> OI_PassPipeline{
    PASS_PIPELINE_ARG, "Textual description of the pass pipeline to run"};

/// Construct a pass pipeline parser with the given command line description.
PassPipelineCLParser::PassPipelineCLParser(StringRef arg, StringRef description)
    : impl(std::make_unique<detail::PassPipelineCLParserImpl>(
          arg, description, /*passNamesOnly=*/false)) {
  impl->PendingEntries.push_back(llvm::clv2::makeEntry<&OI_PassPipeline>(
      impl->PipelineValue, impl->PipelineCount));
}

PassPipelineCLParser::PassPipelineCLParser(StringRef arg, StringRef description,
                                           StringRef alias)
    : PassPipelineCLParser(arg, description) {
  // Shares the slot and occurrence counter with --pass-pipeline, so either
  // spelling feeds the same value.
  impl->AliasOpt.emplace(alias, "Alias for --" PASS_PIPELINE_ARG,
                         llvm::clv2::ValueRequired);
  // help-only, no descriptor spelling, so it is set on the option's own
  // static info rather than passed to the OptionInfo constructor.
  impl->AliasOpt->staticInfo().SuppressValuePlaceholder = true;
  llvm::clv2::detail::OptionEntry E =
      impl->AliasOpt->makeEntry(impl->PipelineCount);
  E.ParseSlot = &impl->PipelineValue;
  impl->PendingEntries.push_back(std::move(E));
}

void PassPipelineCLParser::registerWith(llvm::clv2::OptionParser &P) {
  impl->registerWith(P);
}

PassPipelineCLParser::~PassPipelineCLParser() = default;

/// Returns true if this parser contains any valid options to add.
bool PassPipelineCLParser::hasAnyOccurrences() const {
  return impl->PipelineCount != 0 || !impl->passList.empty();
}

/// Returns true if the given pass registry entry was registered at the
/// top-level of the parser, i.e. not within an explicit textual pipeline.
bool PassPipelineCLParser::contains(const PassRegistryEntry *entry) const {
  return impl->contains(entry);
}

/// Adds the passes defined by this parser entry to the given pass manager.
LogicalResult PassPipelineCLParser::addToPipeline(
    OpPassManager &pm,
    function_ref<LogicalResult(const Twine &)> errorHandler) const {
  if (impl->PipelineCount) {
    if (!impl->passList.empty())
      return errorHandler(
          "'-" PASS_PIPELINE_ARG
          "' option can't be used with individual pass options");
    std::string errMsg;
    llvm::raw_string_ostream os(errMsg);
    FailureOr<OpPassManager> parsed =
        parsePassPipeline(impl->PipelineValue, os);
    if (failed(parsed))
      return errorHandler(errMsg);
    pm = std::move(*parsed);
    return success();
  }

  for (auto &passIt : impl->passList) {
    if (failed(passIt.registryEntry->addToPipeline(pm, passIt.options,
                                                   errorHandler)))
      return errorHandler("failed to add `" +
                          passIt.registryEntry->getPassArgument() +
                          "` with options `" + passIt.options + "`");
  }
  return success();
}

//===----------------------------------------------------------------------===//
// PassNameCLParser
//===----------------------------------------------------------------------===//

/// Construct a pass pipeline parser with the given command line description.
PassNameCLParser::PassNameCLParser(StringRef arg, StringRef description)
    : impl(std::make_unique<detail::PassPipelineCLParserImpl>(
          arg, description, /*passNamesOnly=*/true)) {}
PassNameCLParser::~PassNameCLParser() = default;

void PassNameCLParser::registerWith(llvm::clv2::OptionParser &P) {
  impl->registerWith(P);
}

/// Returns true if this parser contains any valid options to add.
bool PassNameCLParser::hasAnyOccurrences() const {
  return !impl->passList.empty();
}

/// Returns true if the given pass registry entry was registered at the
/// top-level of the parser, i.e. not within an explicit textual pipeline.
bool PassNameCLParser::contains(const PassRegistryEntry *entry) const {
  return impl->contains(entry);
}
