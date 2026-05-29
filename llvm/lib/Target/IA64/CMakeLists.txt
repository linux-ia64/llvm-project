add_llvm_component_group(IA64)

# -----------------------------------------------------------------------------
# Phase C (TableGen .td migration) is done incrementally. So far the register
# file (IA64RegisterInfo.td) and the instruction descriptions
# (IA64InstrFormats.td + IA64InstrInfo.td) are ported and wired to
# -gen-register-info and -gen-instr-info below. The DAG selection patterns,
# PatLeaf predicates, IA64ISD SDNodes and Pat<> rules are deferred to Phase D
# (they reference the lowering), so the pattern-driven generators are not wired
# yet. The generated .inc files are not consumed by C++ yet (that happens in
# Phase D), but generating them now keeps the tree building and verifies the .td.
#
# Remaining Phase C sub-steps grow IA64.td and add the matching tablegen calls,
# modeled on lib/Target/Sparc:
#
#   tablegen(LLVM IA64GenAsmWriter.inc     -gen-asm-writer)
#   tablegen(LLVM IA64GenSubtargetInfo.inc -gen-subtarget)
#
# The IA64ISD SDNodes are declared by hand in IA64ISelLowering.h (as the
# pre-removal backend did and many in-tree targets still do), so -gen-sd-node-info
# is not used.
#
# Phase D then grows the add_llvm_target source list (IA64AsmPrinter.cpp,
# IA64Bundling.cpp, IA64FrameLowering.cpp, IA64InstrInfo.cpp,
# IA64ISelDAGToDAG.cpp, IA64ISelLowering.cpp, IA64MCInstLower.cpp,
# IA64RegisterInfo.cpp, IA64Subtarget.cpp) plus the IA64Desc link component.
# -----------------------------------------------------------------------------

set(LLVM_TARGET_DEFINITIONS IA64.td)

tablegen(LLVM IA64GenRegisterInfo.inc -gen-register-info)
tablegen(LLVM IA64GenInstrInfo.inc -gen-instr-info)
tablegen(LLVM IA64GenCallingConv.inc -gen-callingconv)
tablegen(LLVM IA64GenDAGISel.inc -gen-dag-isel)

add_public_tablegen_target(IA64CommonTableGen)

add_llvm_target(IA64CodeGen
  IA64FrameLowering.cpp
  IA64ISelDAGToDAG.cpp
  IA64ISelLowering.cpp
  IA64InstrInfo.cpp
  IA64MachineFunctionInfo.cpp
  IA64RegisterInfo.cpp
  IA64TargetMachine.cpp

  LINK_COMPONENTS
  CodeGen
  CodeGenTypes
  Core
  MC
  Support
  Target
  TargetParser
  IA64Desc
  IA64Info

  ADD_TO_COMPONENT
  IA64
  )

add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
