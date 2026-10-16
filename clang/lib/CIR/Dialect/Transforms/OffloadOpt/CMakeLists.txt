add_clang_library(MLIRCIROffloadOpt
  KernelBindingTable.cpp
  KernelCloning.cpp
  LocalSlots.cpp
  DeviceIndex.cpp
  DeadKernelElimination.cpp
  KernelArgConstantPropagation.cpp
  LaunchBoundsPropagation.cpp
  SpecializeLaunchWrappers.cpp
  SpecializeConstantArgs.cpp
  PromoteKernelArgSpace.cpp
  PropagateBlockShape.cpp
  PropagatePointerFacts.cpp
  EliminateCoveredGuards.cpp
  SpecializeDriverFields.cpp
  DeadArgElimination.cpp
  InlineLaunchPaths.cpp

  DEPENDS
  MLIRCIROpsIncGen
  MLIRCIRPassIncGen

  LINK_LIBS PUBLIC
  MLIRAnalysis
  MLIRTransformUtils
  MLIRLLVMDialect
  MLIRCIRTransforms
  MLIRCIR
  MLIRIR
  MLIRPass
  MLIRSupport
  )
