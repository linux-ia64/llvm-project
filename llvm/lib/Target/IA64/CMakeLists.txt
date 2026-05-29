add_llvm_component_group(IA64)

# The IA64ISD SDNodes are declared by hand in IA64ISelLowering.h (as the
# pre-removal backend did and many in-tree targets still do), so
# -gen-sd-node-info is not used. AsmParser, Disassembler and object emission
# (-gen-asm-matcher / -gen-disassembler / MCCodeEmitter / AsmBackend) are out of
# Stage-1 scope (asm-output path only).

set(LLVM_TARGET_DEFINITIONS IA64.td)

tablegen(LLVM IA64GenRegisterInfo.inc -gen-register-info)
tablegen(LLVM IA64GenInstrInfo.inc -gen-instr-info)
tablegen(LLVM IA64GenCallingConv.inc -gen-callingconv)
tablegen(LLVM IA64GenDAGISel.inc -gen-dag-isel)
tablegen(LLVM IA64GenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM IA64GenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(IA64CommonTableGen)

add_llvm_target(IA64CodeGen
  IA64AsmPrinter.cpp
  IA64Bundling.cpp
  IA64FrameLowering.cpp
  IA64ISelDAGToDAG.cpp
  IA64ISelLowering.cpp
  IA64InstrInfo.cpp
  IA64MCInstLower.cpp
  IA64MachineFunctionInfo.cpp
  IA64RegisterInfo.cpp
  IA64Subtarget.cpp
  IA64TargetMachine.cpp

  LINK_COMPONENTS
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  MC
  SelectionDAG
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
