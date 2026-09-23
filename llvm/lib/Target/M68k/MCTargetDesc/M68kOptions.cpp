#include "llvm/Target/M68k/M68kOptions.h"
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
#include "llvm/Target/M68k/M68kOptions.inc"
#undef OPTION
};
#define OPTTABLE_STR_TABLE_CODE
#include "llvm/Target/M68k/M68kOptions.inc"
#undef OPTTABLE_STR_TABLE_CODE
#define OPTTABLE_PREFIXES_TABLE_CODE
#include "llvm/Target/M68k/M68kOptions.inc"
#undef OPTTABLE_PREFIXES_TABLE_CODE
static constexpr OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "llvm/Target/M68k/M68kOptions.inc"
#undef OPTION
};
class M68kOptTable : public GenericOptTable {
public:
  M68kOptTable()
      : GenericOptTable(OptionStrTable, OptionPrefixesTable, InfoTable) {}
};
} // namespace
llvm::opt::OptTable &llvm::M68kOptions::table() {
  static M68kOptTable Table;
  return Table;
}
#define OPTIONS_STRUCT_DEFS
#include "llvm/Target/M68k/M68kOptions.inc"
#undef OPTIONS_STRUCT_DEFS
static llvm::opt::RegisterLibraryOptions<llvm::M68kOptions> Registration;
