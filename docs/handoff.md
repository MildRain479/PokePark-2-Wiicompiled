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
| `flib/FName.cpp` | Matching (`Demangle_FName`, `FName_encoding`, two empty stubs) |
| `flib/scripting/Vec3Syscalls.cpp` | NonMatching: 23/24 functions; `Vec3CopySyscall` is the exception |
| `flib/scripting/core_syscalls.cpp` | NonMatching: 98/104 functions; all data sections match |
| `flib/flMemory.cpp` | NonMatching: 18/21 functions; all data sections match |

`flib/CodepointConvert.cpp` (0x802EDA20-0x802EDC50) is split but has no
source yet. Other flib code (the record table, the
`LookupFunctionInSymbolTable` area) sits in auto units up to 0x802F3130
and still needs splitting.

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
- a member-wise-copying temporary per component, in either order, or as
  named temps. These load all three components each time; the original
  loads each component only once. The slots line up with three Vec
  temporaries at 0x8/0x14/0x20, each keeping only the component read
  from it, so whatever built them stored a single field. Only contrived
  code ("write one field of a throwaway Vec, read it back") seems likely
  to match, and that would be a fake match.

### Open problems: `core_syscalls.cpp`

- **`RecordTableEntryFill*ArrayPropSyscall` (4 functions, ~94%).** These are
  loops that the compiler unrolls by 8. The best form so far keeps
  `int n = count - 2;` in a local. What remains is register assignment
  inside the unrolled body: the original loads the field offset before the
  data pointer, and puts the loop index in r4 and the argument pointer in
  r6. Changing the address expression or the declaration order did not
  help.
- **`stack_vprintf` (97.8%).** Two differences remain:
  - Scheduling in `%s` and `%f`: the original advances `args` before
    setting r6.
  - Register choice in `%x`.

  The frame size is right once `spec` is `char[16]`, because the frame is
  rounded to 16 bytes and 20 bytes overflows it.
- **`stack_vprintf_d_specifier` (91.8%).** The left-aligned padding loop and
  the register reuse differ. Writing the negative flag before the
  terminator store got it from 83% to 92%.

### Open problems: `flMemory.cpp`

- **`flMemory::Allocate` (99.0%).** Only register assignment differs: the
  original uses mask→r7, minSize→r8 and chunkSize→r5. The best
  declaration order so far is `mask, chunk, minSize, chunkSize`, declared
  up front. The loop temporaries (rest, after, remainder, data, aligned)
  are now declared first, in the order rest, after, remainder, aligned,
  data (98.4% to 99.0%); re-permuting the four main locals on top found
  nothing better.
- **`flMemoryManager::free` (99.4%).** The first header load (the chunk
  passed to `Free`) should be in r28 and the guard pointer in r31; mine are
  swapped. Dropping the guard local or reordering declarations did not
  help.
- **`__sinit_\flMemory_cpp` (88%).** The original stores `mInitialized`
  through the `this` register (`stw r0, 0(r31)`). Mine stores through
  `gMemoryManager@l(r30)` and keeps r30 alive. Changing the constructor
  form (`this->`, a helper, an out-of-class inline) changed nothing.

Lessons from this file:
- Game code is built with `-inline noauto`. Without it, the compiler inlines
  earlier non-`inline` functions, such as `flMutex::Init` into the manager.
- `flMutex`'s destructor checks `mInitialized` twice; the second check is an
  inlined base-class destructor (`flMutexBase`).
- The 4 zero bytes after `"HEAP::Alloc Size=%d"` come from a 0x18-byte
  `char` array (`sHeapAllocFormat`), not from a separate object.
- Assigning inside the condition (`if ((a->next = b->next) != NULL)
  a->next->prev = a;`) reproduces the original's re-load after the store.

Lessons from `FName.cpp`:
- A loop tested at the top, with its increments in a separate block
  before the test, came only from a `goto`. Every `for`/`while` form
  compiled to a rotated loop (81-86%).
- Locals were declared up front in the order `value, j, full, i`. A
  4-way permutation search found it.

Experiment scripts used `score()` from a small helper: write a variant,
build the object, run objdiff-cli, and read `match_percent`. Recreate it
when needed. It is not in the repo. Once, a hand-written copy of a
variant scored 69% while the script's copy scored 98%, and the cause was
never found. Before trusting a score, rebuild from the exact file and
delete the `.o` first.

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
