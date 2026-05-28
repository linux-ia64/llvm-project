add_llvm_component_library(LLVMIA64Desc
  IA64MCTargetDesc.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  IA64
  )
