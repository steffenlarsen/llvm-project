#include "llvm/Target/XCore/XCoreOptions.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/LibraryOptions.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
using namespace llvm;
using namespace llvm::opt;
namespace {
enum ID {
  OPT_INVALID = 0,
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "llvm/Target/XCore/XCoreOptions.inc"
#undef OPTION
};
#define OPTTABLE_STR_TABLE_CODE
#include "llvm/Target/XCore/XCoreOptions.inc"
#undef OPTTABLE_STR_TABLE_CODE
#define OPTTABLE_PREFIXES_TABLE_CODE
#include "llvm/Target/XCore/XCoreOptions.inc"
#undef OPTTABLE_PREFIXES_TABLE_CODE
static constexpr OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "llvm/Target/XCore/XCoreOptions.inc"
#undef OPTION
};
class XCoreOptTable : public GenericOptTable {
public:
  XCoreOptTable()
      : GenericOptTable(OptionStrTable, OptionPrefixesTable, InfoTable) {}
};
} // namespace
llvm::opt::OptTable &llvm::XCoreOptions::table() {
  static XCoreOptTable Table;
  return Table;
}
#define OPTIONS_STRUCT_DEFS
#include "llvm/Target/XCore/XCoreOptions.inc"
#undef OPTIONS_STRUCT_DEFS
static llvm::opt::RegisterLibraryOptions<llvm::XCoreOptions> Registration;
