add_clang_library(MLIRCIROffloadOpt
  KernelBindingTable.cpp
  DeadKernelElimination.cpp
  KernelArgConstantPropagation.cpp
  KernelCloning.cpp
  DeviceIndex.cpp
  TightenLaunchBounds.cpp

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
