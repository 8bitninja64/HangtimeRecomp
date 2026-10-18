# How NBA Hangtime Recompiled works

## Two products

| | What it contains | Built from | Distributed? |
|---|---|---|---|
| **Runtime** `hangtime[.exe]` | our runtime code (`full_runtime/`, `process_context.cpp`), N64ModernRuntime (libultra, `vendor/`), RT64, SDL2, Dear ImGui | this repository and its pinned dependencies | yes (release packages) |
| **Game module** `hangtime_game.dll` / `.so` | the game's CPU code and audio microcode, recompiled to C and compiled; the module table (`game_module_api.cpp`) | the player's own ROM | **never** |

The runtime contains no game code. At start it loads the game module through one exported
table (`game_module.hpp`: API version, the ROM's SHA-256, section table, boot entry, audio
microcode entry). Runtime code reaches game functions only through that table. A module built
from a different ROM or for another runtime version is rejected and rebuilt.

## From ROM to module

1. **Partition** (`full_inventory.py`): the game has no code overlays; all CPU code is the boot
   image (ROM 0x1000 -> 0x80000400) up to the RSP boot code. Function starts come from the boot
   entry, every `jal` target, code pointers in the data image and code after a function's natural
   end. The result is computed from the ROM on each build; no function table is stored here.
2. **Configuration** (`prepare_full.py`, `libultra_map.py`): libultra routines get their names
   so N64Recomp maps them to N64ModernRuntime; hooks are inserted at a few places (safe points
   for the runtime's thread switching, the game's process switcher, cheats, widescreen culling).
3. **Recompile**: N64Recomp turns every function into C; RSPRecomp turns the audio microcode
   into C++.
4. **Compile**: developers use MSVC or clang (`build.py`); players' release packages use the
   bundled Zig C/C++ toolchain on first start (`full_runtime/module_builder.cpp`), keeping
   only the finished module and a build log.

## Runtime

- **Threads and scheduling**: libultra threads run as fibers (N64ModernRuntime ultramodern);
  the game's own cooperative process system is switched with `process_context.cpp`.
- **Video**: RT64 renders the game's display lists (Direct3D 12 on Windows, Vulkan on Linux),
  paced by an emulated video interrupt at 60 Hz with a presentation guard that never blocks the
  game on the GPU.
- **Audio**: the recompiled audio microcode produces the original sample stream; the AI
  (audio interface) FIFO and DAC timing follow the console.
- **Input**: keyboard and SDL game controllers are mapped to N64 controllers (ports 1-4) through
  the controller callback the game polls; nothing else touches game state.
- **Widescreen**: optional; widens the projection and the game's own sprite culling in live play
  through generated hooks, restoring the original values afterwards.
- **Launcher**: a Dear ImGui settings program shown before the game (`launcher_ui.cpp`).

## Upstream projects

| | Pinned | Role |
|---|---|---|
| [N64Recomp](https://github.com/N64Recomp/N64Recomp) | ffb39cd | recompilers (build tools) |
| [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) | cdf5abb (vendored in `vendor/`, unmodified; `vendor_manifest.json`) | libultra reimplementation |
| [RT64](https://github.com/rt64/rt64) | de343a5 | renderer |
