add_llvm_component_library(LLVMIA64Desc
  IA64InstPrinter.cpp
  IA64MCAsmInfo.cpp
  IA64MCTargetDesc.cpp
  IA64TargetStreamer.cpp

  LINK_COMPONENTS
  MC
  IA64Info
  Support
  TargetParser

  ADD_TO_COMPONENT
  IA64
  )
