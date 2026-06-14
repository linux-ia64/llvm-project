//===-- IA64TargetStreamer.h - IA64 Target Streamer ------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This streamer emits the IA-64 unwind directives (.proc / .prologue /
// .save ar.pfs / .save rp / .fframe / .body / .restore sp / .endp) that GNU
// 'gas' assembles into the .IA_64.unwind / .IA_64.unwind_info sections. Those
// sections -- not DWARF .eh_frame -- are what gdb/libunwind read to walk an
// IA-64 stack, so emitting them is what makes a backtrace work. The asm printer
// drives these calls off the frame-setup/destroy flags on the prologue and
// epilogue instructions.
//
// Only the textual (asm) form is implemented: the IA-64 backend has no
// integrated assembler, so the object encoding is gas's job. The base class is
// a no-op so the null streamer (and any future object streamer) link cleanly.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64TARGETSTREAMER_H
#define LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64TARGETSTREAMER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCStreamer.h"

namespace llvm {

class formatted_raw_ostream;
class MCSymbol;

class IA64TargetStreamer : public MCTargetStreamer {
  virtual void anchor();

public:
  IA64TargetStreamer(MCStreamer &S);

  /// Open the unwind region for a procedure: ".proc <sym>".
  virtual void emitProc(const MCSymbol *Sym) {}
  /// Close it: ".endp <sym>".
  virtual void emitEndP(const MCSymbol *Sym) {}
  /// Begin the prologue region: ".prologue".
  virtual void emitPrologueDirective() {}
  /// Record where ar.pfs (the caller's frame marker) was saved:
  /// ".save ar.pfs, <reg>".
  virtual void emitSaveARPFS(StringRef Reg) {}
  /// Record the fixed memory-frame size in bytes: ".fframe <size>".
  virtual void emitFFrame(int64_t Size) {}
  /// Record where the return pointer (b0) was saved: ".save rp, <reg>".
  virtual void emitSaveRP(StringRef Reg) {}
  /// End the prologue, begin the body region: ".body".
  virtual void emitBody() {}
  /// Snapshot the current unwind state under a label: ".label_state <n>".
  virtual void emitLabelState(unsigned N) {}
  /// Restore a snapshotted unwind state: ".copy_state <n>". Emitted before each
  /// '.restore sp' in a function with several epilogues, so gas re-opens the
  /// region the previous '.restore' closed.
  virtual void emitCopyState(unsigned N) {}
  /// Mark the point where sp is restored to its on-entry value: ".restore sp".
  virtual void emitRestoreSP() {}
};

// Textual (.s) output for GNU gas.
class IA64TargetAsmStreamer : public IA64TargetStreamer {
  formatted_raw_ostream &OS;

public:
  IA64TargetAsmStreamer(MCStreamer &S, formatted_raw_ostream &OS);

  void emitProc(const MCSymbol *Sym) override;
  void emitEndP(const MCSymbol *Sym) override;
  void emitPrologueDirective() override;
  void emitSaveARPFS(StringRef Reg) override;
  void emitFFrame(int64_t Size) override;
  void emitSaveRP(StringRef Reg) override;
  void emitBody() override;
  void emitLabelState(unsigned N) override;
  void emitCopyState(unsigned N) override;
  void emitRestoreSP() override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64TARGETSTREAMER_H
