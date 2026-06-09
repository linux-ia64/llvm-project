//===- IA64RegisterInfo.cpp - IA64 Register Information ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the IA64 implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "IA64RegisterInfo.h"
#include "IA64FrameLowering.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"

#define GET_REGINFO_TARGET_DESC
#include "IA64GenRegisterInfo.inc"

using namespace llvm;

// rp (the return pointer, branch register b0) is the return-address register.
IA64RegisterInfo::IA64RegisterInfo() : IA64GenRegisterInfo(IA64::rp) {}

const MCPhysReg *
IA64RegisterInfo::getCalleeSavedRegs(const MachineFunction * /*MF*/) const {
  // r4-r7 are the static callee-saved general registers (IA-64 SysV psABI);
  // glibc's setjmp/longjmp save and restore them via the jmpbuf. The backend
  // rarely allocates them (they trail the GR allocation order), but LowerCall
  // parks gp/sp/rp in r4/r6/r7 across calls in returns_twice (setjmp) functions
  // -- which only works if every function that touches them saves/restores them,
  // i.e. they must be true CSRs so a nested setjmp call does not clobber an
  // outer frame's parked values. (r5 is also the frame pointer.)
  static const MCPhysReg CalleeSavedRegs[] = {IA64::r4, IA64::r5, IA64::r6,
                                              IA64::r7, 0};
  return CalleeSavedRegs;
}

BitVector IA64RegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  Reserved.set(IA64::r0);  // always zero
  Reserved.set(IA64::r1);  // global data pointer (gp)
  Reserved.set(IA64::r2);  // reserved for spilling/filling predicates
  Reserved.set(IA64::r5);  // frame pointer
  Reserved.set(IA64::r12); // stack pointer (sp)
  Reserved.set(IA64::r13); // thread pointer (tp)
  Reserved.set(IA64::r22); // reserved as an address-calculation scratch
  Reserved.set(IA64::rp);  // return pointer (b0)

  // F0 and F1 are the architectural fixed FP constants +0.0 and +1.0; they are
  // members of the FP class only so they can be named as explicit operands
  // (e.g. F0 is the addend in the xma-based integer-multiply sequence). They
  // must never be allocated as scratch, or the constant they hold is clobbered.
  Reserved.set(IA64::F0);  // fixed +0.0
  Reserved.set(IA64::F1);  // fixed +1.0

  // The output registers (out0-out7) are an alias for the top of the stacked
  // register frame that 'alloc' carves out for passing arguments to callees;
  // they are not freely allocatable. The pre-removal backend hid them from the
  // GR allocation order via RegisterClass MethodBodies (a mechanism that no
  // longer exists); we express that reservation here. They lead the GR
  // allocation order, so without this the ar.pfs-save GR lands on 'out7',
  // which is meaningless when 'alloc' declares zero output registers.
  Reserved.set(IA64::out0);
  Reserved.set(IA64::out1);
  Reserved.set(IA64::out2);
  Reserved.set(IA64::out3);
  Reserved.set(IA64::out4);
  Reserved.set(IA64::out5);
  Reserved.set(IA64::out6);
  Reserved.set(IA64::out7);

  // ar.pfs is an application register, not a freely allocatable GPR — it is a
  // member of the GR class only so 'mov ar.pfs = rN' / 'alloc rN = ar.pfs' can
  // name it. The pre-removal backend kept it out of the GR allocation order via
  // RegisterClass MethodBodies; reserving it here is the modern equivalent.
  // Without this, the coalescer folds the ar.pfs-save vreg straight into
  // AR_PFS, producing the nonsensical 'alloc ar.pfs = ar.pfs' (the save GR is
  // lost). Reserved, the restore copy 'mov ar.pfs = rN' survives and the
  // save vreg is allocated to a real scratch GR (r3 for a leaf function).
  Reserved.set(IA64::AR_PFS);
  Reserved.set(IA64::B6);  // indirect-call branch target (set up per call site)
  return Reserved;
}

bool IA64RegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                           int SPAdj, unsigned FIOperandNum,
                                           RegScavenger * /*RS*/) const {
  assert(SPAdj == 0 && "Unexpected");

  MachineInstr &MI = *II;
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  DebugLoc DL = MI.getDebugLoc();

  bool FP = TFI->hasFP(MF);

  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();

  // Choose a base register: ( hasFP ? frame pointer : stack pointer ).
  unsigned BaseRegister = FP ? IA64::r5 : IA64::r12;

  // Add the frame object offset to the offset from the base register.
  int Offset = MF.getFrameInfo().getObjectOffset(FrameIndex);
  Offset += MF.getFrameInfo().getStackSize();

  // We use 'r22' as an address-calculation scratch register here.
  MI.getOperand(FIOperandNum).ChangeToRegister(IA64::r22, false);
  if (Offset <= 8191 && Offset >= -8192) { // smallish offset
    BuildMI(MBB, II, DL, TII->get(IA64::ADDIMM22), IA64::r22)
        .addReg(BaseRegister)
        .addImm(Offset);
  } else { // it's big
    BuildMI(MBB, II, DL, TII->get(IA64::MOVLIMM64), IA64::r22).addImm(Offset);
    BuildMI(MBB, II, DL, TII->get(IA64::ADD), IA64::r22)
        .addReg(BaseRegister)
        .addReg(IA64::r22);
  }

  return false;
}

Register IA64RegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  return TFI->hasFP(MF) ? IA64::r5 : IA64::r12;
}
