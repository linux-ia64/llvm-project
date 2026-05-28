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

// The 96 stacked general registers in register-stack order (index 0 == r32).
static const MCPhysReg StackedGPRsInOrder[IA64NumStackedGPRs] = {
    IA64::r32,  IA64::r33,  IA64::r34,  IA64::r35,  IA64::r36,  IA64::r37,
    IA64::r38,  IA64::r39,  IA64::r40,  IA64::r41,  IA64::r42,  IA64::r43,
    IA64::r44,  IA64::r45,  IA64::r46,  IA64::r47,  IA64::r48,  IA64::r49,
    IA64::r50,  IA64::r51,  IA64::r52,  IA64::r53,  IA64::r54,  IA64::r55,
    IA64::r56,  IA64::r57,  IA64::r58,  IA64::r59,  IA64::r60,  IA64::r61,
    IA64::r62,  IA64::r63,  IA64::r64,  IA64::r65,  IA64::r66,  IA64::r67,
    IA64::r68,  IA64::r69,  IA64::r70,  IA64::r71,  IA64::r72,  IA64::r73,
    IA64::r74,  IA64::r75,  IA64::r76,  IA64::r77,  IA64::r78,  IA64::r79,
    IA64::r80,  IA64::r81,  IA64::r82,  IA64::r83,  IA64::r84,  IA64::r85,
    IA64::r86,  IA64::r87,  IA64::r88,  IA64::r89,  IA64::r90,  IA64::r91,
    IA64::r92,  IA64::r93,  IA64::r94,  IA64::r95,  IA64::r96,  IA64::r97,
    IA64::r98,  IA64::r99,  IA64::r100, IA64::r101, IA64::r102, IA64::r103,
    IA64::r104, IA64::r105, IA64::r106, IA64::r107, IA64::r108, IA64::r109,
    IA64::r110, IA64::r111, IA64::r112, IA64::r113, IA64::r114, IA64::r115,
    IA64::r116, IA64::r117, IA64::r118, IA64::r119, IA64::r120, IA64::r121,
    IA64::r122, IA64::r123, IA64::r124, IA64::r125, IA64::r126, IA64::r127};

MCRegister llvm::getIA64StackedGPR(unsigned Idx) {
  assert(Idx < IA64NumStackedGPRs && "stacked-GPR index out of range");
  return StackedGPRsInOrder[Idx];
}

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

  // Cap the stacked-GPR register frame. 'alloc' carves a frame of
  // (locals + outputs) stacked registers out of r32-r127, and the architecture
  // limits that frame to 96 registers.
  // Reserve the last 8 + 2 registers for out0-out7 + the saved return pointer +
  // the saved ar.pfs. Frame lowering parks rp and ar.pfs in stacked locals just
  // above the ones the allocator used (see IA64FrameLowering::emitPrologue), so
  // the allocator must leave two registers below the outputs for them.
  Reserved.set(IA64::r118);
  Reserved.set(IA64::r119);
  Reserved.set(IA64::r120);
  Reserved.set(IA64::r121);
  Reserved.set(IA64::r122);
  Reserved.set(IA64::r123);
  Reserved.set(IA64::r124);
  Reserved.set(IA64::r125);
  Reserved.set(IA64::r126);
  Reserved.set(IA64::r127);
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
