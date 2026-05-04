! Verify that `-mllvm` and `-mmlir` reach the same option set.
!
! A single parser handles both, so either flag accepts any LLVM, MLIR or Flang
! option and both report the same banner. This is deliberate: the checks below
! are positive on both sides so that re-separating them fails here rather than
! silently changing what each flag accepts.

! RUN: %flang_fc1  -mmlir --help | FileCheck %s --check-prefix=MLIR
! RUN: %flang_fc1  -mllvm --help | FileCheck %s --check-prefix=MLLVM

! MLIR: flang (option parsing) [options]
! MLIR: --mlir-{{.*}}

! MLLVM: flang (option parsing) [options]
! MLLVM: --mlir-{{.*}}
