//===- PassOptions.h - Pass Option Utilities --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains utilities for registering options with compiler passes and
// pipelines.
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_PASS_PASSOPTIONS_H_
#define MLIR_PASS_PASSOPTIONS_H_

#include "mlir/Support/LLVM.h"
#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace mlir {
class OpPassManager;

namespace detail {
namespace pass_options {

/// Trait used to detect if a type has a operator<< method.
template <typename T>
using has_stream_operator_trait =
    decltype(std::declval<raw_ostream &>() << std::declval<T>());
template <typename T>
using has_stream_operator = llvm::is_detected<has_stream_operator_trait, T>;

/// Trait used to detect if a type has operator==.
template <typename T>
using has_equality_trait =
    decltype(std::declval<const T &>() == std::declval<const T &>());
template <typename T>
using has_equality = llvm::is_detected<has_equality_trait, T>;

//===----------------------------------------------------------------------===//
// Modifier detection traits
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Enum entry for storing name->value mappings
//===----------------------------------------------------------------------===//

/// A single enum entry mapping a string name to an integer value plus
/// description (for help text).
struct EnumEntry {
  StringRef name;
  int value;
  StringRef description;
};

/// A container for enum value entries, used as a modifier in Option/ListOption
/// constructors to register enum name-to-value mappings.
struct ValuesClass {
  SmallVector<EnumEntry> entries;

  template <typename... Args>
  ValuesClass(EnumEntry first, Args &&...rest) {
    entries.push_back(first);
    addEntries(std::forward<Args>(rest)...);
  }

private:
  void addEntries() {}
  template <typename... Args>
  void addEntries(EnumEntry entry, Args &&...rest) {
    entries.push_back(entry);
    addEntries(std::forward<Args>(rest)...);
  }
};

/// Wrapper for pass option description strings, used by generated code.
struct Desc {
  StringRef value;
  explicit Desc(StringRef v) : value(v) {}
};

/// Wrapper for pass option initial values, used by generated code.
template <typename T>
struct Init {
  T value;
  explicit Init(T v) : value(std::move(v)) {}
};
template <typename T>
Init(T) -> Init<T>;

template <typename T>
struct is_known_modifier : std::false_type {};
template <>
struct is_known_modifier<Desc> : std::true_type {};
template <typename T>
struct is_known_modifier<Init<T>> : std::true_type {};
template <>
struct is_known_modifier<ValuesClass> : std::true_type {};

//===----------------------------------------------------------------------===//
// OptionTypeHelper - trait for type-specific parse/print
//===----------------------------------------------------------------------===//

/// A trait class that can be specialized to provide custom parsing and printing
/// for types used with pass options. The primary template is empty; types that
/// need custom handling should specialize it with:
///   static bool parse(StringRef str, T &result);
///   static void print(raw_ostream &os, const T &value);
template <typename T, typename = void>
struct OptionTypeHelper {
  static constexpr bool hasCustomHandler = false;
};

//===----------------------------------------------------------------------===//
// Parsing helpers
//===----------------------------------------------------------------------===//

/// Parse a boolean value from a string.
/// Accepts: "true", "false", "1", "0", or empty (treated as true).
inline bool parseBool(StringRef str, bool &result) {
  if (str.empty() || str.equals_insensitive("true") || str == "1") {
    result = true;
    return false; // success
  }
  if (str.equals_insensitive("false") || str == "0") {
    result = false;
    return false; // success
  }
  return true; // failure
}

/// Parse an integer from a string.
template <typename T>
bool parseInteger(StringRef str, T &result) {
  return str.getAsInteger(0, result);
}

/// Parse a floating-point value from a string.
inline bool parseFloat(StringRef str, float &result) {
  double d;
  if (str.getAsDouble(d))
    return true;
  result = static_cast<float>(d);
  return false;
}
inline bool parseDouble(StringRef str, double &result) {
  return str.getAsDouble(result);
}

/// Parse a string value (trivial).
inline bool parseString(StringRef str, std::string &result) {
  result = str.str();
  return false; // always succeeds
}

/// Parse an enum from a string using the provided entries.
template <typename T>
bool parseEnum(StringRef str, T &result, ArrayRef<EnumEntry> entries,
               raw_ostream &errorStream, StringRef optionName = "") {
  if constexpr (std::is_enum_v<T> || std::is_integral_v<T>) {
    for (const auto &entry : entries) {
      if (entry.name == str) {
        result = static_cast<T>(entry.value);
        return false; // success
      }
    }
    errorStream << "for the --" << optionName
                << " option: Cannot find option named '" << str << "'!\n";
    return true; // failure
  } else {
    errorStream << "enum parsing not supported for this type\n";
    return true;
  }
}

/// Print a boolean value.
inline void printBool(raw_ostream &os, bool value) {
  os << (value ? StringRef("true") : StringRef("false"));
}

/// Print a string value, escaping if necessary.
inline void printString(raw_ostream &os, const std::string &str) {
  const size_t spaceIndex = str.find_first_of(' ');
  const size_t escapeIndex =
      std::min({str.find_first_of('{'), str.find_first_of('\''),
                str.find_first_of('"')});
  const bool requiresEscape = spaceIndex < escapeIndex;
  if (requiresEscape)
    os << "{";
  os << str;
  if (requiresEscape)
    os << "}";
}

/// Print a generic value using operator<<.
template <typename T>
void printGenericValue(raw_ostream &os, const T &value) {
  os << value;
}

/// Find an enum entry name for a given value.
template <typename T>
std::optional<StringRef> findEnumName(const T &value,
                                      ArrayRef<EnumEntry> entries) {
  if constexpr (std::is_enum_v<T> || std::is_integral_v<T>) {
    for (const auto &entry : entries) {
      if (static_cast<int>(value) == entry.value)
        return entry.name;
    }
  }
  return std::nullopt;
}

//===----------------------------------------------------------------------===//
// Comma-separated list parsing
//===----------------------------------------------------------------------===//

/// Parse a string containing a list of comma-delimited elements, invoking the
/// given parser for each sub-element. This handles nested delimiters like
/// {}, (), [], and quoted strings.
LogicalResult
parseCommaSeparatedList(StringRef argName, StringRef optionStr,
                        function_ref<LogicalResult(StringRef)> elementParseFn);

} // namespace pass_options
} // namespace detail
} // namespace mlir

/// Create an enum value entry for use with pass options.
template <typename T>
mlir::detail::pass_options::EnumEntry clEnumValN(T value, llvm::StringRef name,
                                                 llvm::StringRef desc) {
  return {name, static_cast<int>(value), desc};
}

namespace llvm::cl {
/// Create a ValuesClass from enum entries, for use in pass option declarations.
template <typename... Args>
mlir::detail::pass_options::ValuesClass values(Args &&...args) {
  return mlir::detail::pass_options::ValuesClass(std::forward<Args>(args)...);
}

/// Spelling used in PassOptions declarations.
inline mlir::detail::pass_options::Desc desc(llvm::StringRef d) {
  return mlir::detail::pass_options::Desc(d);
}

/// Spelling used in PassOptions declarations.
template <typename T>
mlir::detail::pass_options::Init<T> init(T v) {
  return mlir::detail::pass_options::Init<T>(std::move(v));
}
} // namespace llvm::cl

namespace mlir {
namespace detail {

//===----------------------------------------------------------------------===//
// PassOptions
//===----------------------------------------------------------------------===//

/// Base container class and manager for all pass options.
class PassOptions {
public:
  /// This is the type-erased option base class.
  class OptionBase {
  public:
    virtual ~OptionBase() = default;

    /// Out of line virtual function to provide home for the class.
    virtual void anchor();

    /// Print the name and value of this option to the given stream.
    virtual void print(raw_ostream &os) = 0;

    /// Parse a value from a string. Returns true on failure.
    virtual bool parseValue(StringRef value) = 0;

    /// Return the argument string of this option.
    StringRef getArgStr() const { return argStr; }

    /// Returns true if this option has any value assigned to it.
    bool hasValue() const { return optHasValue; }

    /// Print help information for this option.
    virtual void printOptionInfo(size_t globalWidth) const = 0;

    /// Return the width required for printing help.
    virtual size_t getOptionWidth() const = 0;

  protected:
    /// Copy the value from the given option into this one.
    virtual void copyValueFrom(const OptionBase &other) = 0;

    /// The argument name of this option.
    StringRef argStr;

    /// Description for help text.
    StringRef description;

    /// Flag indicating if this option has a value.
    bool optHasValue = false;

    /// Allow access to private methods.
    friend PassOptions;
  };

  //===--------------------------------------------------------------------===//
  // OptionParser - template alias kept for source compatibility with Pass.h
  //===--------------------------------------------------------------------===//

  /// The OptionParser is no longer used for dispatch but is kept as a
  /// compatibility alias. The second template parameter of Option/ListOption
  /// is now ignored.
  template <typename DataType>
  using OptionParser = void;

  //===--------------------------------------------------------------------===//
  // Option<T>
  //===--------------------------------------------------------------------===//

  /// This class represents a specific pass option, with a provided data type.
  /// The optional OptionParser template parameter is accepted but ignored,
  /// for backwards compatibility.
  template <typename DataType, typename = void>
  class Option : public OptionBase {
  public:
    template <typename... Args>
    Option(PassOptions &parent, StringRef arg, Args &&...args)
        : value{}, defaultValue{} {
      argStr = arg;
      applyModifiers(std::forward<Args>(args)...);
      defaultValue = value;
      parent.addOption(this);
    }

    ~Option() override = default;

    /// Get the value.
    const DataType &getValue() const { return value; }
    DataType &getValue() { return value; }

    /// Dereference to get the value (like a smart pointer).
    const DataType &operator*() const { return value; }
    DataType &operator*() { return value; }

    /// Implicit conversion to the data type.
    operator const DataType &() const { return value; }

    /// Assignment operator.
    Option &operator=(const DataType &v) {
      value = v;
      optHasValue = true;
      return *this;
    }

    /// Copy assignment from another Option.
    Option &operator=(const Option &other) {
      value = other.value;
      optHasValue = other.optHasValue;
      return *this;
    }

    /// Arrow operator for struct/class types.
    const DataType *operator->() const { return &value; }
    DataType *operator->() { return &value; }

    /// Explicit setter.
    void setValue(const DataType &v) {
      value = v;
      optHasValue = true;
    }

    /// Returns 1 if a value has been explicitly set, 0 otherwise.
    unsigned getNumOccurrences() const { return optHasValue ? 1 : 0; }

    /// Forward common methods from the underlying type for convenience.
    /// These allow calling e.g. option.empty() on Option<std::string>.
    template <typename T = DataType>
    auto empty() const -> decltype(std::declval<const T &>().empty()) {
      return value.empty();
    }

    template <typename T = DataType>
    auto size() const -> decltype(std::declval<const T &>().size()) {
      return value.size();
    }

  private:
    /// Parse a value from a string. Returns true on error.
    bool parseValue(StringRef valueStr) override {
      if (parseValueImpl(valueStr))
        return true;
      optHasValue = true;
      return false;
    }

    /// Print the name and value of this option to the given stream.
    void print(raw_ostream &os) override {
      os << argStr << '=';
      printValueImpl(os);
    }

    /// Copy the value from the given option into this one.
    void copyValueFrom(const OptionBase &other) override {
      const auto &otherOpt = static_cast<const Option &>(other);
      value = otherOpt.value;
      optHasValue = other.optHasValue;
    }

    /// Print help information.
    void printOptionInfo(size_t globalWidth) const override {
      printOptionInfoImpl(globalWidth);
    }

    /// Return the width required for printing help.
    size_t getOptionWidth() const override { return getOptionWidthImpl(); }

    //===------------------------------------------------------------------===//
    // Type-specific parsing
    //===------------------------------------------------------------------===//

    /// Parse for bool.
    template <typename T = DataType>
    std::enable_if_t<std::is_same_v<T, bool>, bool>
    parseValueImpl(StringRef valueStr) {
      return pass_options::parseBool(valueStr, value);
    }

    /// Parse for integral types (int, unsigned, int64_t, etc.) but NOT bool.
    template <typename T = DataType>
    std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, bool>
    parseValueImpl(StringRef valueStr) {
      if (pass_options::parseInteger(valueStr, value)) {
        llvm::errs() << "'" << valueStr
                     << "' value invalid for integer argument\n";
        return true;
      }
      return false;
    }

    /// Parse for floating-point types.
    template <typename T = DataType>
    std::enable_if_t<std::is_floating_point_v<T>, bool>
    parseValueImpl(StringRef valueStr) {
      if constexpr (std::is_same_v<T, double>)
        return pass_options::parseDouble(valueStr, value);
      else
        return pass_options::parseFloat(valueStr, value);
    }

    /// Parse for std::string.
    template <typename T = DataType>
    std::enable_if_t<std::is_same_v<T, std::string>, bool>
    parseValueImpl(StringRef valueStr) {
      return pass_options::parseString(valueStr, value);
    }

    /// Parse for enum types (types that have enum entries registered).
    template <typename T = DataType>
    std::enable_if_t<
        !std::is_integral_v<T> && !std::is_same_v<T, std::string> &&
            !std::is_base_of_v<PassOptions, T> && std::is_enum_v<T>,
        bool>
    parseValueImpl(StringRef valueStr) {
      return pass_options::parseEnum(valueStr, value, enumEntries, llvm::errs(),
                                     argStr);
    }

    /// Parse for nested PassOptions types.
    template <typename T = DataType>
    std::enable_if_t<std::is_base_of_v<PassOptions, T>, bool>
    parseValueImpl(StringRef valueStr) {
      return failed(value.parseFromString(valueStr));
    }

    /// Fallback parse for other types - check OptionTypeHelper, then enum
    /// entries.
    template <typename T = DataType>
    std::enable_if_t<!std::is_integral_v<T> && !std::is_floating_point_v<T> &&
                         !std::is_same_v<T, std::string> &&
                         !std::is_base_of_v<PassOptions, T> &&
                         !std::is_enum_v<T>,
                     bool>
    parseValueImpl(StringRef valueStr) {
      if constexpr (pass_options::OptionTypeHelper<T>::hasCustomHandler)
        return pass_options::OptionTypeHelper<T>::parse(valueStr, value);
      if (!enumEntries.empty())
        return pass_options::parseEnum(valueStr, value, enumEntries,
                                       llvm::errs(), argStr);
      llvm::errs() << "unsupported option type for parsing\n";
      return true;
    }

    //===------------------------------------------------------------------===//
    // Type-specific printing
    //===------------------------------------------------------------------===//

    /// Print for bool.
    template <typename T = DataType>
    std::enable_if_t<std::is_same_v<T, bool>> printValueImpl(raw_ostream &os) {
      pass_options::printBool(os, value);
    }

    /// Print for std::string.
    template <typename T = DataType>
    std::enable_if_t<std::is_same_v<T, std::string>>
    printValueImpl(raw_ostream &os) {
      pass_options::printString(os, value);
    }

    /// Print for PassOptions (nested).
    template <typename T = DataType>
    std::enable_if_t<std::is_base_of_v<PassOptions, T>>
    printValueImpl(raw_ostream &os) {
      value.print(os);
    }

    /// Print for all other types (non-bool, non-string, non-PassOptions).
    template <typename T = DataType>
    std::enable_if_t<!std::is_same_v<T, bool> &&
                     !std::is_same_v<T, std::string> &&
                     !std::is_base_of_v<PassOptions, T>>
    printValueImpl(raw_ostream &os) {
      // Check for OptionTypeHelper first.
      if constexpr (pass_options::OptionTypeHelper<T>::hasCustomHandler) {
        pass_options::OptionTypeHelper<T>::print(os, value);
        return;
      }
      // Check for enum entries.
      if (!enumEntries.empty()) {
        if (auto name = pass_options::findEnumName(value, enumEntries))
          os << *name;
        else
          llvm_unreachable("unknown enum value for option");
        return;
      }
      // Fall back to integer representation for enum types.
      if constexpr (std::is_enum_v<T>)
        os << static_cast<int>(value);
      else if constexpr (std::is_integral_v<T>)
        os << value;
      else if constexpr (pass_options::has_stream_operator<T>::value)
        os << value;
      else
        llvm_unreachable("cannot print option value");
    }

    //===------------------------------------------------------------------===//
    // Help text
    //===------------------------------------------------------------------===//

    void printOptionInfoImpl(size_t globalWidth) const {
      size_t optWidth = getOptionWidthImpl();
      llvm::outs() << "  --" << argStr;
      if (!enumEntries.empty()) {
        llvm::outs() << "=<value>";
        llvm::outs().indent(globalWidth > optWidth ? globalWidth - optWidth
                                                   : 0);
        llvm::outs() << "-   " << description << '\n';
        for (const auto &entry : enumEntries) {
          llvm::outs() << "    =" << entry.name;
          size_t indent = globalWidth > entry.name.size() + 5
                              ? globalWidth - entry.name.size() - 5
                              : 0;
          llvm::outs().indent(indent);
          llvm::outs() << "-   " << entry.description << '\n';
        }
      } else {
        llvm::outs().indent(globalWidth > optWidth ? globalWidth - optWidth
                                                   : 0);
        llvm::outs() << "-   " << description << '\n';
      }
    }

    size_t getOptionWidthImpl() const {
      // "--" + argStr
      size_t width = argStr.size() + 2;
      if (!enumEntries.empty()) {
        // "=<value>"
        width += 8;
        for (const auto &entry : enumEntries)
          width = std::max(width, entry.name.size() + 5); // "    ="
      }
      return width;
    }

    //===------------------------------------------------------------------===//
    // Modifier application
    //===------------------------------------------------------------------===//

    void applyModifiers() {} // base case

    template <typename First, typename... Rest>
    void applyModifiers(First &&first, Rest &&...rest) {
      applyModifier(std::forward<First>(first));
      applyModifiers(std::forward<Rest>(rest)...);
    }

    void applyModifier(pass_options::Desc d) { description = d.value; }

    template <typename T>
    void applyModifier(pass_options::Init<T> init) {
      value = static_cast<DataType>(std::move(init.value));
    }

    void applyModifier(pass_options::ValuesClass vc) {
      enumEntries = std::move(vc.entries);
    }

    /// Ignore any other modifiers.
    template <typename T>
    std::enable_if_t<!pass_options::is_known_modifier<std::decay_t<T>>::value>
    applyModifier(T &&) {}

    DataType value;
    DataType defaultValue;
    SmallVector<pass_options::EnumEntry> enumEntries;
  };

  //===--------------------------------------------------------------------===//
  // ListOption<T>
  //===--------------------------------------------------------------------===//

  /// This class represents a specific pass option that contains a list of
  /// values of the provided data type. The elements within the textual form of
  /// this option are parsed assuming they are comma-separated.
  template <typename DataType, typename = void>
  class ListOption : public OptionBase {
  public:
    using StorageType = std::vector<DataType>;

    template <typename... Args>
    ListOption(PassOptions &parent, StringRef arg, Args &&...args) {
      argStr = arg;
      applyModifiers(std::forward<Args>(args)...);
      defaultValues = values;
      defaultAssigned = true;
      parent.addOption(this);
    }
    ~ListOption() override = default;

    /// Copy assignment.
    ListOption &operator=(const ListOption &other) {
      values = other.values;
      optHasValue = other.optHasValue;
      defaultAssigned = other.defaultAssigned;
      return *this;
    }

    /// Allow assigning from an ArrayRef.
    ListOption &operator=(ArrayRef<DataType> newValues) {
      values.assign(newValues.begin(), newValues.end());
      optHasValue = true;
      return *this;
    }

    /// Allow move-assigning from a SmallVector.
    template <unsigned N>
    ListOption &operator=(SmallVector<DataType, N> &&newValues) {
      values.assign(std::make_move_iterator(newValues.begin()),
                    std::make_move_iterator(newValues.end()));
      optHasValue = true;
      return *this;
    }

    /// Allow assigning from a SmallVector by const reference.
    template <unsigned N>
    ListOption &operator=(const SmallVector<DataType, N> &newValues) {
      values.assign(newValues.begin(), newValues.end());
      optHasValue = true;
      return *this;
    }

    /// Allow accessing the data held by this option.
    MutableArrayRef<DataType> operator*() { return values; }
    ArrayRef<DataType> operator*() const { return values; }

    /// Support range-based for loops.
    typename StorageType::iterator begin() { return values.begin(); }
    typename StorageType::iterator end() { return values.end(); }
    typename StorageType::const_iterator begin() const {
      return values.begin();
    }
    typename StorageType::const_iterator end() const { return values.end(); }

    /// Access elements.
    DataType &operator[](size_t i) { return values[i]; }
    const DataType &operator[](size_t i) const { return values[i]; }

    /// Get the number of elements.
    size_t size() const { return values.size(); }
    bool empty() const { return values.empty(); }

    /// Clear the list.
    void clear() { values.clear(); }

    /// Implicit conversion to ArrayRef.
    operator ArrayRef<DataType>() const { return values; }

    /// Push back a value.
    void push_back(const DataType &v) { values.push_back(v); }

    /// Alias for push_back.
    void addValue(const DataType &v) { push_back(v); }

  private:
    /// Parse comma-separated values. Returns true on error.
    bool parseValue(StringRef valueStr) override {
      if (defaultAssigned) {
        values.clear();
        defaultAssigned = false;
      }
      optHasValue = true;

      if (valueStr.empty())
        return false;

      return failed(pass_options::parseCommaSeparatedList(
          argStr, valueStr, [&](StringRef elemStr) -> LogicalResult {
            DataType elem{};
            if (parseSingleValue(elemStr, elem))
              return failure();
            values.push_back(std::move(elem));
            return success();
          }));
    }

    /// Print the name and value of this option to the given stream.
    void print(raw_ostream &os) override {
      // Don't print if still at default value.
      if (defaultAssigned && defaultValues.size() == values.size()) {
        if constexpr (pass_options::has_equality<DataType>::value) {
          bool allMatch = true;
          for (size_t i = 0; i < values.size(); ++i) {
            if (!(values[i] == defaultValues[i])) {
              allMatch = false;
              break;
            }
          }
          if (allMatch)
            return;
        } else {
          // If we can't compare elements, assume equal if sizes match and
          // values haven't been set.
          if (!optHasValue)
            return;
        }
      }

      os << argStr << "={";
      llvm::interleave(
          values, os, [&](const DataType &val) { printSingleValue(os, val); },
          ",");
      os << "}";
    }

    /// Copy the value from the given option into this one.
    void copyValueFrom(const OptionBase &other) override {
      const auto &otherList = static_cast<const ListOption &>(other);
      values = otherList.values;
      optHasValue = other.optHasValue;
      defaultAssigned = otherList.defaultAssigned;
    }

    /// Print help information.
    void printOptionInfo(size_t globalWidth) const override {
      size_t optWidth = getOptionWidth();
      llvm::outs() << "  --" << argStr;
      if (!enumEntries.empty()) {
        llvm::outs() << "=<value>";
        llvm::outs().indent(globalWidth > optWidth ? globalWidth - optWidth
                                                   : 0);
        llvm::outs() << "-   " << description << '\n';
        for (const auto &entry : enumEntries) {
          llvm::outs() << "    =" << entry.name;
          size_t indent = globalWidth > entry.name.size() + 5
                              ? globalWidth - entry.name.size() - 5
                              : 0;
          llvm::outs().indent(indent);
          llvm::outs() << "-   " << entry.description << '\n';
        }
      } else {
        llvm::outs().indent(globalWidth > optWidth ? globalWidth - optWidth
                                                   : 0);
        llvm::outs() << "-   " << description << '\n';
      }
    }

    /// Return the width required for printing help.
    size_t getOptionWidth() const override {
      size_t width = argStr.size() + 2;
      if (!enumEntries.empty()) {
        width += 8;
        for (const auto &entry : enumEntries)
          width = std::max(width, entry.name.size() + 5);
      }
      return width;
    }

    //===------------------------------------------------------------------===//
    // Single-value parsing (per element)
    //===------------------------------------------------------------------===//

    template <typename T = DataType>
    std::enable_if_t<std::is_same_v<T, bool>, bool>
    parseSingleValue(StringRef str, T &result) {
      return pass_options::parseBool(str, result);
    }

    template <typename T = DataType>
    std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, bool>
    parseSingleValue(StringRef str, T &result) {
      if (pass_options::parseInteger(str, result)) {
        llvm::errs() << "'" << str << "' value invalid for integer argument\n";
        return true;
      }
      return false;
    }

    template <typename T = DataType>
    std::enable_if_t<std::is_same_v<T, std::string>, bool>
    parseSingleValue(StringRef str, T &result) {
      return pass_options::parseString(str, result);
    }

    template <typename T = DataType>
    std::enable_if_t<std::is_enum_v<T>, bool> parseSingleValue(StringRef str,
                                                               T &result) {
      return pass_options::parseEnum(str, result, enumEntries, llvm::errs(),
                                     argStr);
    }

    template <typename T = DataType>
    std::enable_if_t<std::is_base_of_v<PassOptions, T>, bool>
    parseSingleValue(StringRef str, T &result) {
      return failed(result.parseFromString(str));
    }

    template <typename T = DataType>
    std::enable_if_t<
        !std::is_integral_v<T> && !std::is_same_v<T, std::string> &&
            !std::is_enum_v<T> && !std::is_base_of_v<PassOptions, T>,
        bool>
    parseSingleValue(StringRef str, T &result) {
      if constexpr (pass_options::OptionTypeHelper<T>::hasCustomHandler)
        return pass_options::OptionTypeHelper<T>::parse(str, result);
      if (!enumEntries.empty())
        return pass_options::parseEnum(str, result, enumEntries, llvm::errs(),
                                       argStr);
      llvm::errs() << "unsupported list element type for parsing\n";
      return true;
    }

    //===------------------------------------------------------------------===//
    // Single-value printing (per element)
    //===------------------------------------------------------------------===//

    template <typename T = DataType>
    std::enable_if_t<std::is_same_v<T, bool>> printSingleValue(raw_ostream &os,
                                                               const T &val) {
      pass_options::printBool(os, val);
    }

    template <typename T = DataType>
    std::enable_if_t<std::is_same_v<T, std::string>>
    printSingleValue(raw_ostream &os, const T &val) {
      pass_options::printString(os, val);
    }

    template <typename T = DataType>
    std::enable_if_t<std::is_base_of_v<PassOptions, T>>
    printSingleValue(raw_ostream &os, const T &val) {
      val.print(os);
    }

    template <typename T = DataType>
    std::enable_if_t<!std::is_same_v<T, bool> &&
                     !std::is_same_v<T, std::string> &&
                     !std::is_base_of_v<PassOptions, T>>
    printSingleValue(raw_ostream &os, const T &val) {
      if constexpr (pass_options::OptionTypeHelper<T>::hasCustomHandler) {
        pass_options::OptionTypeHelper<T>::print(os, val);
        return;
      }
      if (!enumEntries.empty()) {
        if (auto name = pass_options::findEnumName(val, enumEntries))
          os << *name;
        else
          llvm_unreachable("unknown enum value for list option");
        return;
      }
      if constexpr (pass_options::has_stream_operator<T>::value)
        os << val;
      else
        llvm_unreachable("cannot print list option value");
    }

    //===------------------------------------------------------------------===//
    // Modifier application
    //===------------------------------------------------------------------===//

    void applyModifiers() {}

    template <typename First, typename... Rest>
    void applyModifiers(First &&first, Rest &&...rest) {
      applyModifier(std::forward<First>(first));
      applyModifiers(std::forward<Rest>(rest)...);
    }

    void applyModifier(pass_options::Desc d) { description = d.value; }

    void applyModifier(pass_options::ValuesClass vc) {
      enumEntries = std::move(vc.entries);
    }

    /// Ignore any other modifiers.
    template <typename T>
    std::enable_if_t<!pass_options::is_known_modifier<std::decay_t<T>>::value>
    applyModifier(T &&) {}

    StorageType values;
    StorageType defaultValues;
    bool defaultAssigned = false;
    SmallVector<pass_options::EnumEntry> enumEntries;
  };

  PassOptions() = default;
  /// Delete the copy constructor to avoid copying the internal options map.
  PassOptions(const PassOptions &) = delete;
  PassOptions(PassOptions &&) = delete;

  /// Copy the option values from 'other' into 'this', where 'other' has the
  /// same options as 'this'.
  void copyOptionValuesFrom(const PassOptions &other);

  /// Parse options out as key=value pairs. Everything is space separated.
  LogicalResult parseFromString(StringRef options,
                                raw_ostream &errorStream = llvm::errs());

  /// Print the options held by this struct in a form that can be parsed via
  /// 'parseFromString'.
  void print(raw_ostream &os) const;

  /// Print the help string for the options held by this struct. `descIndent` is
  /// the indent that the descriptions should be aligned.
  void printHelp(size_t indent, size_t descIndent) const;

  /// Return the maximum width required when printing the help string.
  size_t getOptionWidth() const;

private:
  /// Register an option with this PassOptions instance.
  void addOption(OptionBase *option) {
    options.push_back(option);
    optionsMap[option->getArgStr()] = option;
  }

  /// A list of all of the opaque options.
  std::vector<OptionBase *> options;

  /// Map from option name to option pointer for fast lookup.
  llvm::StringMap<OptionBase *> optionsMap;
};
} // namespace detail

//===----------------------------------------------------------------------===//
// PassPipelineOptions
//===----------------------------------------------------------------------===//

/// Subclasses of PassPipelineOptions provide a set of options that can be used
/// to initialize a pass pipeline. See PassPipelineRegistration for usage
/// details.
///
/// Usage:
///
/// struct MyPipelineOptions : PassPipelineOptions<MyPassOptions> {
///   ListOption<int> someListFlag{*this, "flag-name", Desc("...")};
/// };
template <typename T>
class PassPipelineOptions : public virtual detail::PassOptions {
public:
  /// Factory that parses the provided options and returns a unique_ptr to the
  /// struct.
  static std::unique_ptr<T> createFromString(StringRef options) {
    auto result = std::make_unique<T>();
    if (failed(result->parseFromString(options)))
      return nullptr;
    return result;
  }
};

/// A default empty option struct to be used for passes that do not need to take
/// any options.
struct EmptyPipelineOptions : public PassPipelineOptions<EmptyPipelineOptions> {
};
} // namespace mlir

#endif // MLIR_PASS_PASSOPTIONS_H_
