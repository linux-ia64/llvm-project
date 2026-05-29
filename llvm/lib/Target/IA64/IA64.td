//===-- IA64.td - Target definition file for Intel IA64 ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a target description file for the Intel IA-64 architecture, also
// known variously as ia64, IA-64, IPF, "the Itanium architecture" etc.
//
//===----------------------------------------------------------------------===//

// Get the target-independent interfaces which we are implementing.
include "llvm/Target/Target.td"
include "llvm/TableGen/SearchableTable.td"

//===----------------------------------------------------------------------===//
// Register File Description
//===----------------------------------------------------------------------===//

include "IA64RegisterInfo.td"

// Map the PointerLikeRegClass (ptr_rc) operands of the target-independent
// pseudo-instructions to IA-64's general registers.
defm : RemapAllTargetPseudoPointerOperands<GR>;

//===----------------------------------------------------------------------===//
// Instruction Descriptions
//===----------------------------------------------------------------------===//

include "IA64InstrInfo.td"

def IA64InstrInfo : InstrInfo;

//===----------------------------------------------------------------------===//
// Calling Convention Description
//===----------------------------------------------------------------------===//

include "IA64CallingConv.td"

//===----------------------------------------------------------------------===//
// IA-64 processors
//===----------------------------------------------------------------------===//

// The pre-removal backend had a trivial subtarget with no features; a single
// generic processor with no scheduling model is enough to drive -gen-subtarget.
def : ProcessorModel<"generic", NoSchedModel, []>;

def IA64 : Target {
  let InstructionSet = IA64InstrInfo;
}
