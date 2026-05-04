// RUN: not llvm-tblgen -gen-clv2-options -I %p/../../include %s 2>&1 \
// RUN:   | FileCheck %s

// toCamelCase drops '-', so CLI names that differ only by hyphenation derive
// the same getter.  Without a check this is a redefinition error inside the
// generated header, which points at the .inc instead of at the .td.

include "llvm/Support/CLV2Options.td"

def TestReg : Registry<"test_opts">;

def First : Option<"foo-bar", "hyphenated", "bool"> {
  let Registry = TestReg;
}

// "fooBar" camel-cases to the same "FooBar" as "foo-bar".
def Second : Option<"fooBar", "already camel", "bool"> {
  let Registry = TestReg;
}

// CHECK: error: getter name 'test_opts::getFooBar' collides with option 'foo-bar'; set GetterName on one of them
// CHECK: note: previous option with this getter is here
// CHECK: error: duplicate clv2 getter name
