# Building HangtimeRecomp

You need your own **NBA Hangtime (USA)** ROM: the build generates the game code from it. The
ROM and everything generated from it stay on your machine (git-ignored; never commit them).

`build.py` does everything from a fresh clone: it checks the ROM, fetches the pinned upstream
sources (git submodules), builds N64Recomp/RSPRecomp and RT64, recompiles the game code from your
ROM, and builds the runtime and the game module.

```
git clone --recursive https://github.com/8bitninja64/HangtimeRecomp.git
cd HangtimeRecomp
python build.py --rom "path/to/NBA Hangtime (USA).z64"
```

Instead of `--rom` you can put the ROM in the repository's `rom/` folder (any of `.z64`, `.n64`,
`.v64`) or set `HANGTIME_ROM`. `python build.py --clean` rebuilds everything. Step logs go to
`build/logs/`. The first build takes 10-30 minutes (RT64 and its shader tools); later builds only
redo what changed.

## Windows 10/11 (x64)

Install:
- **Visual Studio 2022** (Community is fine) or the Build Tools for Visual Studio 2022, with the
  "Desktop development with C++" workload (MSVC v143 and a Windows 10/11 SDK).
- **CMake** 3.20 or newer (on PATH, or the Visual Studio copy).
- **Python** 3.10 or newer and **Git**.

Result: `full_runtime\build\Release\hangtime.exe` with `hangtime_game.dll`, `SDL2.dll`,
`dxcompiler.dll`, `dxil.dll` and `assets\` beside it. Run `hangtime.exe` from that folder.

## Linux (x86-64)

Debian/Ubuntu packages (names differ on other distributions):

```
sudo apt install git cmake ninja-build clang python3 pkg-config libsdl2-dev libgtk-3-dev libvulkan-dev
```

`build.py` uses clang (or gcc; set `CC`/`CXX` to choose). Result: `build/linux/hangtime` with
`hangtime_game.so` and `assets/`. Run `./hangtime` from that folder. Running needs a Vulkan 1.2
driver (Mesa RADV/ANV, NVIDIA proprietary), SDL2 and GTK 3.

## What gets built where

| Path | What | Distributable? |
|---|---|---|
| `external/` | pinned upstream sources (N64Recomp, RT64; git submodules) | under their own licenses |
| `build/deps/` | N64Recomp, RSPRecomp, RT64 static libraries | yes (tools), see THIRD_PARTY_NOTICES.md |
| `full_generated/`, `audio_rsp_generated.cpp`, `full.toml`, `full.symbols.toml`, `full_inventory.json` | game code and tables generated from your ROM | **no** |
| `hangtime[.exe]` | the runtime (no game code) | yes, once the project license is published |
| `hangtime_game.dll` / `.so` | the game module (generated code) | **no** |

## Release packages

`python package_release.py` (Windows) or `python3 package_release.py --linux` assemble a package
of the runtime with the first-start build kit (setup files, N64Recomp, RSPRecomp and the Zig
C/C++ toolchain) and scan it for anything derived from the ROM. Packages are marked INTERNAL until
the project's license and legal review are complete. Packaging needs Zig 0.17.0 (`ZIG_DIR`).

## Tests

`python check_public.py` (no ROM needed) checks the tree for ROM data, generated code, binaries,
secrets and personal paths. Gameplay tests need your ROM and a desktop session; see
[docs/TESTING.md](docs/TESTING.md).
