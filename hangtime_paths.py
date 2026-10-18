"""Where the build finds the ROM and the pinned upstream projects (no machine-specific paths).

Each location is, in order: an environment variable, the repository's own folder (a fresh clone:
`rom/` for the player's ROM, `external/` for the git submodules), then the original development
layout beside the repository (../rom, ../N64Recomp, ../runtime/rt64).

  HANGTIME_ROM        the NBA Hangtime (USA) ROM image (.z64/.n64/.v64; checked by SHA-256)
  HANGTIME_N64RECOMP  N64Recomp source tree (pinned ffb39cd)
  HANGTIME_RT64       RT64 source tree (pinned de343a5)
"""
import os
from pathlib import Path

REPO = Path(__file__).resolve().parent
ROM_SHA256 = '6e1304d5bf8922a652f4063f9d26120ac8c3e9af2a5f8a4f4bc733cf003edf83'   # NBA Hangtime (USA), big-endian .z64

def _first(env, *candidates):
    if os.environ.get(env): return Path(os.environ[env]).resolve()
    for c in candidates:
        if c.exists(): return c.resolve()
    return candidates[0]

def rom_path():
    """The ROM: $HANGTIME_ROM, else the only ROM image in rom/, else ../rom/NBA Hangtime (USA).z64."""
    if os.environ.get('HANGTIME_ROM'): return Path(os.environ['HANGTIME_ROM']).resolve()
    images = sorted(p for p in (REPO / 'rom').glob('*') if p.suffix.lower() in ('.z64', '.n64', '.v64'))
    if images: return images[0].resolve()
    return (REPO.parent / 'rom' / 'NBA Hangtime (USA).z64').resolve()

def rom_bytes():
    """The ROM as a big-endian (.z64) image; .v64 (byte-swapped) and .n64 (little-endian) are converted."""
    import hashlib
    path = rom_path()
    if not path.exists():
        raise SystemExit(f'ROM not found: {path}\nPut your NBA Hangtime (USA) ROM in {REPO / "rom"} or set HANGTIME_ROM.')
    data = bytearray(path.read_bytes())
    if data[:4] == b'\x37\x80\x40\x12':     # .v64: 16-bit byte swap
        data[0::2], data[1::2] = data[1::2], data[0::2]
    elif data[:4] == b'\x40\x12\x37\x80':   # .n64: 32-bit little-endian words
        for i in range(0, len(data) - 3, 4): data[i:i + 4] = data[i:i + 4][::-1]
    if hashlib.sha256(data).hexdigest() != ROM_SHA256:
        raise SystemExit(f'{path} is not NBA Hangtime (USA) (SHA-256 of the big-endian image differs).')
    return bytes(data)

def rom_z64_path(work):
    """A big-endian ROM file for the recompilers: the ROM itself when it is a .z64, else a
    converted copy written to `work` (a git-ignored build folder)."""
    import hashlib
    data = rom_bytes()
    path = rom_path()
    if path.read_bytes()[:4] == b'\x80\x37\x12\x40': return path
    work = Path(work); work.mkdir(parents=True, exist_ok=True)
    out = work / 'rom.z64'
    if not out.exists() or hashlib.sha256(out.read_bytes()).hexdigest() != ROM_SHA256: out.write_bytes(data)
    return out.resolve()

def n64recomp_dir(): return _first('HANGTIME_N64RECOMP', REPO / 'external' / 'N64Recomp', REPO.parent / 'N64Recomp')
def rt64_dir(): return _first('HANGTIME_RT64', REPO / 'external' / 'rt64', REPO.parent / 'runtime' / 'rt64')
