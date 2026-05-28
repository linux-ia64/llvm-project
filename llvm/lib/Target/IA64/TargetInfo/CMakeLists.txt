add_llvm_component_library(LLVMIA64Info
  IA64TargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  IA64
  )
