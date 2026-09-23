set(LLVM_TARGET_DEFINITIONS ${PROJECT_SOURCE_DIR}/include/llvm/CodeGen/ValueTypes.td)
tablegen(LLVM GenVT.inc -gen-vt)
add_public_tablegen_target(vt_gen)

# Reset TABLEGEN_OUTPUT first: it accumulates across every tablegen() call in
# this directory, so without this the clv2 target would also depend on the
# unrelated .inc files generated above -- and in MLIR's case on mlir-tblgen,
# which creates a dependency cycle.
set(TABLEGEN_OUTPUT "")
set(LLVM_TARGET_DEFINITIONS CommandFlagsOptions.td)
tablegen(LLVM CommandFlagsOptInfos.inc -gen-clv2-options)

add_public_tablegen_target(CommandFlagsTableGen)

# Reset TABLEGEN_OUTPUT again so this target doesn't also depend on the
# unrelated .inc files generated above (see the comment at the top of this
# file).
set(TABLEGEN_OUTPUT "")
# -gen-opt-parser-defs is only implemented by the full llvm-tblgen, not the
# llvm-min-tblgen bootstrap tool that LLVM_HEADERS (set above us in
# llvm/include/llvm/CMakeLists.txt) would otherwise route this to.
set(LLVM_TABLEGEN_PROJECT "")
set(LLVM_TARGET_DEFINITIONS CodeGenPassOptionsAsmPrint.td)
tablegen(LLVM CodeGenPassOptionsAsmPrint.inc -gen-opt-parser-defs)
# Deliberately not named *OptionsTableGen: that suffix is matched by
# _collect_clv2_options_targets() (TableGen.cmake) to build the
# llvm_clv2_options_gen aggregate, which is itself a dependency of
# llvm-tblgen. Since this target depends on llvm-tblgen above, matching the
# suffix would create a cycle.
add_public_tablegen_target(CodeGenAsmPrintOptParserTableGen)

# Reset TABLEGEN_OUTPUT again so this target doesn't also depend on the
# unrelated .inc files generated above (see the comment at the top of this
# file).
set(TABLEGEN_OUTPUT "")
set(LLVM_TARGET_DEFINITIONS CodeGenPassOptionsGISel.td)
tablegen(LLVM CodeGenPassOptionsGISel.inc -gen-opt-parser-defs)
# Deliberately not named *OptionsTableGen: see the comment above
# CodeGenAsmPrintOptParserTableGen.
add_public_tablegen_target(CodeGenGISelOptParserTableGen)

# Reset TABLEGEN_OUTPUT again so this target doesn't also depend on the
# unrelated .inc files generated above (see the comment at the top of this
# file).
set(TABLEGEN_OUTPUT "")
set(LLVM_TARGET_DEFINITIONS CodeGenPassOptionsRegAlloc.td)
tablegen(LLVM CodeGenPassOptionsRegAlloc.inc -gen-opt-parser-defs)
# Deliberately not named *OptionsTableGen: see the comment above
# CodeGenAsmPrintOptParserTableGen.
add_public_tablegen_target(CodeGenRegAllocOptParserTableGen)

# Reset TABLEGEN_OUTPUT again so this target doesn't also depend on the
# unrelated .inc files generated above (see the comment at the top of this
# file).
set(TABLEGEN_OUTPUT "")
set(LLVM_TARGET_DEFINITIONS CodeGenPassOptionsSched1.td)
tablegen(LLVM CodeGenPassOptionsSched1.inc -gen-opt-parser-defs)
# Deliberately not named *OptionsTableGen: see the comment above
# CodeGenAsmPrintOptParserTableGen.
add_public_tablegen_target(CodeGenSched1OptParserTableGen)

# Reset TABLEGEN_OUTPUT again so this target doesn't also depend on the
# unrelated .inc files generated above (see the comment at the top of this
# file).
set(TABLEGEN_OUTPUT "")
set(LLVM_TARGET_DEFINITIONS CodeGenPassOptionsSched2.td)
tablegen(LLVM CodeGenPassOptionsSched2.inc -gen-opt-parser-defs)
# Deliberately not named *OptionsTableGen: see the comment above
# CodeGenAsmPrintOptParserTableGen.
add_public_tablegen_target(CodeGenSched2OptParserTableGen)

# Reset TABLEGEN_OUTPUT again so this target doesn't also depend on the
# unrelated .inc files generated above (see the comment at the top of this
# file).
set(TABLEGEN_OUTPUT "")
set(LLVM_TARGET_DEFINITIONS CodeGenPassOptionsMachine1.td)
tablegen(LLVM CodeGenPassOptionsMachine1.inc -gen-opt-parser-defs)
# Deliberately not named *OptionsTableGen: see the comment above
# CodeGenAsmPrintOptParserTableGen.
add_public_tablegen_target(CodeGenMachine1OptParserTableGen)

# Reset TABLEGEN_OUTPUT again so this target doesn't also depend on the
# unrelated .inc files generated above (see the comment at the top of this
# file).
set(TABLEGEN_OUTPUT "")
set(LLVM_TARGET_DEFINITIONS CodeGenPassOptionsMachine2.td)
tablegen(LLVM CodeGenPassOptionsMachine2.inc -gen-opt-parser-defs)
# Deliberately not named *OptionsTableGen: see the comment above
# CodeGenAsmPrintOptParserTableGen.
add_public_tablegen_target(CodeGenMachine2OptParserTableGen)

# Reset TABLEGEN_OUTPUT again so this target doesn't also depend on the
# unrelated .inc files generated above (see the comment at the top of this
# file).
set(TABLEGEN_OUTPUT "")
set(LLVM_TARGET_DEFINITIONS CodeGenPassOptionsSelDAG.td)
tablegen(LLVM CodeGenPassOptionsSelDAG.inc -gen-opt-parser-defs)
# Deliberately not named *OptionsTableGen: see the comment above
# CodeGenAsmPrintOptParserTableGen.
add_public_tablegen_target(CodeGenSelDAGOptParserTableGen)

# Reset TABLEGEN_OUTPUT again so this target doesn't also depend on the
# unrelated .inc files generated above (see the comment at the top of this
# file).
set(TABLEGEN_OUTPUT "")
set(LLVM_TARGET_DEFINITIONS CodeGenPassOptionsCore2.td)
tablegen(LLVM CodeGenPassOptionsCore2.inc -gen-opt-parser-defs)
# Deliberately not named *OptionsTableGen: see the comment above
# CodeGenAsmPrintOptParserTableGen.
add_public_tablegen_target(CodeGenCore2OptParserTableGen)

# Reset TABLEGEN_OUTPUT again so this target doesn't also depend on the
# unrelated .inc files generated above (see the comment at the top of this
# file).
set(TABLEGEN_OUTPUT "")
set(LLVM_TARGET_DEFINITIONS CodeGenPassOptionsCore1.td)
tablegen(LLVM CodeGenPassOptionsCore1.inc -gen-opt-parser-defs)
# Deliberately not named *OptionsTableGen: see the comment above
# CodeGenAsmPrintOptParserTableGen.
add_public_tablegen_target(CodeGenCore1OptParserTableGen)
