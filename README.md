# PokePark 2 Wiicompiled

A native Windows and Android port of *PokéPark 2: Wonders Beyond*
(USA, S2LE01).
The game's PowerPC code is statically recompiled into C and runs natively,
with the Wii's hardware replaced underneath it. There is no emulator: the
game's own code runs, drawing with Vulkan or OpenGL at high resolutions and
frame rates, played with a keyboard and mouse, a controller, or a touch
screen.

**This repository contains no game data.** You need your own copy of the
game. The program reads your disc image the first time it runs.

> **Beta.** Please report problems in [Issues](../../issues). Say what
> you did and what happened, attach a screenshot if you can, and give your
> Windows version, graphics card and Renderer setting (Options > Display),
> or on Android your phone's model and Android version.

## Playing on Windows

1. Download the latest build from [Releases](../../releases) and unzip it.
2. Run `PokePark2.exe`.
3. The first time, choose your disc image (ISO, RVZ, WBFS, WIA, CISO and
   the like). It's checked against the original disc, then the game is set
   up in an `S2LE01` folder beside the program, and your saves are kept in
   `S2LE01\nand`.

Keep the `prompts` and `tools` folders beside `PokePark2.exe`.

**Requirements:** Windows 10 or 11 (64-bit) and a graphics card with
Vulkan 1.3 or OpenGL 3.3.

## Playing on Android

1. Download the `.apk` from [Releases](../../releases) on your phone and
   install it (allow installing apps from your browser or file manager
   when asked).
2. Open **PokePark 2**.
3. The first time, choose your disc image (RVZ, ISO, WBFS, WIA, CISO and
   the like) in the file picker. The game plays straight from it, so
   nothing is extracted; if you move the file, the app asks again.

Play with the touch controls drawn over the game (a stick on the left,
the buttons on the right, - and + at the top left, drag elsewhere to turn
the camera) or a controller. Settings are in Options, in the PokéPark Pad.

**Dual-screen handhelds.** On a handheld with a second built-in screen,
such as the AYN Thor, the second screen shows the PokéPark Pad's pages:
your friends list and the map. Options > Dual screen, shown only on these
handhelds, turns it on or off, swaps the screens, and keeps the minimap
on the game's screen too. The Retroid Dual Screen Add-On should work the
same way, but hasn't been tested.

**Requirements:** Android 11 or later on a 64-bit (arm64) phone or
handheld with Vulkan 1.3. Tested on the AYN Thor so far.

## Playing on Steam Deck

There's no Linux version, but the Windows one should run through Proton,
the Deck's built-in Windows compatibility layer. This hasn't been tested
yet, so please report how it goes.

1. In Desktop Mode, download the Windows build from
   [Releases](../../releases) and unzip it somewhere on the Deck, along
   with your disc image.
2. In Steam, choose **Games > Add a Non-Steam Game to My Library** and
   pick `PokePark2.exe`.
3. In that entry's **Properties > Compatibility**, tick **Force the use of
   a specific Steam Play compatibility tool** and choose the latest
   Proton.
4. Start it. The first time, choose your disc image, as on Windows.
5. In Options > Display, set **Aspect ratio** to **16:10** (the Deck's
   screen) and turn on **Fullscreen**.

The Deck's controls work as an Xbox controller. If something doesn't
work, open an issue saying so, with your Proton version.

## Bringing over a save from a Wii or Dolphin

The game keeps its saves where a Wii does, in the same files, so a save
from your Wii or from Dolphin should carry on where you left off. This
hasn't been tested with PokéPark 2 yet.

**From a Wii.** A Wii copies saves to an SD card encrypted, so Dolphin
(free, [dolphin-emu.org](https://dolphin-emu.org)) is used to unpack it:

1. On the Wii, go to **Wii Options > Data Management > Save Data > Wii**,
   choose PokéPark 2 and **Copy** it to an SD card. It's saved as
   `private\wii\title\S2LE\data.bin` on the card.
2. In Dolphin, choose **Tools > Import Wii Save...** and pick that
   `data.bin`.
3. Carry on as from Dolphin, below.

**From Dolphin.**

1. In Dolphin, choose **File > Open User Folder**. The save is in
   `Wii\title\00010000\53324c45\data` there.
2. Close the game if it's running, and keep a copy of its own save folder
   in case you want it back:
   - **Windows:** `S2LE01\nand\title\00010000\53324c45\data`, beside
     `PokePark2.exe`.
   - **Android:** `Android/data/io.github.pokeparkrecomp2/files/S2LE01/nand/title/00010000/53324c45/data`
     on the phone's storage. The phone's own Files app hides this
     folder, but other file managers, such as CX File Explorer, can open
     it (allow access to the folder when asked). Or connect the phone to
     a computer with a USB cable and choose **File transfer**, and it
     shows up there.

   Start the game once first if that folder isn't there yet.
3. Copy everything in Dolphin's `data` folder into the game's `data`
   folder, replacing what's there.
4. Start the game; your save files are on the save files screen.

Saves go the other way too: copy the game's `data` folder into Dolphin's,
and on to a Wii by right-clicking the game in Dolphin's list and choosing
**Export Wii Save**, which writes a `data.bin` to copy to the SD card.

## Improvements over the Wii version

### Graphics

- **Widescreen.** The game runs in its native 16:9 mode at full
  horizontal resolution, rather than squeezed for a TV to stretch.
- **High resolution.** Renders at 1x to 6x the Wii's resolution, or to
  fit the window. 3x is pixel for pixel on a 1440p display.
- **High frame rates.** 30 fps (the game's own in the park), 60 fps, or
  120, 144, 165 or 240 fps. Above 60, extra frames are interpolated
  between the game's, with every object, camera and light moving
  smoothly. The game still updates 60 times a second, so nothing speeds
  up.
- **Anti-aliasing.** 2x, 4x or 8x MSAA.
- **Anisotropic filtering.** 2x to 16x, for sharp ground and walls at
  an angle.
- **Aspect ratio.** 16:9 (the game's), its own 4:3, or 16:10 and 21:9
  ultrawide, which show more of the world above and below or at the
  sides. The HUD and menus stay at 16:9 in the middle.
- **Field of view.** 80% to 150% of the game's, in the park.
- **Movies.** Shown as they were, sharpened, or AI upscaled.
- **Vulkan and OpenGL renderers.** Vulkan is the default and the
  fastest. It falls back to OpenGL if Vulkan can't start.
- **Fullscreen** (borderless) or windowed, toggled with Alt+Enter.

### Controls

- **Keyboard and mouse**, with a **mouse-controlled camera** that turns
  and tilts the player camera, even while running, kept inside walls and
  above the ground.
- **Xbox-style controllers**, with rumble (off, light, normal or strong),
  and movement in any direction with the left stick.
- **Fully rebindable.** Each Wii Remote button has two keyboard or mouse
  slots and two controller slots.
- **Button prompts match your device.** The game's on-screen Wii Remote
  icons are redrawn as the key or button they're bound to, in the menus'
  button bar, the bubble over Pikachu's head and the text, switching
  between keyboard and controller as you use them.
- **Instructions for your controls.** The attractions' How to Play
  pages, the partners' moves as each joins, the moves taught in the
  field, Iron Tail, Shake Sprint and the portals name your keys or
  buttons, instead of telling you to shake, tilt or point the remote.
- **Motion controls mapped.** Shaking, tilting and pointing are mapped
  to keys, the mouse and the sticks.
- **Dance Inferno, made for keys.** On the Wii any shake danced whatever
  move came up. Here each move has its own key, drawn on its icon along
  the bottom: press it as the move comes up and hold it to the end of
  its bar.
- Camera speed and inverted camera settings.

### Gameplay options

Optional changes to the game's balance, all off (100%) by default:

| Setting | Choices |
|---|---|
| Text speed | 1x (the game's), 2x, 3x, or a page at once |
| Berries collected | 50% to 300% |
| Prices | free, 50%, 100% or 200% |
| Damage dealt | 50% to 300% |
| Damage taken | none, 50% to 300% |
| Chase speed | 50% to 150% of the speed of the Pokémon you chase |

### Quality of life

- **Faster start-up.** The wrist strap screen is cut short.
- **An in-game Options menu.** It's in the PokéPark Pad (pause menu) and
  on the save files screen, in the game's own style, with a way to close
  the game. Changes apply immediately and are saved in `settings.ini`.
- **Autosave.** With Options > Gameplay > Autosave on, the game saves
  itself, as the PokéPark Pad's Save does, whenever you make a friend
  or enter another area. It's off by default.
- **Update check.** At start it asks GitHub whether a newer release is
  out. If one is, the save files screen says so, and Options > Display >
  Updates opens its download page. Nothing is downloaded by itself, and
  the Updates setting turns the check off.
- Runs at 60 Hz to match PC displays (the Wii runs at 59.94 Hz).

## Default controls

| Wii Remote | In the park | Keyboard and mouse | Controller |
|---|---|---|---|
| D-pad | Move | WASD / arrow keys | D-pad / left stick |
| 2 | Jump | Space / X | A |
| 1 | Dash; held, then let go, a move | Left click / Z | X |
| A | Call | Right click / Enter | B |
| B | Look around (held) | V / Left Ctrl | Y / right trigger |
| | Turn camera | Mouse | Right stick |
| + | PokéPark Pad | P | Start |
| - | Photo | M | Back |
| Shake | Iron Tail, the partners' moves, Shake Sprint, portals (held) | Q, Left Shift or E | LB or RB |

While the window has focus, the mouse is captured for the camera. Esc
releases it, and a click captures it again.

<details>
<summary>Attraction controls</summary>

| Attraction | Keyboard and mouse | Controller |
|---|---|---|
| Cake Contraption | Aim: mouse. Shoot: right click | Aim: right stick. Shoot: B |
| Dance Inferno | Each move's key, shown on its icon: W A S D, E, Space | D-pad, RB, A |
| Power Bash | Swing: Q and E by turns. Drop a bomb: right click | LB and RB by turns. B |
| Flight of Fancy | Steer: WASD. Faster: Space | Left stick. A |

</details>

## Known gaps

- **Graphics:** a few rare effects may still be wrong.
- **Sound:** the Wii Remote speaker is silent.
- **Input:** no Wii Remote pointer or Nunchuk. The game is played
  sideways, so neither is needed.
- **Android:** no rumble yet.
- It has been tested on one PC and the AYN Thor, so problems on other
  hardware are likely: please report them.

## Building

Needs Windows 10 or 11, Visual Studio 2022 (C++ desktop workload),
Python 3 and, for the Vulkan renderer, the
[Vulkan SDK](https://vulkan.lunarg.com/). Without the SDK, only OpenGL is
built.

1. Extract your disc image into `orig/S2LE01` with
   [decomp-toolkit](https://github.com/encounter/decomp-toolkit/releases)
   (`dtk disc extract "<your disc image>" orig/S2LE01`), so that
   `orig/S2LE01/sys/main.dol` exists.
2. Recompile and build:
   ```
   python tools/recomp/recomp.py
   python tools/recomp/build.py --run test
   python tools/recomp/build.py --public
   ```
   `--public` puts a release build in `Releases_S2LE01/`. Without it you
   get a development build at `build/recomp_S2LE01/boot.exe`, which also
   has a debug settings menu (Backspace, or Back+Start on a controller)
   that can start any minigame.

The Android app is built with `python tools/recomp/build.py --android`,
which needs the Android SDK and NDK and Rust; see
[recomp/README.md](recomp/README.md#android).

How it works, and the environment variables for testing, are described
in [recomp/README.md](recomp/README.md).

## Credits

- Built on [PokePark Wiicompiled](https://github.com/MildRain479/PokePark-Wiicompiled),
  the first game's port, which grew out of the
  [PokePark Wii decompilation](https://github.com/SephDB/pokepark-wii-decomp)
  by Seph De Busser and contributors. Many function names
  (`config/S2LE01/symbols_ported.txt`) are carried over from it.
- Disc images are read with
  [decomp-toolkit](https://github.com/encounter/decomp-toolkit) on
  Windows and [nod](https://github.com/encounter/nod) on Android, both by
  Luke Street and contributors (MIT or Apache-2.0; licenses in
  `recomp/assets`).
- The disc check follows [SparkPark](https://github.com/Dvrkie13/SparkPark)
  by Dvrkie13 (CC0).
- Button icons by [Icons8](https://icons8.com).

PokéPark 2: Wonders Beyond is © Nintendo, Creatures Inc., GAME FREAK inc.
and The Pokémon Company. This project is not affiliated with or endorsed
by them.

Released under CC0 1.0 (see `LICENSE`).
