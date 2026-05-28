add_llvm_component_group(IA64)

# -----------------------------------------------------------------------------
# Phase A (build integration).
#
# LLVM requires every target in LLVM_TARGETS_TO_BUILD to provide an
# LLVM<Target>CodeGen component plus the LLVMInitializeIA64{Target,TargetInfo,
# TargetMC} entry points referenced by the generated Targets.def. These are
# split across three libraries by convention (so tools only link what they
# need): TargetInfo (LLVMIA64Info), MCTargetDesc (LLVMIA64Desc, holds
# ...TargetMC), and CodeGen (LLVMIA64CodeGen, holds ...Target). Minimal stub
# versions are defined here. This is enough for
#   -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=IA64
# to configure and build, and for `llc --version` to list ia64. The target is
# not yet usable for codegen (no real TargetMachine / MC layer until B-D).
#
# Phases B-D grow this into the full definition, modeled on lib/Target/Sparc:
#
#   set(LLVM_TARGET_DEFINITIONS IA64.td)
#   tablegen(LLVM IA64GenRegisterInfo.inc  -gen-register-info)
#   tablegen(LLVM IA64GenInstrInfo.inc     -gen-instr-info)
#   tablegen(LLVM IA64GenAsmWriter.inc     -gen-asm-writer)
#   tablegen(LLVM IA64GenDAGISel.inc       -gen-dag-isel)
#   tablegen(LLVM IA64GenCallingConv.inc   -gen-callingconv)
#   tablegen(LLVM IA64GenSubtargetInfo.inc -gen-subtarget)
#   tablegen(LLVM IA64GenSDNodeInfo.inc    -gen-sd-node-info -sdnode-namespace=IA64ISD)
#   add_public_tablegen_target(IA64CommonTableGen)
#
#   ... and the add_llvm_target below grows the source list (IA64AsmPrinter.cpp,
#   IA64Bundling.cpp, IA64FrameLowering.cpp, IA64InstrInfo.cpp,
#   IA64ISelDAGToDAG.cpp, IA64ISelLowering.cpp, IA64MCInstLower.cpp,
#   IA64RegisterInfo.cpp, IA64Subtarget.cpp) plus the IA64Desc link component.
# -----------------------------------------------------------------------------

add_llvm_target(IA64CodeGen
  IA64TargetMachine.cpp

  LINK_COMPONENTS
  Support

  ADD_TO_COMPONENT
  IA64
  )

add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
