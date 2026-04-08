# Generate dialect + op declarations/definitions from OffloadOps.td.
# OffloadOps.td includes OffloadBase.td and OffloadTypes.td, so a single
# add_mlir_dialect() call covers the whole dialect.
add_mlir_dialect(OffloadOps offload)
add_mlir_doc(OffloadOps OffloadOps Dialects/ -gen-op-doc -dialect=offload)

# Enum declarations/definitions (ExecSpace, MemSpace).
set(LLVM_TARGET_DEFINITIONS OffloadOps.td)
mlir_tablegen(OffloadOpsEnums.h.inc   -gen-enum-decls)
mlir_tablegen(OffloadOpsEnums.cpp.inc -gen-enum-defs)
add_mlir_dialect_tablegen_target(MLIROffloadOpsEnumsGen)

# Attribute declarations/definitions (LaunchBoundsAttr, etc.).
set(LLVM_TARGET_DEFINITIONS OffloadOps.td)
mlir_tablegen(OffloadOpsAttributes.h.inc   -gen-attrdef-decls -attrdefs-dialect=offload)
mlir_tablegen(OffloadOpsAttributes.cpp.inc -gen-attrdef-defs  -attrdefs-dialect=offload)
add_mlir_dialect_tablegen_target(MLIROffloadOpsAttributesIncGen)

# Type declarations/definitions (StreamType, EventType).
set(LLVM_TARGET_DEFINITIONS OffloadOps.td)
mlir_tablegen(OffloadOpsTypes.h.inc   -gen-typedef-decls -typedefs-dialect=offload)
mlir_tablegen(OffloadOpsTypes.cpp.inc -gen-typedef-defs  -typedefs-dialect=offload)
add_mlir_dialect_tablegen_target(MLIROffloadOpsTypesIncGen)
