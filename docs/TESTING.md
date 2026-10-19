# Testing

## Without a ROM (automatic; CI runs these)

- `python check_public.py`: the tree contains no ROM data (fingerprints of ROM windows, raw and
  as hex words), no generated code, binaries, secrets, personal paths or unexpected e-mail
  addresses. `--release` also requires the license files.
- CI builds the runtime-side code on Windows and Linux (the game module needs a ROM, so CI
  cannot build or run the game).

## With your ROM (local; Windows)

These drive the real game through scripted controller input gated on the game's own state and
check its RAM, logs, frames and audio. They open a game window; run them one at a time.

| Command | Checks | Time |
|---|---|---|
| `python verify_full.py` | boot to the title and player select, frame pacing | 1 min |
| `python verify_full.py --match` | menus, team select, tip-off, live play, audio | 3 min |
| `python verify_full.py --full-match [--widescreen]` | a complete match to FINAL RESULTS | 14 min |
| `python verify_input.py` | keyboard and virtual gamepad: every control reaches the game | 6 min |
| `python verify_cheats.py` | original cheats through the game's own flags | 6 min |
| `python verify_update.py` | update in place from a fake local GitHub release: bad checksum refused, install keeps settings and saves, launcher install restarts, the new version rebuilds the game files and plays | 12 min |
| `python verify_multiplayer.py` | two virtual gamepads as opponents and as teammates; four gamepads joining as players 1-4 | 11 min |
| `python bench_perf.py` | frame timing (waits for a quiet machine) | 4 min |
| `python verify_partition.py` | the runtime's own partition equals the generators' output | 10 s |

## Hardware tests that need a person

| Test | How | Report |
|---|---|---|
| Clean Windows PC | extract a release package on a PC without development tools; run `hangtime.exe`, choose the ROM, let the first-start build finish, play a quarter | first-start time, any error dialog, `game_build/build.log` if it failed |
| Physical gamepad | play with an Xbox/PlayStation/other pad: menus, team select, all buttons in play, the launcher with the pad only | which pad, anything that did not respond |
| Native Linux (GPU) | build or extract on Linux with a working Vulkan driver; play a match; try the launcher, fullscreen, a gamepad | distribution, GPU and driver, frame rate, audio |
| Steam Deck, Desktop Mode | copy the Linux build, run `./hangtime` from the file manager or Konsole; the launcher and game with the built-in controls | as above |
| Steam Deck, Game Mode | add `hangtime` as a non-Steam game in Desktop Mode, then start it from Game Mode | whether it starts fullscreen, controls, performance, suspend/resume |

Until these are reported, the README lists those platforms as untested.
