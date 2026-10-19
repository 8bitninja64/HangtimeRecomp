# Changelog

All notable changes are listed here. Versions follow `MAJOR.MINOR.PATCH`; 0.x releases are
work in progress.

## [0.1.0] - 2026-10-19 (first public pre-release)

### Added
- Complete NBA Hangtime (USA) game recompiled from the player's own ROM: menus, four quarters,
  halftime, statistics and final results.
- Saving: the game's own Controller Pak code over an emulated Controller Pak per controller
  (records, created players, options).
- Up to four players on gamepads and the keyboard, joined the game's way.
- Launcher update check and in-place updates from GitHub releases (SHA-256 checked).
- Windows and Linux (Steam Deck) packages with the first-start build kit.
- Windows x64 runtime (Direct3D 12; Vulkan optional) with launcher, keyboard and gamepad
  controls, local multiplayer, true 16:9 widescreen, original cheats, fullscreen.
- Native Linux x86-64 build (Vulkan), work in progress.
- First-start build of the game code from the player's ROM; nothing from the cartridge is
  distributed.
- One-command source build (`build.py`) from a fresh clone; content checks (`check_public.py`).
