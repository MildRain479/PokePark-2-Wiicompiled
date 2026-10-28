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
4. It loads its data through its own DVD library.
5. It draws the Wii Strap safety screen, then the "Loading..." screen
   with an animated 3D Pikachu, in a window through OpenGL.

It then stays on "Loading...": the sound manager waits on nw4r's sound
system, which only advances on AX audio-frame callbacks. Those need the
AX/DSP replacement (see "Not done yet").

The game's own `OSReport` output, including the SDK banners and the
heap sizes, is printed to stderr.

When the watchdog stops a run, it prints:
- the last 200 functions entered;
- a backtrace of the running thread;
- where every other thread is waiting.

Diagnostics:
- `RECOMP_HLE_LOG=1` logs device requests, disc reads and large
  allocations; `2` adds every thread switch with a backtrace; `3` adds a
  backtrace for each large allocation.
- `RECOMP_WATCH=<address>[,<length>]` stops on the first write to a guest
  memory range, with a trace and backtrace.
- `RECOMP_GX_LOG=1` logs every presented frame with draw counts.
- `RECOMP_GX_DEBUG_FRAME=<n>` prints each draw of frame n: viewport, TEV
  stages and the first vertices.
- `RECOMP_GX_DUMP=<dir>` writes every 30th presented frame as a BMP;
  `RECOMP_GX_DUMP_EVERY=<n>` changes the interval.
- `RECOMP_GX_HIDDEN=1` keeps the window hidden.
- `RECOMP_GX_SCALE=<n>` sets the internal resolution multiplier
  (default 2).

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
| `hle_gx.c` | `GXDrawDone` decodes the pending FIFO and returns. |
| `hle_os.c` | `OSReport`/`OSPanic` print to stderr; failed `MEMAllocFromAllocator` calls are logged. |

**Memory differences from a Wii:**
- **MEM2:** the game also gets the top of MEM2 that IOS normally keeps
  (`0x933E0000` up). The IPC buffer moves to the very top.
  - Without this, the game's 21 MB `ParkPrepare` heap doesn't fit, on
    the sizes from its own tables.
  - On hardware it must fit, so some earlier allocation probably differs.
    That is still to be found.
- **Locked-cache DMA:** writing `DMAL` completes at once. nw4r uses it to
  move matrices out of the locked cache.

## Graphics (`recomp/gpu`)

The GPU is replaced at its command stream, so the game's GX and nw4r code
runs unchanged.

- **Capture.** Stores to the write-gather pipe (`0xCC008000`, the whole
  32-byte window) go to a FIFO instead of memory. Each command is decoded
  as soon as it is complete, the way the GPU trails the CPU.
  - This matters because indexed matrix loads, vertex arrays and textures
    are read from memory that the game soon reuses.
- **Decoding** (`gx_fifo.c`):
  - CP (vertex formats and arrays), XF (matrices, projection, viewport,
    color channels, texture coordinate generation including
    post-transform) and BP (TEV, textures, palettes, blending, depth,
    scissor, EFB copies) registers.
  - Display lists.
  - All seven primitive types are decoded. Quads, triangles, strips and
    fans are drawn; lines and points are skipped.
  - Vertices are transformed on the CPU.
  - The pixel-engine draw-done and token commands raise the GX
    interrupts. Each token is delivered with its own value.
- **Textures** (`gx_texture.c`): every GX format (I4, I8, IA4, IA8,
  RGB565, RGB5A3, RGBA8, C4, C8, C14X2, CMPR) and the palette formats,
  cached by address and content hash.
- **Backend** (`gl_backend.c`, OpenGL 3.3):
  - The EFB is an offscreen framebuffer at 2x resolution.
  - TEV runs in one uber-shader driven by uniforms: up to 16 stages with
    every input, bias, scale, clamp and compare mode, konst colors, swap
    tables and alpha test.
  - EFB-to-texture copies become textures.
  - Copies to the XFB are presented in the window.

Not implemented yet:
- lighting (lit channels use the material color);
- indirect texturing, fog and logic ops;
- z-textures;
- lines and points;
- copy pixel formats (copies keep RGBA);
- a Vulkan backend.

## Not done yet

- **Audio, needed to get past loading.** AX must run its 5 ms frame
  callbacks, at first silently and then mixed on the host, instead of on
  the DSP.
- **Graphics features.** See the list above.
- **Input.** WPAD/KPAD need a host replacement that feeds controllers in
  as Wii Remote and Nunchuk data.
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
