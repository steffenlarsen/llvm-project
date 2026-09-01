add_clang_library(MLIRCIROffloadOpt
  KernelBindingTable.cpp
  KernelCloning.cpp
  DeadKernelElimination.cpp
  KernelArgConstantPropagation.cpp
  LaunchBoundsPropagation.cpp
  DeadArgElimination.cpp

  DEPENDS
  MLIRCIROpsIncGen
  MLIRCIRPassIncGen

  LINK_LIBS PUBLIC
  MLIRCIR
  MLIRIR
  MLIRPass
  MLIRSupport
  )
