# Roadmap

## Done
- Whole-program recompilation of all 2,380 CPU functions and the audio microcode.
- Complete matches on Windows: menus through final results, original audio, 60 FPS timing.
- Launcher, rebindable keyboard and gamepad controls, local multiplayer (ports 1-4), true 16:9
  widescreen for live play, original cheats, fullscreen, render GPU choice.
- Runtime / game module split: no game code in anything distributed; first-start build of the
  game module from the player's ROM with a bundled toolchain (Windows and Linux); the function
  partition is computed on the player's machine, so packages carry no function tables.
- Native Linux build (Vulkan), verified under WSL2 with software rendering.
- Saving: the game's own Controller Pak code runs over an emulated Controller Pak per connected
  controller (records, created players, options persist as .mpk files).

## Next
1. **Public release readiness**: license and naming decisions, legal review, first release.
2. **Linux on real hardware** and **Steam Deck** (Desktop and Game Mode): testing, controller-only
   launcher flow, sensible graphics defaults, packaging (glibc baseline).
3. **Clean-machine Windows testing**; decide between the Visual C++ Redistributable and a
   static runtime.
4. Launcher: "rebuild game files" button, controller remapping polish.
5. Rendering: smoother texture uploads; RT64 enhancement options where they keep the original
   look (resolution, MSAA) once evaluated.

## Not planned
Online play, mods or new game content: the goal is the original game, accurately, on modern PCs.
