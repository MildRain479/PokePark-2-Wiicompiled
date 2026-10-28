# Static recompilation (proof of concept)

This translates the game's PowerPC code in `main.dol` into C that compiles
for the host (x86-64 today; any 64-bit target in principle). It is a
stepping stone toward the native port, separate from the decomp: it needs
no matching source, and decompiled functions can replace generated ones
over time.

Nothing here contains game data. The generated code is written to
`build/` (not committed), and the runtime loads `main.dol` from your own
copy at startup.

## Use

```
python tools/recomp/recomp.py          # main.dol -> build/recomp/gen/*.c (~4 s)
python tools/recomp/build.py --run     # compile (~25 s with MSVC) and run the tests
```

`recomp.py` needs `orig/R8AE01/sys/main.dol` and dtk's
`config/R8AE01/symbols.txt` (for function boundaries). `build.py` uses
MSVC on Windows and `cc`/`c++` elsewhere.

## What works

- **Every function translates.** All 14,407 functions, plus 30 extra entry
  points such as `_savegpr_25`: 766,002 instructions with none
  untranslated. That covers integer, branch, condition-register, FPU,
  paired-single and quantized load/store instructions. The output compiles
  cleanly.
- **The output is checked against references** by
  `recomp/tests/test_recomp.cpp`: 2.8 million checks, all passing.
  - `UnicodeToSJIS`, `EncodeCodepoint` and `Demangle_FName` are compared
    against the decompiled source in `src/flib/`, compiled natively. They
    agree exactly on every input tried (1.1M, 1.5M and 200K inputs).
  - The game's MSL C library (`strlen`, `strcmp`, `atoi`, `sprintf` with
    integer, string and `%f` formats) is compared against the host C
    library.
  - The game's math library (`sin`, `cos`, `tan`, `atan`, `atan2`, `pow`,
    `fmod`, `sqrt`) is within 1 ulp of the host's libm. That difference is
    expected rounding between fdlibm and the host library.
- **Hooks.** A function listed in `recomp/patches/hooks.txt` is
  recompiled as `f_<addr>_orig`, and the patch defines `f_<addr>`. Every
  direct and indirect call in the game then goes through the patch.
  `example_hook.c` wraps `strlen`, and the tests confirm that a call from
  inside the game's `sprintf` reaches it. Difficulty options, a
  stick-driven camera and other mods would use this mechanism, once the
  relevant game functions are identified.

## How it works

- `tools/recomp/translate.py` turns each instruction into C that works on
  a `ppc_t` register file (`recomp/runtime/ppc.h`).
- Branches inside a function become `goto`s. Direct calls are C calls.
  `bctr` uses a `switch` over jump-table targets, which are found by
  scanning data for pointers into the function. Other indirect calls go
  through `ppc_icall`, which binary-searches the generated address table.
- Guest memory is one reserved 1 GB block indexed by
  `address & 0x3FFFFFFF`. That folds the cached (0x8...) and uncached
  (0xC...) views together, and keeps MEM2 and the locked cache separate.
  Loads and stores byte-swap.

## Not done yet (what it takes to boot the game)

The game does not run yet. What's missing, roughly in the order the boot
path needs it:

- **Threads and exceptions.** `OSLoadContext` and `OSSwitchThread` resume
  threads with `rfi`, which a C call stack can't express. These need
  host threads or fibers behind hooks. Interrupts (VI retrace, DSP, DVD)
  need host-driven callbacks.
- **Hardware.** Writes to the `0xCC`/`0xCD` hardware registers land in
  plain memory. Each subsystem needs a high-level replacement at the SDK
  boundary:
  - GX (the graphics FIFO) → the Vulkan/OpenGL renderer;
  - DVD → reads from the disc image;
  - PAD/WPAD → host controllers;
  - AX/DSP → host audio;
  - NAND → save files.
- **`setjmp`/`longjmp`.** Not supported across recompiled frames.
- **FPU fidelity.**
  - Fused multiply-add is not fused.
  - `fres` and `frsqrte` return exact results instead of the Gekko's
    estimates.
  - FPSCR rounding modes and exception bits don't affect host arithmetic.
  - `Rc=1` on FP instructions and integer `OE` overflow are ignored.

  This matters for bit-exact physics and replays; none of the current
  tests showed a difference.
- **Performance** has not been measured yet. Every register lives in
  memory (`c->r[n]`). Caching registers in locals per function is the
  usual next step.
