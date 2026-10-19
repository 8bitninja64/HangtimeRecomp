# HangtimeRecomp

A native PC port of NBA Hangtime (N64, USA) for Windows, Linux and Steam Deck, made by static
recompilation. No emulator: the game's code is translated from your ROM and compiled on your PC.

**Status:** fully playable from the title screen to the final results. Early release; please report problems.

**No game code or assets are included.** You need your own ROM.

## ROM

NBA Hangtime **USA**, `.z64`, `.n64` or `.v64`, SHA-256
`6e1304d5bf8922a652f4063f9d26120ac8c3e9af2a5f8a4f4bc733cf003edf83`. The program asks for it on
first start.

## Features

**Gameplay**
- The complete original game: menus, four quarters, halftime, stats, final results
- Up to 4 players on gamepads and keyboard
- Saving (records, created players, options) through the game's own Controller Pak code
- 17 of the game's original cheats from the launcher, plus Always On Fire

**Visuals**
- 60 FPS, original audio
- True 16:9 widescreen (F10)
- Resolution, anti-aliasing and sprite/texture filtering options

**Launcher**
- Settings for graphics, controls, audio and cheats; works with mouse, keyboard or gamepad
- Rebindable controls; hundreds of gamepads supported out of the box
- Built-in updates: shows **UPDATE AVAILABLE** and installs new releases for you

## Download and play

1. Download the latest [release](https://github.com/8bitninja64/HangtimeRecomp/releases/latest):
   `HangtimeRecomp-win64-*.zip` (Windows) or `HangtimeRecomp-linux-x86_64-*.tar.gz` (Linux / Steam Deck).
2. Unzip it anywhere and run `hangtime.exe` (Windows) or `./hangtime` (Linux).
3. Pick your ROM. The first start builds the game from it (a minute or two, once).

Your settings (`hangtime.ini`) and saves (`saves/`) live next to the program.

Windows: if it says `VCRUNTIME140.dll` is missing, install the
[Microsoft Visual C++ Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe).

## Controls

| Action | Keyboard | Gamepad |
|---|---|---|
| Move | Arrow keys | Left stick / D-pad |
| Shoot | S | A |
| Pass | D | B |
| Turbo | X / Shift / Space | RB / RT |
| Start | Enter | Start |

| Key | Action |
|---|---|
| F1 | Back to the launcher |
| F10 | 16:9 widescreen on/off |
| F11 / Alt+Enter | Fullscreen |

## Build from source

See [BUILDING.md](BUILDING.md). In short:

```
git clone --recursive https://github.com/8bitninja64/HangtimeRecomp.git
cd HangtimeRecomp
python build.py --rom "path/to/NBA Hangtime (USA).z64"
```

## How it works

[N64Recomp](https://github.com/N64Recomp/N64Recomp) translates the game's code,
[N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) replaces the N64 operating system and
[RT64](https://github.com/rt64/rt64) renders. Because the translated code comes from the cartridge, it is never
distributed: downloads contain only the program and the build tools, and the game is built from your ROM on your
PC. More in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## AI disclosure

This project was built with heavy help from AI (Claude), under the direction of the project owner.

## Legal

NBA Hangtime, the NBA and its team names belong to their rights holders. This project is not affiliated with or
endorsed by them, Midway or Nintendo. Do not share the ROM or anything generated from it.
This project's own code is licensed under the GNU General Public License, version 3 or (at your option) any later version (GPL-3.0-or-later); the full text is in [LICENSE](LICENSE). Third-party components keep their own licenses ([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).

## Credits

- N64Recomp, N64ModernRuntime, RT64 and their contributors
- SDL, Dear ImGui, Zig and SDL_GameControllerDB
- The N64 homebrew and preservation community
