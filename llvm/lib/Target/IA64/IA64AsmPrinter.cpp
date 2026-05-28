//===-- IA64AsmPrinter.cpp - Print out IA64 LLVM as assembly --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts the machine-dependent LLVM code
// to GNU 'gas'-compatible IA-64 assembly. Unlike the pre-removal backend, which
// hand-formatted each MachineInstr, this lowers each MachineInstr to an MCInst
// and lets the streamer + IA64InstPrinter emit the text.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64MCInstLower.h"
#include "MCTargetDesc/IA64InstPrinter.h"
#include "MCTargetDesc/IA64MCAsmInfo.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "MCTargetDesc/IA64TargetStreamer.h"
#include "TargetInfo/IA64TargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

namespace {
class IA64AsmPrinter : public AsmPrinter {
  // Per-function state for driving the IA-64 unwind directives (see
  // emitInstruction). Reset in emitFunctionBodyStart.
  bool EmittedBody = false;
  bool EmittedFFrame = false;
  // A framed function with more than one epilogue needs .label_state /
  // .copy_state around its '.restore sp's; otherwise gas rejects the second one.
  bool NeedCopyState = false;
  // Set while lowering a GlobalAlias's aliasee: an alias names the aliasee's
  // entry-point symbol directly (`A = B`), so suppress the @fptr descriptor
  // wrapping lowerConstant applies to functions stored in data. See
  // emitGlobalAlias / lowerConstant.
  bool InAliasLowering = false;

  IA64TargetStreamer &getTargetStreamer() {
    return static_cast<IA64TargetStreamer &>(*OutStreamer->getTargetStreamer());
  }

public:
  static char ID;

  explicit IA64AsmPrinter(TargetMachine &TM,
                          std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer), ID) {}

  StringRef getPassName() const override { return "IA64 Assembly Printer"; }

  void emitStartOfAsmFile(Module &M) override;
  void emitFunctionEntryLabel() override;
  void emitFunctionBodyStart() override;
  void emitFunctionBodyEnd() override;
  void emitInstruction(const MachineInstr *MI) override;
  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       const char *ExtraCode, raw_ostream &O) override;
  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                             const char *ExtraCode, raw_ostream &O) override;
  void emitGlobalAlias(const Module &M, const GlobalAlias &GA) override;
  const MCExpr *lowerConstant(const Constant *CV, const Constant *BaseCV,
                              uint64_t Offset) override;
};
} // end anonymous namespace

char IA64AsmPrinter::ID = 0;

void IA64AsmPrinter::emitStartOfAsmFile(Module & /*M*/) {
  // The IA-64 assembly preamble expected by GNU gas, matching the pre-removal
  // output. (lsb should be msb on HP-UX; we only support 64-bit.)
  OutStreamer->emitRawText(StringRef("\t.psr\tlsb"));
  OutStreamer->emitRawText(StringRef("\t.radix\tC"));
  OutStreamer->emitRawText(StringRef("\t.psr\tabi64"));
}

void IA64AsmPrinter::emitFunctionEntryLabel() {
  // Open the unwind region before the function label, the way gcc does. The
  // prologue/body directives are emitted per-instruction in emitInstruction;
  // .endp follows the body in emitFunctionBodyEnd.
  getTargetStreamer().emitProc(CurrentFnSym);
  AsmPrinter::emitFunctionEntryLabel();
}

void IA64AsmPrinter::emitFunctionBodyStart() {
  EmittedBody = false;
  EmittedFFrame = false;

  // A '.restore sp' closes the unwind region it sits in, so a framed function
  // with several return blocks needs .label_state/.copy_state to re-open it for
  // each one. Single-epilogue (or frameless) functions emit a bare '.restore'
  // (or none), matching gcc. getStackSize() != 0 is exactly the has-a-frame
  // (and therefore has-a-'.restore sp') condition.
  unsigned RetBlocks = 0;
  bool Framed = MF->getFrameInfo().getStackSize() != 0;
  if (Framed)
    for (const MachineBasicBlock &MBB : *MF)
      if (!MBB.empty() && MBB.back().getOpcode() == IA64::RET)
        ++RetBlocks;
  NeedCopyState = Framed && RetBlocks > 1;
}

void IA64AsmPrinter::emitFunctionBodyEnd() {
  getTargetStreamer().emitEndP(CurrentFnSym);
}

void IA64AsmPrinter::emitInstruction(const MachineInstr *MI) {
  IA64TargetStreamer &TS = getTargetStreamer();

  // Emit the IA-64 unwind directive that describes this prologue/epilogue
  // instruction, before the instruction itself, so gas associates the unwind
  // record with the right PC. The prologue (alloc, the rp save, the stack
  // adjust) is tagged FrameSetup by frame lowering and ISel; the stack restore
  // is tagged FrameDestroy. The first non-prologue instruction ends the
  // prologue region with .body.
  if (MI->getFlag(MachineInstr::FrameSetup)) {
    switch (MI->getOpcode()) {
    case IA64::ALLOC:
      // alloc copies the caller's ar.pfs into its destination register.
      TS.emitPrologueDirective();
      TS.emitSaveARPFS(
          IA64InstPrinter::getRegisterName(MI->getOperand(0).getReg().asMCReg()));
      break;
    case IA64::MOV:
      // The return-pointer save is 'mov rN = rp'; distinguish it from the
      // frame-pointer setup 'mov r5 = r12' by its source register.
      if (MI->getOperand(1).getReg() == IA64::rp)
        TS.emitSaveRP(IA64InstPrinter::getRegisterName(
            MI->getOperand(0).getReg().asMCReg()));
      break;
    case IA64::ADDIMM22:
    case IA64::ADD:
      // The stack-pointer adjustment writes r12; the .fframe value is the final
      // frame size frame lowering settled on.
      if (!EmittedFFrame && MI->getOperand(0).getReg() == IA64::r12) {
        TS.emitFFrame(MF->getFrameInfo().getStackSize());
        EmittedFFrame = true;
      }
      break;
    }
  } else if (!EmittedBody && !MI->isMetaInstruction() &&
             MI->getOpcode() != IA64::STOP) {
    // End the prologue region at the first real body instruction. Skip the
    // bundler's STOP (';;') pseudo: one can land between prologue instructions
    // (e.g. the forced stop after 'alloc'), and treating it as the body start
    // would push .fframe / a late .save past .body.
    TS.emitBody();
    EmittedBody = true;
    if (NeedCopyState)
      TS.emitLabelState(1);
  }

  if (MI->getFlag(MachineInstr::FrameDestroy) &&
      (MI->getOpcode() == IA64::ADDIMM22 || MI->getOpcode() == IA64::ADD) &&
      MI->getOperand(0).getReg() == IA64::r12) {
    if (NeedCopyState)
      TS.emitCopyState(1);
    TS.emitRestoreSP();
  }

  IA64MCInstLower Lower(OutContext, *this);
  MCInst TmpInst;
  Lower.Lower(MI, TmpInst);
  EmitToStreamer(*OutStreamer, TmpInst);
}

// Print an inline-asm operand referenced by a '$N' substitution. We handle the
// no-modifier register and immediate cases (covering the 'r'/'f' and immediate
// constraints); anything else defers to the generic AsmPrinter handler.
bool IA64AsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                     const char *ExtraCode, raw_ostream &O) {
  if (ExtraCode && ExtraCode[0])
    // We define no IA-64-specific modifiers; let the generic handler try.
    return AsmPrinter::PrintAsmOperand(MI, OpNo, ExtraCode, O);

  const MachineOperand &MO = MI->getOperand(OpNo);
  switch (MO.getType()) {
  case MachineOperand::MO_Register:
    O << IA64InstPrinter::getRegisterName(MO.getReg().asMCReg());
    return false;
  case MachineOperand::MO_Immediate:
    O << MO.getImm();
    return false;
  default:
    break;
  }
  return AsmPrinter::PrintAsmOperand(MI, OpNo, ExtraCode, O);
}

// An inline-asm memory operand ('m'): the address lives in a single register,
// dereferenced as '[rN]'.
bool IA64AsmPrinter::PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                                           const char *ExtraCode,
                                           raw_ostream &O) {
  if (ExtraCode && ExtraCode[0])
    return AsmPrinter::PrintAsmMemoryOperand(MI, OpNo, ExtraCode, O);

  const MachineOperand &MO = MI->getOperand(OpNo);
  if (!MO.isReg())
    return true;
  O << '[' << IA64InstPrinter::getRegisterName(MO.getReg().asMCReg()) << ']';
  return false;
}

// A GlobalAlias is just another name for the aliasee's symbol; on IA-64 a
// function alias must resolve to the aliasee's *entry point*, not its function
// descriptor. The generic AsmPrinter lowers the aliasee through lowerConstant()
// (which wraps functions in @fptr), so `A = @fptr(B)` would be emitted: that
// both mis-aliases A to the descriptor and makes GNU as abort (a symbol's value
// expression can't be an @fptr pseudo-fixup -- "Case value 64 unexpected" in
// resolve_symbol_value). Flag the alias context so lowerConstant emits the bare
// entry-point symbol, yielding the correct `A = B`.
void IA64AsmPrinter::emitGlobalAlias(const Module &M, const GlobalAlias &GA) {
  InAliasLowering = true;
  AsmPrinter::emitGlobalAlias(M, GA);
  InAliasLowering = false;
}

const MCExpr *IA64AsmPrinter::lowerConstant(const Constant *CV,
                                            const Constant *BaseCV,
                                            uint64_t Offset) {
  // A function pointer stored in data is the address of the function's
  // descriptor { entry, gp }, not its entry point: emit data8 @fptr(f). The
  // linker materializes the .opd descriptor; an indirect call dereferences it.
  // (Skipped under alias lowering, where the alias must equal the entry point.)
  if (const auto *F = dyn_cast<Function>(CV)) {
    const MCExpr *E = MCSymbolRefExpr::create(getSymbol(F), OutContext);
    if (InAliasLowering)
      return E;
    return MCSpecifierExpr::create(E, IA64::S_FPTR, OutContext);
  }
  return AsmPrinter::lowerConstant(CV, BaseCV, Offset);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeIA64AsmPrinter() {
  RegisterAsmPrinter<IA64AsmPrinter> X(getTheIA64Target());
}
