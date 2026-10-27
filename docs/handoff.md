# Handoff notes (local branch `flos-match-and-lib-symbols`)

Status, workflow and gotchas from the first flib matching session. Nothing
on this branch has been pushed.

## Status

The full build matches (`build/R8AE01/main.dol: OK`).

| Unit | Status |
|---|---|
| `flib/flOS.cpp` | Matching |
| `flib/flRevolutionAlloc.cpp` | Matching |
| `flib/flSlice2.cpp` | Matching |
| `flib/ScriptLog.cpp` | Matching |
| `flib/scripting/Vec3Syscalls.cpp` | NonMatching: 23/24 functions; `Vec3CopySyscall` is the exception |
| `flib/scripting/core_syscalls.cpp` | Not started (104 functions) |
| `flib/flMemory.cpp` | Split only (needed for alignment), still asm |

Other flib code (the record table, `Demangle_FName`, the
`LookupFunctionInSymbolTable` area) sits in auto units between
0x802ED900 and 0x802F3130 and still needs splitting.

### Open problem: `Vec3CopySyscall`

The original copies z, y, x, then stores the three components to stack
slots 0x20, 0xC and 0x1C of a 0x30 frame (dead stores, apparently three
separate Vec-sized temporaries). These forms did not reproduce it:
- plain struct copy
- member-wise copy in either order
- a temp Vec, chained temps, or return-by-value
- a ctor temp (69%, the closest)
- a copy ctor or assignment operator
- by-value getters or setters
- a discarded `SyscallFloatResult`, pointer-cast and union versions
- a float→word→float round trip

## Compiler flags (game code, `config.json` "game")

- Base flags, plus `-func_align 16`, `-str reuse` and `-RTTI on`.
- Do **not** use `-str pool`. The original pads each string literal to 4
  bytes and addresses them from one base register, which `-str reuse`
  alone reproduces.
- `mw_version` Wii/1.1. Every Wii version produced identical code for these
  files, so this is not yet confirmed.

## Workflow

1. Read `build/R8AE01/asm/<unit>.s` and the data around the TU (strings,
   sdata, sbss, bss).
2. Add every section the TU owns to `splits.txt`, not only `.text`.
3. Add the unit to `objects.json` as `NonMatching`, then run
   `python configure.py`.
4. Build the object and compare it:
   ```
   ninja build/R8AE01/src/<path>.o
   objdiff-cli diff -p . -u main/<path-without-ext> -o build/diff.json --format json
   python tools/objdiff_summary.py build/diff.json --insns
   ```
5. Rename data symbols in `symbols.txt` to the names the compiler emits
   (`@NNN` for string literals, file-statics with `scope:local`). Check
   them with `build/tools/dtk.exe elf info <obj>`.
6. Mark the unit `Matching`, run a full `ninja`, and confirm the DOL
   hash. If it fails, `python tools/dol_diff.py orig/R8AE01/sys/main.dol
   build/R8AE01/main.dol` maps the differing bytes to addresses.

## Matching lessons

- **Alignment of the following code.** If the code after a TU is
  unsplit, it is 4-aligned, and the trailing 16-byte padding disappears
  (every later address shifts). Add the next TU as a split with
  `align:16`, as was done for `flMemory.cpp`.
- **Register allocation follows declaration order.** For locals, try
  every declaration order: a short permutation script finds them
  quickly. For `register` variables in inline asm, the compiler also
  schedules the asm instructions, so the order of the asm template
  matters too (see `flVecDot`).
- **Inline helpers matter.** The shared output code in ScriptLog only
  matched once the file write became its own inline function with the
  NULL check inside it.
- **vtable position.** MWCC places the vtable pointer where the first
  virtual is declared. Declare data members first to get it after them
  (`flSlice2::BaseSlice`).
- **Unknown external signatures.** Functions in TUs not yet decompiled
  are declared `extern "C"` under their current symbol names, with a
  TODO, so no parameter types are guessed into mangled names.

## Tooling gotchas (Windows/PowerShell)

- `Set-Content -Encoding utf8` writes a BOM, which makes `configure.py`
  fail silently on the JSON configs. Write files with a BOM-less
  `UTF8Encoding($false)`.
- `Copy-Item` keeps the source file's modification time, so ninja may not
  rebuild after a variant is copied over. Touch the file afterwards.
- The compiler fails with "Operating system error" on very long paths.
  Put scratch test files under `build/`.
- Git is at `C:\Program Files\Git\cmd`. Add it to `PATH` in new shells.
- objdiff is at `C:\Users\antho\Tools\objdiff\` (the GUI and `objdiff-cli`).
