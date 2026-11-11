PokePark 2: Wonders Beyond, recompiled
=============

A native PC (Windows) and Android version of PokePark 2: Wonders Beyond,
made by statically recompiling the Wii game's PowerPC code into C.

This repository does **not** contain any game assets or code. An existing
copy of the game is required.

Supported version:

- `S2LE01`: Rev 0 (USA)

Building
========

- Extract the game's disc into `orig/S2LE01` (with
  [decomp-toolkit](https://github.com/encounter/decomp-toolkit): `dtk disc
  extract`), so that `orig/S2LE01/sys/main.dol` exists.
- Then:

```
python tools/recomp/recomp.py               # main.dol -> build/recomp_S2LE01/gen/*.c
python tools/recomp/build.py --run test     # compile and run the tests
python tools/recomp/build.py --run boot     # ...or boot the game
python tools/recomp/build.py --android      # the Android app
```

See [recomp/README.md](recomp/README.md) for how it works, its settings and
the Android build.
