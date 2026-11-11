PokePark 2: Wonders Beyond - PC port (open beta)

A native Windows version of the game, recompiled from the Wii original.
No game data is included: you need your own copy of the game.

Getting started
- Windows 10 or 11, 64-bit, and a graphics card with Vulkan 1.3 or
  OpenGL 3.3.
- Run PokePark2.exe. The first time, it asks for your disc image of
  PokePark 2: Wonders Beyond (USA, S2LE01): an ISO, RVZ, WBFS or the
  like. It sets the game up in an S2LE01 folder beside the program;
  your saves are kept in S2LE01\nand.
- Keep the prompts and tools folders beside PokePark2.exe.

Settings
- Options are on the PokePark Pad (and on the save files screen):
  display, keyboard, controller and gameplay. They are saved in
  settings.ini beside the program.
- Renderer: Vulkan by default; OpenGL in Options > Display (from the
  next start). If Vulkan can't start, OpenGL is used.
- Xbox-style controllers and keyboard and mouse are supported. Button
  prompts in the game show the key or button you have bound.
- Aspect ratio (Options > Display): 16:9 by default; 16:10 and 21:9
  show more of the world around the picture; 4:3 is the game's own
  4:3 mode (from the next start).
- Autosave (Options > Gameplay, off by default): the game saves to
  the slot it was loaded from whenever you make a friend or enter
  another area, once any scene or conversation is over.

This is a beta
Some things aren't done yet: a few rare graphics effects, the Wii
Remote speaker, and the Wii Remote pointer and Nunchuk (the game
doesn't need them). It has been tested on one PC so far, so problems
on other hardware are likely.

Reporting problems: open an issue on the project's GitHub page. Say
what you did and what happened, attach a screenshot if you can, and
give your Windows version, graphics card, and the Renderer setting.

Credits
- Button icons by Icons8 (https://icons8.com).
- Disc images are read with decomp-toolkit (tools\dtk.exe); its license
  is in tools\dtk_license.txt.
- PokePark 2 is (c) Nintendo, Creatures Inc., GAME FREAK inc. and The
  Pokemon Company. This project is not affiliated with or endorsed by
  them.
