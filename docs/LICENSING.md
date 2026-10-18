# Licensing

This page describes what the project contains and which license terms apply, as surveyed from
the license files themselves. It is not legal advice.

## The project's own code

Our code is the runtime (`full_runtime/`, `process_context.*`), the build and analysis scripts
(`build.py`, `full_inventory.py`, `prepare_full.py`, `libultra_map.py`, ...), the tests and the
documentation. The project license is pending the owner's decision (recommended: GPL-3.0-or-later, see docs/LICENSING.md).

Why GPL-3.0 matters here: the runtime executable statically links **N64ModernRuntime**
(ultramodern and librecomp), which is licensed under the **GNU GPL version 3**. Its license file
states no version qualifier; GPL-3.0 section 14 then lets recipients choose any published
version. A distributed runtime binary therefore has to be offered under GPL-3.0 terms as a
whole, with its complete corresponding source (this repository at the release tag, including the
build scripts) and the license notices of every component. Every other component compiled into
the runtime is under a permissive license compatible with GPL-3.0 (MIT, BSD-2/3-Clause, Zlib,
ISC, Apache-2.0, NCSA, Khronos MIT-style): see [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

## What is never distributed

| | Why |
|---|---|
| The ROM and anything extracted from it (graphics, audio, data) | copyrighted game content |
| Recompiled game code (`full_generated/`, `audio_rsp_generated.cpp`) and the game module built from it (`hangtime_game.dll/.so`) | mechanically derived from the game's code |
| Function tables computed from the ROM (`full_inventory.json`, `full.symbols.toml`, `full.toml`) | derived from the ROM; computed on each build instead of stored |
| Game captures (screenshots, audio recordings, memory and display-list dumps) | game content |

`.gitignore` keeps these out of git, `check_public.py` rejects them in pull requests (ROM
fingerprints, generated-code markers, file types), and the release package script refuses them
and scans packages for ROM data. Passing these checks is not a legal clearance.

## What the repository does contain about the game

Facts needed to run the original code on a PC, written by us: memory addresses of functions and
variables, libultra function identification (`libultra_map.py`), hook positions and the code we
insert there (`prepare_full.py`), cheat codes and the game flags they set (`full_runtime/cheats.cpp`),
the ROM's SHA-256, a few original instruction words used to confirm hook positions, and
`tools/rom_fingerprints.dat` (truncated SHA-256 hashes of sampled 16-byte ROM windows; one-way,
contains no ROM bytes) used by `check_public.py`.

## Release packages

A release package contains the runtime (GPL-3.0 as a whole), separate third-party files (SDL2,
the DirectX Shader Compiler, the Roboto font), and the first-start build kit: N64Recomp and
RSPRecomp (MIT), the Zig toolchain (MIT, with bundled libraries under their own licenses), our
configuration and the headers the generated code compiles against. Each package includes
`LICENSES/`, `THIRD_PARTY.md` and the source location of the release.

## Open items before the first release

- The project license (owner decision; recommendation GPL-3.0-or-later).
- `dxil.dll` (Microsoft, proprietary terms): redistribution terms beside a GPL-3.0 program.
- `dxcompiler.dll`: the upstream release it comes from and its license and third-party notices.
- `utf8conv` (Windows runtime): confirm its license (the author's related repository is MIT).
- Zig's bundled glibc and Linux headers: include the LGPL-2.1 and GPL-2.0 texts; mingw-w64's
  public-domain disclaimer file.
- Qualified legal review of the project name, the contents listed above and the first-start
  build model.
