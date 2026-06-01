# LLVM with IA-64 (Itanium) Backend — `ia64-restoration`

This is a fork of LLVM/Clang that restores the IA-64 (Intel Itanium) backend,
which was removed after LLVM 2.6 (~2009). The goal is a modern, working backend
that compiles real C programs to IA-64 assembly using the current LLVM
infrastructure.

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

The following features are implemented and verified on IA-64 hardware:

**Integer arithmetic**
- Register-register: `add`, `sub`, `and`, `or`, `xor`, `shl`, `shr` (logical
  and arithmetic)
- Sign/zero-extension: `sxt1/2/4`, `zxt1/2/4`
- Multiplication via the FP `xma.l` unit (IA-64 has no integer multiply
  on integer unit)
- Constants of all widths: 14-bit (`adds`), 22-bit (`addl`), 64-bit (`movl`)
- Division by a power-of-two constant (strength-reduced by the DAG combiner to
  a mask + shift; no hardware divider involved)
- Division by 10 and possibly other division by fixed constant, whatever can be
  folded by LLVM into shift and multiply

**Control flow**
- Unconditional branches
- Comparisons (`icmp eq/ne/lt/le/gt/ge` and unsigned variants) writing predicate
  registers, feeding conditional branches
- Loops (with `phi` nodes, or with `alloca`-based iteration variables)
- Recursive and cross-function calls (non-leaf ABI: `alloc` with correct output
  count, `br.call`, `rp`/`gp` save-restore)

**Memory**
- 8/16/32/64-bit loads and stores
- Stack-allocated variables (`alloca`)
- Heap allocation via `malloc` / external library calls
- Struct layout and field access through pointers

**Globals and external calls**
- Global constant strings and data — address materialised via
  `addl r = @ltoff(sym), gp ;; ld8 r = [r]` (GOT-indirect)
- Direct calls to external functions (`puts`, `malloc`, …) — both statically
  linked (invariant `gp`) and dynamically linked via the import stub (with `gp`
  saved/restored around the call for non-`dso_local` callees)

**Clang frontend**
- `clang --target=ia64-epic-linux-gnu` parses and compiles C with the correct
  LP64 / 80-bit-`long double` type model and IA-64 SysV psABI data layout
- Full pipeline: `cc1 -S` → external `ia64-epic-linux-gnu-as -x` (explicit
  stop-bit mode) → `ia64-epic-linux-gnu-ld -m elf64_ia64`

## What Doesn't Work Yet

**Codegen gaps**
- General integer division and remainder (`udiv`/`sdiv`/`urem`/`srem` between
  two registers) — requires the `frcpa` + Newton-Raphson software-divide
  sequence from the pre-removal backend, not yet ported
- Floating-point arithmetic (FP register class and `fadd`/`fsub`/`fmpy`
  instructions are defined but have no DAG patterns)
- Indirect / function-pointer calls — the entry+`gp` function-descriptor
  mechanism and `BRCALL_INDIRECT` through `b6`
- Varargs (`printf` etc.) — `puts` is used instead throughout the test suite
- Aggregate (struct/union) argument and return passing by value — struct layout
  is correct, but the psABI eightbyte/HFA classification in the CC tables is not
  implemented; the generic `DefaultABIInfo` handles scalars only (or at least
  only those were tested)
- Ternary operator (`select` LLVM instruction)

**Missing tooling layers**
- Object-file emission (`-filetype=obj`) — no MC instruction encoder,
  `AsmBackend`, or `ELFObjectWriter`; the backend is asm-output only and relies
  on GNU `as` to produce objects
- AsmParser (no `.s` → object without external `as`)
- Disassembler
- Integrated assembler support

**Other deferred items**
- Tail-call optimisation, short branches
- `switch` jump tables
- Inline assembly constraints
- TLS, atomics > 64 bits, C++ exceptions / unwind tables
- `-mcpu` / feature tuning

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
