include "llvm/Option/OptParser.td"

def ObjectOptions : OptionsStruct<"llvm"> {}

defm OBJ_DisableBitcodeVersionUpgrade : BoolField<
    "disable-bitcode-version-upgrade", "false",
    "Disable automatic bitcode upgrade for version mismatch", /*hidden=*/1>;
