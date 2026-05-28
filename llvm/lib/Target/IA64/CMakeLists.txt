add_llvm_component_group(IA64)

# -----------------------------------------------------------------------------
# Phase C (TableGen .td migration) is done incrementally. So far the register
# file (IA64RegisterInfo.td) is ported and wired to -gen-register-info below;
# IA64.td still carries an empty InstrInfo placeholder. The generated
# IA64GenRegisterInfo.inc is not consumed by C++ yet (that happens in Phase D),
# but generating it now keeps it building and verifies the .td.
#
# Remaining Phase C sub-steps grow IA64.td and add the matching tablegen calls,
# modeled on lib/Target/Sparc:
#
#   tablegen(LLVM IA64GenInstrInfo.inc     -gen-instr-info)
#   tablegen(LLVM IA64GenAsmWriter.inc     -gen-asm-writer)
#   tablegen(LLVM IA64GenDAGISel.inc       -gen-dag-isel)
#   tablegen(LLVM IA64GenCallingConv.inc   -gen-callingconv)
#   tablegen(LLVM IA64GenSubtargetInfo.inc -gen-subtarget)
#   tablegen(LLVM IA64GenSDNodeInfo.inc    -gen-sd-node-info -sdnode-namespace=IA64ISD)
#
# Phase D then grows the add_llvm_target source list (IA64AsmPrinter.cpp,
# IA64Bundling.cpp, IA64FrameLowering.cpp, IA64InstrInfo.cpp,
# IA64ISelDAGToDAG.cpp, IA64ISelLowering.cpp, IA64MCInstLower.cpp,
# IA64RegisterInfo.cpp, IA64Subtarget.cpp) plus the IA64Desc link component.
# -----------------------------------------------------------------------------

set(LLVM_TARGET_DEFINITIONS IA64.td)

tablegen(LLVM IA64GenRegisterInfo.inc -gen-register-info)

add_public_tablegen_target(IA64CommonTableGen)

add_llvm_target(IA64CodeGen
  IA64TargetMachine.cpp

  LINK_COMPONENTS
  Support

  ADD_TO_COMPONENT
  IA64
  )

add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
