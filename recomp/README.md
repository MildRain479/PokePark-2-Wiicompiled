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

## Booting

```
python tools/recomp/build.py --run boot          # boots main.dol (10 s watchdog)
RECOMP_HLE_LOG=1 build/recomp/boot.exe orig/R8AE01/sys/main.dol 20
```

The boot harness (`recomp/boot/boot.c`) sets up low memory the way the
system menu and apploader leave it, then runs the game's own `__start`.

It needs the disc's files in `orig/R8AE01/disc` (git-ignored), extracted
from your copy with dtk:
- `sys/boot.bin`, `bi2.bin`, `apploader.img` and `fst.bin`;
- `tmd.bin`;
- the `files/` tree.

The THP movies (`files/Thp`, 667 MB) are optional for now.

**Where it gets to:**
1. OS init: memory, caches, interrupts, alarms, threads.
2. IOS: STM, ES and FS requests are answered.
3. The game runs six threads.
4. It loads its data through its own DVD library: the sound archive, the
   Home Button Menu, messages, the strap-screen resources and the first
   attraction.
5. It runs `MainLoop` every retrace, updating nw4r scenes and cameras.

Nothing is drawn or heard yet.

When the watchdog stops a run, it prints:
- the last 200 functions entered;
- a backtrace of the running thread;
- where every other thread is waiting.

`RECOMP_HLE_LOG=1` logs device requests and disc reads; `2` also logs
every thread switch with a backtrace.

**How the hardware is replaced** (`recomp/patches/`, all through
`hooks.txt`):

| File | Replaces |
|---|---|
| `hle_thread.c` | Thread switching. Each guest thread is a host fiber. `OSSaveContext` and `OSLoadContext` become fiber switches. Interrupts run on the interrupted thread and return to it. |
| `hle_irq.c` | Interrupt delivery: device completions, the decrementer (`OSAlarm`), and a 59.94 Hz VI retrace. These are taken when the game enables interrupts, and the idle loop sleeps until the next one. |
| `hle_ios.c` | IOS (`__ios_Ipc2`): devices answered on the host. There is no SD card and no Bluetooth adapter. NAND files don't exist yet. |
| `hle_dvd.c` | The disc. `/dev/di` reads are mapped to the extracted files through the FST. |
| `hle_exi.c` | EXI: no devices. |
| `hle_bt.c` | Bluetooth: reported ready, with no remotes. |
| `hle_audio.c` | The DSP/AI start-up handshakes are skipped, so there is no sound. |
| `hle_gx.c` | `GXDrawDone` returns at once, since there is no GPU. |

## Not done yet

- **Graphics.** GX commands go to the write-gather pipe at `0xCC008000`,
  which is plain memory, so they are dropped. The renderer needs to
  capture writes to that address and decode the command stream into
  Vulkan/OpenGL draws. This is the next big piece.
- **Input.** WPAD/KPAD need a host replacement that feeds controllers in
  as Wii Remote and Nunchuk data.
- **Audio.** AX must be mixed on the host instead of on the DSP.
- **Saves.** NAND file access (`/dev/fs`, ISFS) needs host files.
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
