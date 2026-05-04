add_mlir_interface(SymbolInterfaces)
set(LLVM_TARGET_DEFINITIONS SymbolInterfaces.td)
mlir_tablegen(SymbolInterfacesAttrInterface.h.inc -gen-attr-interface-decls)
mlir_tablegen(SymbolInterfacesAttrInterface.cpp.inc -gen-attr-interface-defs)
add_mlir_interface(RegionKindInterface)

add_mlir_type_interface(QuantStorageTypeInterface)

set(LLVM_TARGET_DEFINITIONS OpAsmInterface.td)
mlir_tablegen(OpAsmAttrInterface.h.inc -gen-attr-interface-decls)
mlir_tablegen(OpAsmAttrInterface.cpp.inc -gen-attr-interface-defs)
mlir_tablegen(OpAsmOpInterface.h.inc -gen-op-interface-decls)
mlir_tablegen(OpAsmOpInterface.cpp.inc -gen-op-interface-defs)
mlir_tablegen(OpAsmTypeInterface.h.inc -gen-type-interface-decls)
mlir_tablegen(OpAsmTypeInterface.cpp.inc -gen-type-interface-defs)
add_mlir_generic_tablegen_target(MLIROpAsmInterfaceIncGen)

set(LLVM_TARGET_DEFINITIONS BuiltinAttributes.td)
mlir_tablegen(BuiltinAttributes.h.inc -gen-attrdef-decls)
mlir_tablegen(BuiltinAttributes.cpp.inc -gen-attrdef-defs)
add_mlir_generic_tablegen_target(MLIRBuiltinAttributesIncGen)

set(LLVM_TARGET_DEFINITIONS BuiltinAttributeInterfaces.td)
mlir_tablegen(BuiltinAttributeInterfaces.h.inc -gen-attr-interface-decls)
mlir_tablegen(BuiltinAttributeInterfaces.cpp.inc -gen-attr-interface-defs)
add_mlir_generic_tablegen_target(MLIRBuiltinAttributeInterfacesIncGen)

set(LLVM_TARGET_DEFINITIONS BuiltinDialect.td)
mlir_tablegen(BuiltinDialect.h.inc -gen-dialect-decls)
mlir_tablegen(BuiltinDialect.cpp.inc -gen-dialect-defs)
add_mlir_generic_tablegen_target(MLIRBuiltinDialectIncGen)

set(LLVM_TARGET_DEFINITIONS BuiltinDialectBytecode.td)
mlir_tablegen(BuiltinDialectBytecode.cpp.inc -gen-bytecode -bytecode-dialect="Builtin")
add_mlir_generic_tablegen_target(MLIRBuiltinDialectBytecodeIncGen)

set(LLVM_TARGET_DEFINITIONS BuiltinLocationAttributes.td)
mlir_tablegen(BuiltinLocationAttributes.h.inc -gen-attrdef-decls)
mlir_tablegen(BuiltinLocationAttributes.cpp.inc -gen-attrdef-defs)
add_mlir_generic_tablegen_target(MLIRBuiltinLocationAttributesIncGen)

set(LLVM_TARGET_DEFINITIONS BuiltinOps.td)
mlir_tablegen(BuiltinOps.h.inc -gen-op-decls)
mlir_tablegen(BuiltinOps.cpp.inc -gen-op-defs)
add_mlir_generic_tablegen_target(MLIRBuiltinOpsIncGen)

set(LLVM_TARGET_DEFINITIONS BuiltinTypes.td)
mlir_tablegen(BuiltinTypes.h.inc -gen-typedef-decls)
mlir_tablegen(BuiltinTypes.cpp.inc -gen-typedef-defs)
add_public_tablegen_target(MLIRBuiltinTypesIncGen)
mlir_tablegen(BuiltinTypeConstraints.h.inc -gen-type-constraint-decls)
mlir_tablegen(BuiltinTypeConstraints.cpp.inc -gen-type-constraint-defs)
add_mlir_generic_tablegen_target(MLIRBuiltinTypeConstraintsIncGen)

set(LLVM_TARGET_DEFINITIONS BuiltinTypeInterfaces.td)
mlir_tablegen(BuiltinTypeInterfaces.h.inc -gen-type-interface-decls)
mlir_tablegen(BuiltinTypeInterfaces.cpp.inc -gen-type-interface-defs)
add_mlir_generic_tablegen_target(MLIRBuiltinTypeInterfacesIncGen)

set(LLVM_TARGET_DEFINITIONS TensorEncoding.td)
mlir_tablegen(TensorEncInterfaces.h.inc -gen-attr-interface-decls)
mlir_tablegen(TensorEncInterfaces.cpp.inc -gen-attr-interface-defs)
add_mlir_generic_tablegen_target(MLIRTensorEncodingIncGen)

set(LLVM_TARGET_DEFINITIONS OpAsmDialectInterface.td)
mlir_tablegen(OpAsmDialectInterface.h.inc -gen-dialect-interface-decls)
add_mlir_generic_tablegen_target(MLIROpAsmDialectInterfaceIncGen)

add_mlir_doc(BuiltinAttributes BuiltinAttributes Dialects/ -gen-attrdef-doc)
add_mlir_doc(BuiltinLocationAttributes BuiltinLocationAttributes Dialects/ -gen-attrdef-doc)
add_mlir_doc(BuiltinOps BuiltinOps Dialects/ -gen-op-doc)
add_mlir_doc(BuiltinTypes BuiltinTypes Dialects/ -gen-typedef-doc)
add_mlir_doc(BuiltinTypes BuiltinTypeInterfaces Dialects/ -gen-type-interface-docs)

# Reset TABLEGEN_OUTPUT first: it accumulates across every tablegen() call in
# this directory, so without this the clv2 target would also depend on the
# unrelated .inc files generated above -- and in MLIR's case on mlir-tblgen,
# which creates a dependency cycle.
set(TABLEGEN_OUTPUT "")
# Also drop the accumulated LLVM_COMMON_DEPENDS for this one target.
# add_public_tablegen_target() add_dependencies() it onto the target, which
# here is every mlir_tablegen IncGen target declared above -- and so
# transitively mlir-tblgen.  This is a `tablegen(LLVM ...)` rule that needs
# only llvm-min-tblgen, and inheriting mlir-tblgen forms a cycle:
# mlir-tblgen -> MLIRTableGen -> ... -> MLIROptionsTableGen.
set(_mlir_saved_common_depends ${LLVM_COMMON_DEPENDS})
set(LLVM_COMMON_DEPENDS "")
set(LLVM_TARGET_DEFINITIONS MLIROptions.td)
tablegen(LLVM MLIROptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(MLIROptionsTableGen)
set(LLVM_COMMON_DEPENDS ${_mlir_saved_common_depends} MLIROptionsTableGen)
