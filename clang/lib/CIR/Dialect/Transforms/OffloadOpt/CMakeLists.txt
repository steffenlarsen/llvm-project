add_clang_library(MLIRCIROffloadOpt
  KernelBindingTable.cpp
  DeadKernelElimination.cpp
  KernelArgConstantPropagation.cpp
  SpecializeConstantArgs.cpp
  KernelCloning.cpp
  DeviceIndex.cpp
  PropagateBlockShape.cpp
  TightenLaunchBounds.cpp
  EliminateCoveredGuards.cpp

  DEPENDS
  MLIRCIROpsIncGen
  MLIRCIRPassIncGen

  LINK_LIBS PUBLIC
  MLIRCIR
  MLIRLLVMDialect
  MLIRCIRTransforms
  MLIRIR
  MLIRPass
  MLIRSupport
  )
