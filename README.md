PokePark 2: Wonders Beyond, recompiled
=============

A native PC (Windows) and Android version of PokePark 2: Wonders Beyond,
made by statically recompiling the Wii game's PowerPC code into C.

This repository does **not** contain any game assets or code. An existing
copy of the game is required.

Supported version:

- `S2LE01`: Rev 0 (USA)

Improvements over the Wii version
=============

**Controls**

- Keyboard and mouse, or an Xbox-style controller, with every button
  rebindable (two keys and two controller buttons for each). On Android,
  a controller or touch controls drawn over the game.
- The mouse or right stick turns the camera (kept inside walls and above
  the ground); left click dashes and right click uses Thunderbolt.
- Pikachu moves in any direction with the left stick.
- The Wii Remote's shaking and tilting are on keys: Q and E by turns to
  shake, and the movement keys or stick to tilt in the attractions and
  minigames.
- Each attraction plays without a Wii Remote: Cake Contraption is aimed
  with the mouse or right stick, and in Dance Inferno each move has its
  own key, drawn on its icon (W A S D, E and Space; the d-pad, RB and A).
- Button prompts throughout show the key or button you've bound: in the
  menus' button bar, the bubble over Pikachu's head and the text.
- Instructions are rewritten for the PC's and phone's controls: the
  attractions' How to Play pages, the partners' moves, the moves taught in
  the field and the portals.

**Graphics**

- Vulkan or OpenGL, at up to 6x the original resolution, with
  anti-aliasing (up to 8x MSAA) and anisotropic texture filtering.
- Up to 240 fps: 30, 60, 120, 144, 165 or 240 fps, with frames drawn
  in between the game's. The game itself runs at its own speed.
- Widescreen at full resolution: 16:9, 16:10 and 21:9 show more of the
  world, with the HUD kept where it belongs; 4:3 is the game's own mode.
- Field of view from 80% to 150% of the original.
- The movies can be shown as they were, sharpened or AI upscaled.
- Steady frame pacing, and the game no longer settles at 15 fps.

**Gameplay options**

- Text speed, up to instant.
- Modifiers: berries found, shop prices, damage dealt and taken, and how
  fast Pokémon run in chases.
- Autosave (off by default): saves to the slot you loaded whenever you
  make a friend or enter another area.
- Rumble strength, on the controller.

**Convenience**

- Options in the PokePark Pad's pause menu and on the save file screen,
  including a way to close the game.
- Starts from your disc image (ISO, RVZ, WBFS or the like): a set-up
  screen asks for it the first time.
- A shorter boot: the wrist strap screen is cut short.
- An update check: Options tells you when a newer release is out.
- Android handhelds with two screens: the friends list and the map on
  the second screen, and the minimap can move to the top right while the
  touch controls are shown.

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
