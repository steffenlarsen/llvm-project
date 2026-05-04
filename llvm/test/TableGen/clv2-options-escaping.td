// RUN: llvm-tblgen -gen-clv2-options -I %p/../../include %s | FileCheck %s

// Descriptions and value names reach the generated header as C string
// literals, so every byte has to survive the round trip.  Definitions are
// emitted in alphabetical order, which is the order checked below.

include "llvm/Support/CLV2Options.td"

def TestReg : Registry<"test_opts">;

// A non-printable byte followed by hex digits.  This must NOT be emitted as
// \xNN: C++ hex escapes have no digit limit, so "\x01abc" is one oversized
// escape rather than a byte plus "abc".  Octal caps at three digits.
def EscNonPrintable : Option<"esc-nonprintable", "byteabc after",
                             "std::string"> {
  let Registry = TestReg;
}
// CHECK: EscNonPrintable{
// CHECK-NEXT: "esc-nonprintable", "byte\001abc after"

// The ordinary escapes.
def EscQuoteBackslash : Option<"esc-quote", "a \"quoted\" and \\ backslash",
                               "std::string"> {
  let Registry = TestReg;
}
// CHECK: EscQuoteBackslash{
// CHECK-NEXT: "esc-quote", "a \"quoted\" and \\ backslash"

// value_desc goes through the same escaping.
def EscValueDesc : Option<"esc-value-desc", "has a value name",
                          "std::string"> {
  let Registry = TestReg;
  let ValueDesc = "a\"b";
}
// CHECK: EscValueDesc{
// CHECK-NEXT: "esc-value-desc", "has a value name", value_desc("a\"b")

// No hex escape should appear anywhere in the output.
// CHECK-NOT: \x
