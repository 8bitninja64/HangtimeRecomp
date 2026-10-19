# HangtimeRecomp

**An unofficial, work-in-progress native PC port of NBA Hangtime for the Nintendo 64, built by
static recompilation.** Windows x64 today, native Linux in testing, Steam Deck planned.

> No game data is included. You need your own NBA Hangtime (USA) cartridge image (ROM); the
> program checks it and builds the game code from it on your own computer. Do not ask for or
> share ROM files. This project is not affiliated with, endorsed or sponsored by the NBA, Midway,
> Nintendo or any other rights holder.

## About

HangtimeRecomp runs the original game's own MIPS code, translated ahead of time into native C
by [N64Recomp](https://github.com/N64Recomp/N64Recomp) and compiled for your PC. It is not an
emulator frontend and not a remake: the game logic, physics, AI, menus and rules are the
original's. Around that code sits an independently written runtime:

- the N64 operating system library (libultra) from [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime),
- graphics through [RT64](https://github.com/rt64/rt64) (Direct3D 12 or Vulkan),
- console-accurate audio, controller input, a settings launcher and quality-of-life options.

Because recompiled game code is derived from the cartridge, **it is never distributed**: the
release package contains only the runtime and the build tools, and on first start it generates
the game code from your verified ROM (about one to two minutes, once). See
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

**Status: playable work in progress.** Complete matches play from the title screen to the final
results on Windows. Linux builds and runs; it has been verified under WSL2 with software
rendering but not yet on Linux GPU hardware or a Steam Deck.

## Features

Verified by automated tests that drive the real game (see [docs/TESTING.md](docs/TESTING.md)):

- **The full original game**: title, player select, options, team select, matchup, four
  quarters with breaks, halftime and end-of-game statistics, final results, attract mode.
- **60 frames per second** game timing on Windows (59.9 game frames per 60 video interrupts in
  complete matches), original audio at 22,047 Hz.
- **True 16:9 widescreen** for live play (F10), with the original 4:3 for menus and statistics
  screens; or the original 4:3 throughout.
- **Controls**: keyboard and gamepads (SDL game controllers), rebindable, checked through the
  game's own input decoding.
- **Local multiplayer**: gamepads on N64 controller ports 1-4, as opponents or teammates
  (two players verified with virtual gamepads; four physical gamepads not yet tested).
- **Original cheats**: 17 of the game's 21 cheat codes (matchup-screen codes and button
  sequences) from the launcher, applied through the game's own code, plus "Always On Fire".
  Four original codes cannot work in this version of the game (docs/ROADMAP.md).
- **Launcher**: graphics, controls, cheats, audio and game-files pages, usable with mouse,
  keyboard or gamepad; F1 returns to it from the game. Fullscreen (F11 or Alt+Enter), per-system
  render GPU choice for hybrid-graphics laptops.

## Playing

1. Download a release package for your system (see Releases; none published yet) or build from
   source (below).
2. Run `hangtime.exe` (Windows) or `./hangtime` (Linux) and choose your **NBA Hangtime (USA)**
   ROM when asked. Accepted: `.z64`, `.n64` or `.v64` byte order; the program checks the
   SHA-256 of the big-endian image:
   `6e1304d5bf8922a652f4063f9d26120ac8c3e9af2a5f8a4f4bc733cf003edf83`.
   Other regions and revisions are not supported.
3. The first start prepares the game files from your ROM (a progress window; nothing is
   downloaded, your ROM is not changed). Later starts go straight to the launcher.

Gamepads: Xbox, PlayStation, Switch Pro, 8BitDo and hundreds of others work through SDL and the
bundled `gamecontrollerdb.txt` (SDL_GameControllerDB). If yours is not recognized, `hangtime.log`
names it ("GAMEPAD unsupported"); a mapping line in a `gamecontrollerdb.txt` in your settings folder
adds it.

Default controls: arrows move, **S** shoot, **D** pass, **X**/Shift/Space turbo, **Enter** start;
gamepad left stick or D-pad, **A** shoot, **B** pass, **RB/RT** turbo, **Start**. F10 toggles
16:9, F11 fullscreen, F1 back to the launcher. Settings are kept in `hangtime.ini` beside the
program (or in your user folder when that is not writable).

| File or folder (beside the program) | What it is |
|---|---|
| `hangtime.ini` | settings (delete it to restore the defaults) |
| `saves/controller_pak_1.mpk` (2-4 for more players) | your Controller Pak: player records, created players and game options, saved by the game itself; standard 32 KB `.mpk` images, compatible with N64 emulators |
| `hangtime_game.dll` / `.so` | the game code generated from your ROM on first start; never share it |
| `game_build/build.log` | log of that first-start build |
| `hangtime.log` | run log (Windows) |
| `setup/`, `tools/` | the first-start build kit: configuration, N64Recomp, RSPRecomp, Zig |

## Building from source

See [BUILDING.md](BUILDING.md). In short, with the tools installed:

```
git clone --recursive https://github.com/8bitninja64/HangtimeRecomp.git
cd HangtimeRecomp
python build.py --rom "path/to/your/NBA Hangtime (USA).z64"
```

## Platforms

| Platform | Status |
|---|---|
| Windows 10/11 x64 | playable; complete matches verified; Direct3D 12 (Vulkan optional) |
| Linux x86-64 | builds and runs natively (Vulkan); verified under WSL2 with software rendering only; needs testing on real GPUs |
| Steam Deck | not yet tested; expected to work through the Linux build (gamepad-driven launcher, Vulkan); see [docs/TESTING.md](docs/TESTING.md) |

## Known issues

- Laptops with hybrid graphics: rendering on the discrete GPU while the screen is driven by
  the integrated one can cap the display at 20 frames per second on some systems. Choose the
  integrated GPU in Launcher > Graphics > Render GPU.
- Brief frame-time spikes when RT64 uploads new textures.
- Linux and Steam Deck are untested on real hardware.
- Windows needs the Microsoft Visual C++ 2015-2022 Redistributable (x64).
- The first-start build needs about 150 MB (most of it the Zig compiler cache, kept for faster rebuilds) of free space while it runs.
- Four original cheats (fast passing, max speed, max blocking, tournament mode) are unavailable.

## Roadmap

See [docs/ROADMAP.md](docs/ROADMAP.md): Linux and Steam Deck hardware validation, release
packages, launcher polish and accuracy work.

## Contributing

Bug reports, tests on your hardware and pull requests are welcome; see
[CONTRIBUTING.md](CONTRIBUTING.md). Never attach ROMs, generated code or game captures.

## AI disclosure

This project is developed with substantial help from AI coding assistants (Anthropic's Claude,
OpenAI's ChatGPT/Codex and similar tools), directed and reviewed by the project owner.
AI-produced code is held to the same standard as any other: it must build, pass the tests and
be reviewed. If you do not want to use software made this way, please do not use this project.

## License and acknowledgments

The project license is pending the owner's decision (recommended: GPL-3.0-or-later, see docs/LICENSING.md). Third-party components keep their own licenses; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [docs/LICENSING.md](docs/LICENSING.md).

Built on the work of: N64Recomp and N64ModernRuntime (Wiseguy and contributors), RT64 (Dario
and contributors), SDL, Dear ImGui, Zig, and the N64 homebrew and preservation community.

NBA Hangtime is a trademark of its respective owners. "NBA" and NBA team names and logos are
trademarks of NBA Properties, Inc.; Nintendo 64 is a trademark of Nintendo. They are used here
only to identify the game this software is compatible with. This repository contains no game
code, graphics, audio or other data from the cartridge.
