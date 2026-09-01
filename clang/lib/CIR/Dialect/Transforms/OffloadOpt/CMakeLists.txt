add_clang_library(MLIRCIROffloadOpt
  KernelBindingTable.cpp
  KernelCloning.cpp
  DeviceIndex.cpp
  DeadKernelElimination.cpp
  KernelArgConstantPropagation.cpp

  DEPENDS
  MLIRCIROpsIncGen
  MLIRCIRPassIncGen

  LINK_LIBS PUBLIC
  MLIRLLVMDialect
  MLIRCIRTransforms
  MLIRCIR
  MLIRIR
  MLIRPass
  MLIRSupport
  )
