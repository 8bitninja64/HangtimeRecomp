"""Assemble and scan a distribution package of the runtime (RELEASE_LEGAL.md).

usage: python package_release.py [--out dist] [--personal]          (Windows)
       python3 package_release.py --linux [--out DIR] [--personal]  (Linux / WSL; BUILD_DIR, N64RECOMP_BUILD, ZIG_DIR)

--personal: a ready-to-play package like other recompilation projects ship (Rush2Recompiled): the
runtime plus the game module already built from the owner's ROM, no first-start build kit (about
30 MB instead of 330). It contains game code generated from the ROM, so it is for the ROM owner's
own devices only (NOTICE.txt, "-PERSONAL" suffix); never upload or share it (RELEASE_LEGAL.md).

Windows: copies the runtime (full_runtime/build/Release: hangtime.exe, SDL2.dll, dxcompiler.dll,
dxil.dll, assets/Roboto-Medium.ttf, hangtime.lib for the player-side module build), license texts,
THIRD_PARTY.md and README.txt into dist/<name>/ and zips it. Linux: the ELF from build_linux.sh
(system SDL2/GTK3/Vulkan, not bundled) and Linux builds of the tools, as a .tar.gz.
Both include the first-run module build kit (setup/, tools/: N64Recomp, RSPRecomp, Zig with only the
package's own target's libc trees, copy_zig). It never copies the game module,
generated sources, the ROM, settings, logs or symbol maps, and then scans the result:
  - forbidden names (hangtime_game.*, *.z64/*.n64/*.v64, *.map, *.pdb, hangtime.ini, *.log, captures)
  - the ROM's code: 8,192 sampled 16-byte windows of the ROM's CPU text (both byte orders) must
    occur in no packaged file (needs the developer's local ROM; the ROM itself is never packaged)
  - every component listed in THIRD_PARTY.md has its license text, or the package is INTERNAL.
The package stays INTERNAL (NOTICE.txt, folder suffix) while RELEASE_LEGAL.md's open questions are
unresolved; that flag is set here and must be removed deliberately after review.
"""
import hashlib, json, os, random, shutil, subprocess, sys, tarfile, zipfile
from pathlib import Path

P = Path(__file__).resolve().parent
sys.path.insert(0, str(P))
import hangtime_paths, third_party
ROOT = P.parent
VER = '0.1.0'
INTERNAL = False  # owner 2026-10-19: public pre-release, GPL-3.0-or-later (PUBLIC_RELEASE.md); a missing license text still makes a package INTERNAL
LINUX = '--linux' in sys.argv
PERSONAL = '--personal' in sys.argv
EXE = '' if LINUX else '.exe'
def _env_or(var, *candidates):
    if os.environ.get(var): return Path(os.environ[var])
    return next((c for c in candidates if c.exists()), candidates[0])
# Build outputs: build.py's folders, else the development layout. Zig 0.17.0 (MIT): $ZIG_DIR.
if LINUX:
    BUILD = _env_or('BUILD_DIR', P / 'build/linux', Path.home() / 'build/hangtime')
    ZIG_DIR = _env_or('ZIG_DIR', Path.home() / 'zig/zig-x86_64-linux-0.17.0')
    N64RECOMP_BIN = _env_or('N64RECOMP_BUILD', P / 'build/deps/n64recomp', Path.home() / 'build/n64recomp')
else:
    BUILD = _env_or('BUILD_DIR', P / 'full_runtime/build/Release')
    ZIG_DIR = _env_or('ZIG_DIR', ROOT / 'experiments/zig/win/zig-x86_64-windows-0.17.0')
    N64RECOMP_BIN = _env_or('N64RECOMP_BUILD', P / 'build/deps/n64recomp/Release', hangtime_paths.n64recomp_dir() / 'build/Release')
RTV = P / 'vendor/N64ModernRuntime'
# First-run module build kit (module_builder.hpp): what the generated code compiles against,
# as published path -> source. All are our sources or the MIT/GPL-3.0 runtime headers; the list is
# the compiler's own dependency output for the module sources (zig -M).
SDK = {
    'include/recomp.h': hangtime_paths.n64recomp_dir() / 'include/recomp.h',
    'include/hangtime_full.h': P / 'full_runtime/include/hangtime_full.h',
    'include/librecomp/recomp.h': P / 'full_runtime/include/librecomp/recomp.h',
    'include/process_context.h': P / 'process_context.h',
    'include/game_module.hpp': P / 'full_runtime/game_module.hpp',
    'include/librecomp/rsp.hpp': RTV / 'librecomp/include/librecomp/rsp.hpp',
    'include/librecomp/rsp_vu.hpp': RTV / 'librecomp/include/librecomp/rsp_vu.hpp',
    'include/librecomp/rsp_vu_impl.hpp': RTV / 'librecomp/include/librecomp/rsp_vu_impl.hpp',
    'include/librecomp/sections.h': RTV / 'librecomp/include/librecomp/sections.h',
    'include/ultramodern/ultra64.h': RTV / 'ultramodern/include/ultramodern/ultra64.h',
    'src/game_module_api.cpp': P / 'full_runtime/game_module_api.cpp',
    'src/rsp.cpp': RTV / 'librecomp/src/rsp.cpp',
}
# The function partition and hooks are computed on the player's machine (partition.cpp); only the
# audio microcode's location and dispatch targets are configuration.
SETUP = {'audio_rsp.toml': P / 'audio_rsp.toml'}

# Third-party components in this package (third_party.py): compiled into the runtime or shipped.
def package_components():
    out = []
    for name, ver, spdx, cr, prod, lic, note in third_party.COMPONENTS:
        if not any(k in prod for k in ('runtime', 'package')): continue
        if PERSONAL and ('tools/' in prod or name == 'Zig'): continue   # no first-start build kit
        if LINUX and (prod.startswith('package (Windows)') or prod == 'runtime (Windows)'): continue
        root = third_party.roots().get(lic[0]) if lic else None
        path = ZIG_DIR / 'LICENSE' if name == 'Zig' else (root / lic[1] if lic else None)
        out.append((f'{name} {ver}'.strip(), spdx, path, note))
    return out
FILES = ['hangtime', 'assets/Roboto-Medium.ttf', 'gamecontrollerdb.txt'] if LINUX else         ['hangtime.exe', 'SDL2.dll', 'dxcompiler.dll', 'dxil.dll', 'hangtime.lib', 'assets/Roboto-Medium.ttf', 'gamecontrollerdb.txt']
FORBIDDEN = ('full.toml', 'full.symbols.toml', 'full_inventory', 'hangtime_game', '.z64', '.n64', '.v64', '.map', '.pdb', 'hangtime.ini', '.log', 'captures', 'full_generated', 'audio_rsp_generated', 'game_build')

README_PERSONAL = """HangtimeRecomp {ver} - unofficial PC port of NBA Hangtime (Nintendo 64)

Unofficial fan project. Not affiliated with, endorsed or sponsored by the NBA, Midway, Nintendo or
any other rights holder. All trademarks belong to their owners.

PERSONAL BUILD: this package contains game code generated from its owner's own NBA Hangtime (USA)
ROM (hangtime_game.{module_ext}). It is for that person's own computers only. Do not upload, share or
distribute it.

Requirements: {requirements}

Start: run {exe} and choose your NBA Hangtime (USA) ROM (.z64/.n64/.v64; SHA-256 checked), or put
it beside the program as "NBA Hangtime (USA).z64". Settings are stored in hangtime.ini beside it (or
in your user folder). License texts of the included components: LICENSES/ and THIRD_PARTY.md.
"""

README = """HangtimeRecomp {ver} - unofficial PC port of NBA Hangtime (Nintendo 64)

Unofficial fan project. Not affiliated with, endorsed or sponsored by the NBA, Midway, Nintendo or
any other rights holder. All trademarks belong to their owners.

This package contains no game data and no game code. You need your own copy of NBA Hangtime (USA)
for the Nintendo 64; the program checks it (SHA-256) and builds the game files from it on your
computer. Do not ask for or share ROM files.

Status: {status}

Requirements: {requirements}

Start: run {exe} and choose your ROM. The first time, the program prepares the game files
from your ROM (about a minute; tools/ holds the recompiler and the Zig compiler it uses) and keeps
them in your data folder. Settings are stored in hangtime.ini beside it (or in your user folder).
Updates: the launcher shows UPDATE AVAILABLE when a newer release is published and installs it
in place on request (your ROM, settings and saves are kept).

License: {product} is free software under the GNU General Public License, version 3 or (at your
option) any later version (LICENSE.txt). Source code: https://github.com/{slug} (the release tag
matching this version). License texts of the included components: LICENSES/ and THIRD_PARTY.md.
"""

def main():
    out = Path(sys.argv[sys.argv.index('--out') + 1]) if '--out' in sys.argv else P / 'dist'
    name = f'HangtimeRecomp-{"linux-x86_64" if LINUX else "win64"}-{VER}' + ('-PERSONAL' if PERSONAL else '-INTERNAL' if INTERNAL else '')
    d = out / name
    if d.exists(): shutil.rmtree(d)
    (d / 'LICENSES').mkdir(parents=True)
    module = 'hangtime_game.so' if LINUX else 'hangtime_game.dll'
    files = [f for f in FILES if f != 'hangtime.lib'] + [module] if PERSONAL else FILES
    for f in files:
        src = BUILD / f
        assert src.exists(), f'missing build output {src} (run build_full.py)'
        (d / f).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, d / f)
        # Linux builds carry debug info (78 MB -> 6 MB): strip it, keeping the dynamic symbols
        # the game module links against and crash backtraces use.
        if LINUX and f in ('hangtime', 'hangtime_game.so') and shutil.which('strip'):
            subprocess.run(['strip', '--strip-unneeded', str(d / f)], check=True)
    for pub, src in ({} if PERSONAL else SDK).items():
        assert src.exists(), f'missing {src}'
        (d / 'setup/sdk' / pub).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, d / 'setup/sdk' / pub)
    for pub, src in ({} if PERSONAL else SETUP).items():
        assert src.exists(), f'missing {src} (run build_full.py: prepare_full.py writes it)'
        shutil.copy2(src, d / 'setup' / pub)
    if not PERSONAL:   # the first-start build kit
        (d / 'tools').mkdir(exist_ok=True)
        for exe in ('N64Recomp' + EXE, 'RSPRecomp' + EXE): shutil.copy2(N64RECOMP_BIN / exe, d / 'tools' / exe)
        assert (ZIG_DIR / ('zig' + EXE)).exists(), f'Zig not found at {ZIG_DIR}'
        copy_zig(ZIG_DIR, d / 'tools/zig')
    missing, rows = [], ['# Third-party components in this package', '', '| component | license | text |', '|---|---|---|']
    for comp, lic, path, note in package_components():
        if path and path.exists():
            dst = 'LICENSES/' + ''.join(c if c.isalnum() or c in '-_' else '_' for c in comp.split(' (')[0]).strip('_') + '.txt'
            if path.suffix in ('.h', '.hpp', '.c', '.cpp'):   # license in a source header: the header's opening (license block)
                lines = path.read_text(encoding='utf-8', errors='replace').splitlines()
                (d / dst).write_text('\n'.join(lines[:150]) + '\n', encoding='utf-8')
            else:
                shutil.copy2(path, d / dst)
            rows.append(f'| {comp} | {lic} | {dst} |')
        else:
            missing.append(comp); rows.append(f'| {comp} | {lic} | **missing: obtain before release** |')
        if 'OPEN' in note and comp not in missing: missing.append(comp + ' (open question)')
    # Zig's bundled glibc (LGPL-2.1-or-later) and Linux UAPI headers (GPL-2.0 WITH Linux-syscall-note)
    # refer to these texts without shipping them; the standard texts from RT64's Mupen64Plus docs.
    for text, lic_name in (() if PERSONAL else (('lgpl-license', 'LGPL-2.1.txt'), ('gpl-license', 'GPL-2.0.txt'))):
        src = third_party.roots()['rt64'] / 'src/contrib/mupen64plus-core/doc' / text
        if src.exists(): shutil.copy2(src, d / 'LICENSES' / lic_name)
        else: missing.append(f'{lic_name} (for tools/zig)')
    sdl_readme = third_party.roots()['rt64'] / 'src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/README-SDL.txt'
    if not LINUX and sdl_readme.exists(): shutil.copy2(sdl_readme, d / 'README-SDL.txt')
    if LINUX:
        rows += ['', 'Used from the system, not bundled: SDL2 (zlib), GTK 3 (LGPL-2.1), Vulkan loader (Apache-2.0).']
    (d / 'THIRD_PARTY.md').write_text('\n'.join(rows) + '\n', encoding='utf-8')
    assert PERSONAL or INTERNAL or not missing, f'license texts missing for a release package: {missing}'
    status = ('INTERNAL BUILD - not for public distribution (license and legal review pending, RELEASE_LEGAL.md).'
              if INTERNAL or missing else f'public pre-release {VER} (unofficial fan project)')
    requirements = (f'x86_64 Linux with glibc {glibc_needed(BUILD / "hangtime")} or newer, SDL2, GTK 3 and a Vulkan '
                    '1.2 driver (Debian/Ubuntu: libsdl2-2.0-0 libgtk-3-0 libvulkan1).' if LINUX else
                    '64-bit Windows 10/11, Microsoft Visual C++ Redistributable 2015-2022 (x64).')
    if PERSONAL:
        (d / 'README.txt').write_text(README_PERSONAL.format(ver=VER, requirements=requirements, module_ext='so' if LINUX else 'dll',
                                                             exe='./hangtime' if LINUX else 'hangtime.exe'), encoding='utf-8')
        (d / 'NOTICE.txt').write_text('PERSONAL BUILD - contains game code generated from its owner\'s own ROM.\n'
                                      'For the ROM owner\'s own devices only. Do not upload, share or distribute.\n', encoding='utf-8')
    else:
        export = json.loads((P / 'public/export.json').read_text(encoding='utf-8'))
        (d / 'README.txt').write_text(README.format(ver=VER, status=status, requirements=requirements, product=export['PROJECT_NAME'],
                                                    slug=export['GITHUB_SLUG'], exe='./hangtime' if LINUX else 'hangtime.exe'), encoding='utf-8')
        shutil.copy2(P / 'public/LICENSE', d / 'LICENSE.txt')   # the project's license (GPL-3.0-or-later)
    if (INTERNAL or missing) and not PERSONAL:
        (d / 'NOTICE.txt').write_text('INTERNAL BUILD - DO NOT DISTRIBUTE.\nMissing license texts: ' + (', '.join(missing) or 'none') + '\n', encoding='utf-8')
    scan(d)
    if LINUX:   # tar keeps the executable bits and Zig's symlinks
        z = out / (name + '.tar.gz')
        with tarfile.open(z, 'w:gz') as tf: tf.add(d, arcname=name)
    else:
        z = out / (name + '.zip')
        with zipfile.ZipFile(z, 'w', zipfile.ZIP_DEFLATED) as zf:
            for f in sorted(d.rglob('*')):
                if f.is_file(): zf.write(f, f.relative_to(out))
    # SHA256SUMS.txt beside the packages (sha256sum format), uploaded with a release: the launcher's
    # updater checks a download against it when GitHub publishes no digest (update_check.hpp).
    sums_file = out / 'SHA256SUMS.txt'
    sums = {l[66:]: l for l in (sums_file.read_text(encoding='utf-8').splitlines() if sums_file.exists() else []) if len(l) > 66}
    sums[z.name] = hashlib.sha256(z.read_bytes()).hexdigest() + '  ' + z.name
    sums_file.write_text(''.join(sums[k] + '\n' for k in sorted(sums)), encoding='utf-8')
    print(f'package: {z} ({z.stat().st_size // 1024} KiB), {sum(1 for f in d.rglob("*") if f.is_file())} files; '
          f'INTERNAL={INTERNAL or bool(missing)}; missing license texts: {missing or "none"}')

# Zig ships libc headers and sources for every target in lib/libc; the module build only targets
# x86_64-windows-gnu (mingw-w64) or x86_64-linux-gnu (glibc), so the package keeps that target's
# trees and drops the rest (filtered while copying; the downloaded Zig stays unmodified).
# Every license text Zig ships is kept, including those of dropped trees.
ZIG_LIBC_KEEP = {'glibc'} if LINUX else {'mingw'}
ZIG_MINGW_DROP = {'lib32', 'libarm32'}   # import-library .def files for x86 and 32-bit ARM (x86_64 uses lib64, lib-common)

def zig_header_dir_kept(name):
    """lib/libc/include/<arch>-<os>-<abi> (or generic-<libc>): headers for x86_64 Linux glibc / Windows."""
    if name.startswith('generic-'): return LINUX and name == 'generic-glibc'
    arch, osn, abi = name.split('-', 2)
    return arch in ('any', 'x86', 'x86_64') and osn in ('any', 'linux' if LINUX else 'windows') and abi in ('any', 'gnu')

def zig_license(name):
    return name.upper().startswith(('LICENSE', 'COPYING'))

def copy_zig(src, dst):
    def ignore(dirpath, names):
        rel = Path(dirpath).relative_to(src).as_posix()
        if rel == 'lib/libc': return [n for n in names if (src / rel / n).is_dir() and n not in ZIG_LIBC_KEEP and n != 'include']
        if rel == 'lib/libc/include': return [n for n in names if not zig_header_dir_kept(n)]
        if rel == 'lib/libc/mingw': return [n for n in names if n in ZIG_MINGW_DROP]
        return []
    shutil.copytree(src, dst, symlinks=True, ignore=ignore)
    for f in src.rglob('*'):   # license texts inside dropped trees
        if f.is_file() and zig_license(f.name) and not (dst / f.relative_to(src)).exists():
            (dst / f.relative_to(src)).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(f, dst / f.relative_to(src))
    lic = sorted(f.relative_to(src) for f in src.rglob('*') if f.is_file() and zig_license(f.name))
    assert all((dst / f).is_file() for f in lic)
    print(f'zig: {sum(1 for f in dst.rglob("*") if f.is_file())} of {sum(1 for f in src.rglob("*") if f.is_file())} files '
          f'(libc trees for x86_64-{"linux" if LINUX else "windows"}-gnu), {len(lic)} license texts kept')

def glibc_needed(elf):
    """Newest GLIBC_x.y symbol version the ELF imports (it depends on the build machine's distro)."""
    import re, subprocess
    out = subprocess.run(['objdump', '-T', str(elf)], capture_output=True, text=True, check=True).stdout
    return max(set(re.findall(r'GLIBC_(\d+(?:\.\d+)+)', out)), key=lambda v: tuple(map(int, v.split('.'))))

def scan(d):
    files = [f for f in d.rglob('*') if f.is_file()]
    forbidden = tuple(x for x in FORBIDDEN if not (PERSONAL and x == 'hangtime_game'))
    bad = [f for f in files if any(x in str(f.relative_to(d)).lower() for x in forbidden)]
    assert not bad, f'forbidden files in package: {bad}'
    if not hangtime_paths.rom_path().exists():
        print('scan: ROM not available here, ROM-window check skipped'); return
    rom = hangtime_paths.rom_bytes()   # big-endian, SHA-256 checked
    random.seed(0x4E42)
    windows = []
    while len(windows) < 8192:
        o = random.randrange(0x1000, 0x79000 - 16, 4)
        w = rom[o:o + 16]
        if w.count(0) > 6 or len(set(w)) < 6: continue    # skip zero/padding-like windows
        windows.append(w); windows.append(b''.join(w[i:i + 4][::-1] for i in range(0, 16, 4)))
    scanned = [f for f in files if 'tools' not in f.relative_to(d).parts[:1] or f.name in ('N64Recomp' + EXE, 'RSPRecomp' + EXE)]
    # tools/zig holds files of the unmodified Zig 0.17.0 release (archive SHA-256 checked when downloaded); not scanned file by file.
    for f in scanned:
        data = f.read_bytes()
        hits = sum(1 for w in windows if w in data)
        assert hits == 0, f'{f.name} contains {hits} ROM code windows'
    print(f'scan: {len(files)} files, no forbidden names; {len(scanned)} scanned for ROM code windows, none found ({len(windows)} probes)')

if __name__ == '__main__':
    main()
