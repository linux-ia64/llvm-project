# LLVM with IA-64 (Itanium) Backend — `ia64-restoration`

This is a fork of LLVM/Clang that restores the IA-64 (Intel Itanium) backend,
which was removed after LLVM 2.6 (~2009). The goal is a modern, working backend
that compiles real C programs to IA-64 assembly using the current LLVM
infrastructure. It now cross-compiles and runs real software on IA-64 hardware —
most substantially **Lua 5.5.0**: its REPL, the `luac` bytecode compiler, and 21
files of the official test suite.

*Disclaimer: The majority of both code and text in the repository was generated
by Claude models (Opus 4.8, Sonnet 4.6).*

## Getting the source code

You can clone this repository, or, if you want to save disk space, you can
download it in .tar.gz format from GitHub.

The tarball is under 300MB. Extract the necessary parts with the following
command:

```sh
tar --exclude=llvm-project-ia64-restoration/{llvm,clang,libc}/test \
    -xvzf ia64-restoration.tar.gz \
    llvm-project-ia64-restoration/{cmake,llvm,clang,libc,third-party/siphash}
```

This will extract to around 1.7GB of files.

## Building

Requires CMake, Ninja, and a C++17 compiler. The IA-64 target is experimental.

For testing and development of the IA-64 backend, the following command is
recommended:

```sh
mkdir build-llvm && cd build-llvm
cmake ../llvm-project-ia64-restoration/llvm -GNinja \
    -DLLVM_TARGETS_TO_BUILD= \
    -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=IA64 \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_SHARED_LIBS=On \
    -DLLVM_ENABLE_PROJECTS=clang \
    -DLLVM_INCLUDE_TESTS=OFF \
    -DCLANG_INCLUDE_TESTS=OFF \
    -DLLVM_INCLUDE_BENCHMARKS=OFF
ninja llc clang
```

As the toolchain is still unstable, we recommend building with Debug, but you
can also build Release, especially if you are short on disk space.

Optionally, you can enable tests by omitting the last three lines, or build
all LLVM/Clang tools by omitting the "llc clang" part of the ninja command.

Verify the target is registered:

```sh
./bin/llc --version   # should list "ia64" under targets
```

## Using the Backend

### With `llc` (IR → assembly)

```sh
./bin/llc -march=ia64 source.ll   # default output name is source.s
```

### With Clang (C → assembly → object → executable)

The backend is asm-only: there is no integrated assembler, so Clang shells out
to the external GNU `ia64-epic-linux-gnu` binutils. These must be in `PATH`.
(Substitute your own toolchain target triple if needed.)

```sh
export PATH=/path/to/ia64-epic-linux-gnu-binutils/bin:$PATH

clang --target=ia64-epic-linux-gnu \
    --sysroot=$EPIC_ROOT \
    -B$EPIC_GCC_ROOT \
    -L$EPIC_GCC_ROOT \
    funtest-stageD.c -O2 -o $EPIC_ROOT/tmp/funtest-stageD
```

Where:
- `$EPIC_ROOT` — sysroot for the IA-64 Linux system (contains `usr/lib`,
  `usr/include`, etc.)
- `$EPIC_GCC_ROOT` — GCC cross-toolchain root (contains `crt*.o`, `libgcc`,
  and the cross GCC installation that `GCCInstallationDetector` picks up)

For a static executable, add `-static`. The default builds a PIE/dynamic
executable linked against the shared `libc`, which requires the IA-64 dynamic
linker at `/lib/ld-linux-ia64.so.2`.

## What Works

The backend cross-compiles and runs real programs on IA-64 hardware — the
broadest being **Lua 5.5.0** (REPL, `luac`, and 21 files of the official test
suite). `-O0` and `-O2` reach the same point. Verified features:

**Integer arithmetic**
- Register-register: `add`, `sub`, `and`, `or`, `xor`, `shl`, `shr` (logical
  and arithmetic)
- Sign/zero-extension: `sxt1/2/4`, `zxt1/2/4`, and sign-extending narrow loads
- Multiplication via the FP `xma.l` unit; wide multiply (`mulhu`/`mulhs`) via
  `setf.sig` → `xma.hu`/`xma.h` → `getf.sig`
- Constants of all widths: 14-bit (`adds`), 22-bit (`addl`), 64-bit (`movl`)
- Division and remainder (`udiv`/`sdiv`/`urem`/`srem`) — lowered to **libgcc**
  libcalls; divide-by-constant strength-reduced to shift + multiply

**Floating point (`f32`/`f64`, IEEE double)**
- `fadd`/`fsub`/`fmpy`/`fma`/`fms`/`fnma` carrying the `.d` double-precision
  completer (an 82-bit FP register otherwise keeps extended precision);
  `fneg`/`fabs`/`fnegabs`
- FP comparisons (`fcmp`) feeding predicates/branches
- `int`↔`fp` conversions, `f32`↔`f64`; FP division and `sqrt` via libcall
- FP arguments/returns (`F8`–`F15` + GR shadow), including FP varargs

**Predicates / booleans (`i1`)**
- Predicate register class; `i1` `and`/`or`/`xor`/`==`/`!=`, `select`,
  `sext`/`zext`/`anyext`, `i1` memory load/store, PR↔GR copies

**Control flow**
- Unconditional and conditional branches; comparisons → predicates → branches
- Loops (`phi` / `alloca`)
- `select` / ternary (integer, FP, and predicate)
- Computed `goto` (`brind` through `b6`) and `switch` jump tables (absolute,
  `BR_JT`)
- Recursive and cross-function calls (non-leaf ABI: `alloc` with correct output
  count, `br.call`, `rp`/`gp`/`sp` save-restore)
- Indirect / function-pointer calls through the `{entry, gp}` function
  descriptor (`@fptr`, `br.call b6`)
- `setjmp`/`longjmp` — `gp`/`sp`/`rp` preserved across the `longjmp` re-entry
  (parked in callee-saved `r4`/`r6`/`r7`, which the `jmpbuf` restores)

**Memory, globals, external calls**
- 8/16/32/64-bit loads and stores; `alloca`; heap via `malloc`; struct layout
  and field access through pointers
- Globals via GOT-indirect (`addl r = @ltoff(sym), gp ;; ld8 r = [r]`); function
  addresses via `@fptr` descriptors
- Direct *and* indirect calls; statically linked (invariant `gp`) and
  dynamically linked via the import stub (`gp` saved/restored around the call)

**Varargs**
- Callee-side `va_start`/`va_arg` and caller-side passing; FP varargs routed
  through the GR registers per psABI 8.5.4; **more than eight** arguments
  (register-home + stack laid out as one contiguous `va_list` image)

**Clang frontend**
- `clang --target=ia64-epic-linux-gnu` parses and compiles C with the correct
  LP64 / 80-bit-`long double` type model and IA-64 SysV psABI data layout
- Full pipeline: `cc1 -S` → external `ia64-epic-linux-gnu-as -x` (explicit
  stop-bit mode) → `ia64-epic-linux-gnu-ld -m elf64_ia64`
- Source-level DWARF line tables (`.file`/`.loc`), so `gdb` maps PCs to C source

## What Doesn't Work Yet

**ABI / codegen gaps**
- **Tail-call optimisation** — calls are correct but never tail-called
  (`IsTailCall = false`)
- **Inline assembly** operand constraints (no `getRegForInlineAsmConstraint`)
- `int`→`f64` rounding for integers wider than 53 significant bits needs a
  trailing `fnorm.d` (minor; not yet exercised by a test)
- A native `frcpa` + Newton-Raphson software divide (the libgcc libcall is used
  instead) — an optimisation, not a correctness gap

**Missing tooling layers (asm-output only, by design)**
- Object-file emission (`-filetype=obj`) — no MC encoder, `AsmBackend`, or
  `ELFObjectWriter`; relies on GNU `as` to produce objects
- AsmParser (no `.s` → object without external `as`), Disassembler, integrated
  assembler

**Other deferred items**
- DWARF/IA-64 unwind tables (`.cfi*` / `.IA_64.unwind`) and C++ exceptions —
  without them `gdb` cannot reliably unwind past the faulting frame
- Atomics (especially > 64-bit); short-branch relaxation; `-mcpu` / feature
  tuning

**Environment caveats (not backend limitations)**
- The IA-64 runtime this was tested on flushes **subnormals to zero**, and
  Intel's IA-64 `glibc`/`libm` has loose transcendental error bounds. Some
  floating-point conformance tests therefore fail in the *library/runtime*, not
  in generated code — confirmed by the same source failing with system-GCC-built
  binaries and passing on x86. Triage FP failures by comparing against GCC-IA64
  and x86 before suspecting codegen.

## Repository Layout

The IA-64 backend lives in the standard LLVM target directory:

```
llvm/lib/Target/IA64/
├── MCTargetDesc/          MC layer: MCAsmInfo, InstPrinter, MCTargetDesc
├── TargetInfo/            Target singleton registration
├── IA64*.td               TableGen: registers, instructions, calling convention
├── IA64TargetMachine.*    Target machine + pass config
├── IA64Subtarget.*        Subtarget (owns InstrInfo, FrameLowering, TLInfo)
├── IA64ISelLowering.*     IR → DAG lowering (LowerFormalArguments, LowerCall, …)
├── IA64ISelDAGToDAG.cpp   DAG → MachineInstr selection
├── IA64InstrInfo.*        Instruction info (copyPhysReg, load/store slots, …)
├── IA64RegisterInfo.*     Register info + reserved regs
├── IA64FrameLowering.*    Prologue/epilogue (alloc, sp, ar.pfs)
├── IA64AsmPrinter.cpp     MachineInstr → MCInst → streamer
├── IA64MCInstLower.*      MachineInstr → MCInst lowering
└── IA64Bundling.cpp       Stop-bit insertion pass
```

The Clang pieces are in:

```
clang/lib/Basic/Targets/IA64.{h,cpp}   TargetInfo (type model, predefines)
clang/lib/Driver/ToolChains/           Linux.cpp, Gnu.cpp, CommonArgs.cpp
                                       (integrated-as off, elf64_ia64 emulation,
                                        /lib/ld-linux-ia64.so.2, lib/ not lib64/)
```

The reference pre-removal backend (LLVM 2.6) is at commit `cdd405d80477` and
builds on modern systems with some light patching.
