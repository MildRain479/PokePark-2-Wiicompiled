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
python tools/recomp/build.py --release # optimized across files (~70 s, a few % faster)
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

The THP movies (`files/Thp`, 667 MB) are needed from the title screen on.

Save data (the console's NAND) lives in `orig/R8AE01/nand`, also
git-ignored, and is created on first run.

**Where it gets to:**
1. OS init: memory, caches, interrupts, alarms, threads.
2. IOS: STM, ES and FS requests are answered.
3. The game runs six threads.
4. It loads its data through its own DVD library.
5. It draws in a window through OpenGL. The wrist strap safety screen
   is cut short and "Hold Wii Remote Horizontally" shown for its usual
   few seconds (see `hle_game.c`), so the title comes up about 7 s after
   start instead of about 28 s.
6. It creates its save files and copies the Pokémon resources into a
   cache on the NAND.
7. It reaches the title screen, with its THP movie background.
8. A Wii Remote held sideways connects about a second in, driven by the
   keyboard or an Xbox-style controller. Pressing 2 leads to the save
   slots, picture choice and the following menus.

Sound plays through waveOut (`recomp/boot/audio_win32.c`, about 85 ms
buffered): the title movie's soundtrack, menu music and sound effects.
There is no reverb or other effects yet, and the Wii Remote speaker is
silent.

The title, menus and the movie intro run at a steady 60 fps (the window
title shows the frame rate and the slowest frame of the last second).
A Wii's NTSC video runs at 59.94 Hz; here the refresh and the game's
frame timing run at 60 Hz by default, to match PC displays
(`RECOMP_VI_HZ=59.94` restores the original). The console is set up
for a 16:9 TV by default, so the game draws its widescreen pictures and
layouts. It squeezes them into the EFB's 640 columns for a TV to
stretch out; the EFB is rendered 4/3 as wide as it is scaled tall, so
the stretched picture is at full resolution. Frames are shown at the
shape they have on a TV, with black bars filling the rest of the
window. THP movie frames are
decoded natively (`hle_thp.c`). EFB copies are converted to their copy
format, and the EFB depth is stored reversed in a float buffer, for the
precision GX scenes need near the far plane. 3D lighting is implemented
but not yet checked in gameplay.

**Controls** (`recomp/boot/input_win32.c`; remote 0, held sideways), by
default:

| Wii Remote | In the park | Keyboard and mouse | Controller |
|---|---|---|---|
| d-pad | move | WASD / arrows | d-pad / left stick |
| 2 | jump, talk | Space / X | A |
| 1 | dash | left click / Z | X |
| A | Thunderbolt | right click / Enter | B |
| B | recenter the camera | V / Left Ctrl | Y / right trigger |
| (none) | turn the camera | mouse | |
| + / - | | P / M | Start / Back |
| HOME | | Home / H | right stick click |
| shake | Iron Tail | Q / Left Shift | LB / RB |

The keyboard counts only while the game window has focus. While it has
focus the mouse is captured for the camera: Esc lets it go, and a click
in the window captures it again (that click isn't a dash). The mouse
turns the player camera (moving it down looks down on Pikachu);
cutscene and dialogue cameras are the game's. Rumble goes to the controller.

**Settings** (`recomp/boot/menu_win32.c`): Backspace, or Back+Start on a
controller, opens a settings menu over the game, which keeps running but
gets no input. It has:
- fullscreen (borderless, on the window's monitor; Alt+Enter also
  toggles it). In 16:9 the picture is shown at 16:9, as the game's
  cameras project it, so it fills a 16:9 screen;
- the internal resolution: fit the window (the multiple of the 456-line
  picture nearest the window's height), or 1x to 6x;
- anti-aliasing: off, or 2x, 4x or 8x MSAA;
- frame rate: 30 fps (the game's own in the park), 60 fps, which has
  the game draw after every update, or 120, 144, 165 or 240 fps, which
  add interpolated frames between the game's (see Graphics). The updates
  (movement, physics, animation) run at 60 a second whatever the
  setting, so nothing speeds up. No more frames are drawn than the
  window's monitor refreshes (`RECOMP_IGNORE_REFRESH=1` draws them
  anyway, for measuring);
- field of view: 80% to 150% of the game's, for the park's cameras
  (close-ups and the 2D screens' models keep theirs); culling follows it;
- camera sensitivity and inverted tilt for the mouse;
- the bindings above, two keys or mouse buttons and two controller
  buttons for each Wii Remote button (Enter, then press the new one; a
  key or button bound elsewhere moves).

Up/Down choose, Left/Right change, Enter rebinds, Delete clears; on a
controller, the d-pad, A, X to clear and B to close. Changes apply at
once and are saved in `settings.ini` beside `boot.exe`
(`RECOMP_SETTINGS=<file>` uses another file). The menu is plain for
now, until there is a proper pause menu.

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
- `RECOMP_PEEK=<address>,<length>` dumps guest memory when the watchdog
  stops a run.
- `RECOMP_GX_LOG=1` logs every presented frame with draw counts.
- `RECOMP_GX_DEBUG_FRAME=<n>` prints each draw of frame n: viewport, TEV
  stages and the first vertices.
- `RECOMP_GX_DUMP=<dir>` writes every 30th presented frame as a BMP;
  `RECOMP_GX_DUMP_EVERY=<n>` changes the interval.
- `RECOMP_GX_HIDDEN=1` keeps the window hidden.
- `RECOMP_GX_SCALE=<n>` sets the internal resolution multiplier, over
  the settings. The game shows 456 lines, so the picture is 456n lines
  tall; at 3 in 16:9 it is 2560x1368, pixel for pixel across a 1440p
  display.
- `RECOMP_WIDESCREEN=0` sets the console up for a 4:3 TV instead of
  16:9.
- `RECOMP_PAD_SCRIPT="<seconds>:<buttons> ..."` presses buttons at set
  times, for unattended runs: `"10:2 10.3: 22:right 22.3:"` presses 2 at
  10 s and right at 22 s. Buttons: up down left right a b 1 2 plus minus
  home shake, joined with `+`; times count from the remote's first
  report.
- `RECOMP_AUDIO=0` turns sound off; `RECOMP_AUDIO_DUMP=<file.wav>` also
  records the mix.
- `RECOMP_CALLS=<n>` lists the n functions called most in the last 5 s
  before the watchdog stops a run (for finding what a stuck game is
  polling).
- `RECOMP_FPS=1` prints the frame rate, the slowest frame and how busy
  the game thread was every second, with OpenGL calls and time per
  entry point.
- `RECOMP_GX_SKIP=<first>-<last>` drops those draws (numbered from 0 in
  each frame), to find which draws make up something on screen;
  `RECOMP_GX_COPY_LOG=1` logs each new EFB copy target.
- `RECOMP_GX_DUMP_TEX=<address>` saves the texture at that address, as
  decoded, to `tex_<address>.bmp` the first time it is loaded.
- `RECOMP_WATCH_LOG=1`, with `RECOMP_WATCH`, logs every function that
  writes the watched range instead of stopping at the first.
- `RECOMP_DUMP_MEM=<file>` saves MEM1 and MEM2 when the watchdog stops a
  run.
- `RECOMP_SKIP_MOVIES=1` ends movies that don't loop after about a
  second (the 56 s opening), for reaching the park quickly in tests.
- `RECOMP_VI_HZ=<rate>` sets the video refresh and frame rate (default
  60; 59.94 is the Wii's).
- `RECOMP_THP=orig` decodes THP movies with the game's own decoder;
  `RECOMP_THP=check` runs both and reports how far they differ.
- `RECOMP_PROFILE=<seconds>` samples the game's host thread about once a
  millisecond from that point until the watchdog stops the run, then
  lists the host functions it was in most; recompiled functions show
  with their guest names. `ZwDelayExecution` is idle time.
- `RECOMP_GL_DEBUG=1` uses a debug GL context and prints the driver's
  messages about errors and misuse as each call makes them.
- `RECOMP_MINIDUMP=<file>` writes a minidump when the run ends on a
  host fault (`RECOMP_MINIDUMP_FULL=1`: with all memory), for a
  debugger. Host backtraces name the module and offset of each frame,
  as a driver without symbols shows only its nearest export.

**How the hardware is replaced** (`recomp/patches/`, all through
`hooks.txt`):

| File | Replaces |
|---|---|
| `hle_thread.c` | Thread switching. Each guest thread is a host fiber. `OSSaveContext` and `OSLoadContext` become fiber switches. Interrupts run on the interrupted thread and return to it. |
| `hle_irq.c` | Interrupt delivery: device completions, the decrementer (`OSAlarm`), and a 59.94 Hz VI retrace. These are taken when the game enables interrupts, and the idle loop sleeps until the next one. |
| `hle_ios.c` | IOS (`__ios_Ipc2`): devices answered on the host. There is no SD card and no Bluetooth adapter. |
| `hle_nand.c` | The NAND file system (`/dev/fs` and NAND file descriptors), stored in a host directory. It also provides what a US console's system menu leaves there: `SYSCONF` (English, stereo, Wii Remote settings) and `setting.txt` (region USA). `SCGetAspectRatio` reports a 16:9 TV unless `RECOMP_WIDESCREEN=0`. |
| `hle_dvd.c` | The disc. `/dev/di` reads are mapped to the extracted files through the FST. |
| `hle_exi.c` | EXI: no devices. |
| `hle_bt.c` | Bluetooth: reported ready, with no adapter behind it. |
| `hle_wpad.c` | The Wii Remote (WPAD). Remote 0, sideways with no extension, connects a second after the game sets its connect callback; a 200 Hz timer stands in for its reports, read from the host's controls (`host_input.h`). KPAD runs unchanged on top. Remotes 1-3 are absent; the speaker is accepted and silent. |
| `hle_audio.c` | Audio. The DSP start-up is skipped and the DSP takes mail at once. A timer stands in for the AI DMA interrupt, at the buffer length the game sets (3 ms). Each tick mixes the voices of the last command list, as the DSP would (DSP-ADPCM, PCM16 and PCM8 decoding, linear resampling, envelope and mixer volumes, the low-pass filter, loops and ends), sends the mix to the host (`host_audio.h`), hands back to AX through its resume callback, and runs AX's AI callback, which starts the next frame. Volume ramps, the aux (effects) buses and the remote speaker are not mixed. |
| `hle_gx.c` | `GXDrawDone` decodes the pending FIFO and returns. |
| `hle_os.c` | `OSReport`/`OSPanic` print to stderr; failed `MEMAllocFromAllocator` calls are logged. |
| `hle_font.c` | The Wii system font, which `OSInitFont` reads from the boot ROM: `ReadFont` builds the ANSI font from host glyphs (`host_font.h`; GDI's Segoe UI Semibold in `recomp/boot/font_win32.c`), in the ROM font's format, and the SDK expands it as usual. |
| `hle_camera.c` | Mouse camera: the mouse turns the player camera's (`CameraPlayer`) yaw and its easing target, and swings the eye the game rebuilds that yaw from while Pikachu moves (so turning works while running and steers the run), so the game's own follow, distance and wall collision carry the turn on; up and down tilt it as an offset on the pitch the game picks from the slope ahead, kept off while the game works that pitch out. B recenters as on the Wii and clears the tilt. |
| `hle_thp.c` | THP movie video: `THPVideoDecode` decoded natively (baseline JPEG without byte stuffing, written as I8 tiles). The game's decoder took ~12 ms a frame in recompiled code. The output matches it to within a few levels on under 0.5% of bytes (IDCT rounding). |
| `hle_game.c` | Not hardware: game changes. The game's four frame-step constants (1/59.94 s and 2/59.94 s) follow `RECOMP_VI_HZ`, chosen so the 30 Hz alarm's period is exactly twice the 60 Hz one (otherwise the two drift apart and the game can settle at 15 fps). The boot cuts the Strap (safety) screen short: it hands over to Hold (sideways) as soon as the park data has loaded, about 1 s in, instead of after its 21 s timer. Hold runs its course (about 4 s): skipping it sent the first new game of a session back to the title after the opening. `RECOMP_BOOT_SCREENS=1` keeps the Strap timer. |

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
  - Each vertex is decoded once, straight into the current batch, an
    attribute at a time across the draw; primitives become indices.
    Vertex formats and the draw state are worked out again only when
    their registers change (the park sends 22,000 draws a frame).
  - Vertices are transformed on the GPU, from the XF state, as the
    console's transform unit does.
  - The pixel-engine draw-done and token commands raise the GX
    interrupts. Each token is delivered with its own value.
- **Textures** (`gx_texture.c`): every GX format (I4, I8, IA4, IA8,
  RGB565, RGB5A3, RGBA8, C4, C8, C14X2, CMPR) and the palette formats,
  cached by address and content hash.
- **Backend** (`gl_backend.c`, OpenGL 3.3):
  - The EFB is an offscreen framebuffer at the settings' internal
    resolution, multisampled if anti-aliasing is on (resolved before each
    copy reads it).
  - TEV runs in one uber-shader driven by uniforms: up to 16 stages with
    every input, bias, scale, clamp and compare mode, konst colors, swap
    tables and alpha test.
  - EFB-to-texture copies become textures.
  - Copies to the XFB are presented in the window, without waiting for
    the display (the game paces itself; a display whose refresh stalled
    held the game at 7 fps).
  - Presenting goes through a DXGI swapchain of the backend's own (flip
    model, tearing allowed), the picture handed from GL in a shared D3D11
    texture (`WGL_NV_DX_interop2`), not through `SwapBuffers`. NVIDIA's
    driver presents a GL window through a DXGI swapchain it keeps itself,
    and that path crashed about once in 40 minutes of play, just after
    slow scene-change frames: on some rare path the driver calls
    `ID3D11DeviceContext4::Wait` with a fence it never made (a read of
    address 0 in `d3d11.dll`). The GL context lives on a hidden window
    of its own, so the driver never presents. Without the extension, or
    with `RECOMP_GL_PRESENT=swapbuffers`, `SwapBuffers` presents as
    before.
  - Each draw changes only the GL state that differs from the last.
  - Above 60 fps, the newest frame is recorded (its draws with their
    state and XF state, clears, EFB copies) and drawn again while the
    game waits, each draw's matrices (position, normal, texture,
    post-transform, projection, lights) blended from those of the same
    draw in the frame before. Draws are matched by their display lists
    and their place in them; a matrix that changed too much to be the
    same object moving is shown as it is. Shown frames run one game frame
    (16.7 ms) behind. The in-between frames are drawn only while the game
    waits, so at 240 fps their spacing varies (4-12 ms); a render thread
    would even it out.

Not implemented yet:
- lighting (lit channels use the material color);
- indirect texturing, fog and logic ops;
- z-textures;
- lines and points;
- copy pixel formats (copies keep RGBA);
- a Vulkan backend.

## Not done yet

- **Input extras.** No pointer (IR) and no Nunchuk; PokePark is played
  sideways, so neither is needed for the main game. Only Windows input
  so far. Analog movement would need a game-side change, since the game
  reads the d-pad.
- **Sound extras.** The aux buses that feed reverb and other effects
  (AXFX), volume ramps, polyphase resampling (linear is used for all
  voices), the biquad filter and the Wii Remote speaker. Only Windows
  output so far.
- **Graphics features.** See the list above.
- **`setjmp`/`longjmp`.** Not supported across recompiled frames.
- **FPU fidelity.**
  - Fused multiply-add is not fused.
  - `fres` and `frsqrte` return exact results instead of the Gekko's
    estimates.
  - FPSCR rounding modes and exception bits don't affect host arithmetic.
  - `Rc=1` on FP instructions and integer `OE` overflow are ignored.

  This matters for bit-exact physics and replays; none of the current
  tests showed a difference.
- **Performance.** Movies and menus hold 60 fps with frames on time
  even with other programs loading the machine; the park's opening runs
  at its 30 fps target with the game thread about 40% busy. Gameplay
  further in has not been measured yet. In general every register lives in memory (`c->r[n]`), and
  caching registers in locals per function is the usual next step.
  Gameplay has not been profiled yet.
