// PROTOTYPE (Stage 3.1): parsing from a recorded token stream.
//
// Stage 3 preprocesses once per target and splices the recordings into one
// stream for a single parse, so the parser has to be able to run off a
// recording at all. This checks that doing so is transparent: recording the
// whole expanded stream and replaying it must produce identical IR.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O1 -emit-llvm -o %t.direct %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O1 -emit-llvm -o %t.replay %s \
// RUN:   -mllvm -record-replay-tokens
// RUN: diff %t.direct %t.replay

// A translation unit with no declarations at all: Parser::Initialize primes
// the look-ahead with one ConsumeToken, which for an empty unit is already the
// eof, so there is nothing left to record.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o /dev/null \
// RUN:   -x c++ /dev/null -mllvm -record-replay-tokens

#define SQUARE(x) ((x) * (x))
#define REP4(m) m(0) m(1) m(2) m(3)
#define ENTRY(n) int e##n = SQUARE(n);

template <typename T> struct Holder {
  T value;
  static constexpr unsigned size = sizeof(T);
  T doubled() const { return value + value; }
};

REP4(ENTRY)

int use() {
  Holder<int> hi{2};
  Holder<double> hd{3.0};
  return hi.doubled() + static_cast<int>(hd.doubled()) + Holder<long>::size +
         e0 + e1 + e2 + e3;
}
