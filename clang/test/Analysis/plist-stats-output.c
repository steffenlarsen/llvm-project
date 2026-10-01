// RUN: %clang_analyze_cc1 %s -analyzer-checker=core -analyzer-output=plist -analyzer-config serialize-stats=true -o %t.plist
// REQUIRES: asserts
// RUN: FileCheck --input-file=%t.plist %s

void foo(void) {}


// CHECK:  <key>diagnostics</key>
// CHECK-NEXT:  <array>
// CHECK-NEXT:  </array>
// CHECK-NEXT: <key>files</key>
// CHECK-NEXT: <array>
// CHECK-NEXT: </array>
// CHECK-NEXT: <key>statistics</key>
// CHECK-NEXT: <string>{
// The analyzer's own timers are serialized with the statistics.
// CHECK: time.analyzer.syntaxchecks.wall
// CHECK: }
// CHECK-NEXT: </string>
