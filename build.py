"""One-command build from a fresh clone (Windows and Linux): runtime + game module from your ROM.

usage: python build.py [--rom PATH] [--jobs N] [--clean] [--runtime-only] [--zig PATH]

--runtime-only (CI, no ROM): steps 2, 3 and the runtime of step 5; no game module.
--zig PATH (Linux; or $ZIG): build with the Zig 0.17.0 toolchain for glibc 2.31 and a static C++
  runtime, so the binaries also run on older distributions and SteamOS (release packages). Without
  it the system clang/gcc is used (glibc_compat.h pins a few symbols; the binaries need roughly the
  build machine's glibc).

  1. checks your ROM (NBA Hangtime (USA); SHA-256; .z64/.n64/.v64) - see hangtime_paths.py for
     where it is looked for (--rom, $HANGTIME_ROM, rom/, ../rom)
  2. fetches the pinned upstream sources (git submodules in external/) if they are missing
  3. builds the tools and libraries into build/deps/: N64Recomp + RSPRecomp, RT64 (static)
  4. partitions and recompiles the game code from your ROM (full_inventory.py, prepare_full.py,
     N64Recomp, RSPRecomp) into full_generated/ and audio_rsp_generated.cpp - git-ignored, never
     distributed (RELEASE_LEGAL.md)
  5. builds the runtime and the game module:
       Windows: full_runtime/build/Release/hangtime.exe + hangtime_game.dll (Visual Studio 2022)
       Linux:   build/linux/hangtime + hangtime_game.so (Ninja; clang, gcc or Zig)
Requirements: BUILDING.md. Nothing is downloaded except the git submodules.
"""
import os, shutil, subprocess, sys, time
from pathlib import Path

P = Path(__file__).resolve().parent
sys.path.insert(0, str(P))
import hangtime_paths

WIN = sys.platform == 'win32'
args = sys.argv[1:]
def opt(name, default=None):
    return args[args.index(name) + 1] if name in args else default
if opt('--rom'): os.environ['HANGTIME_ROM'] = str(Path(opt('--rom')).resolve())
JOBS = opt('--jobs', str(os.cpu_count() or 4))
DEPS = P / 'build' / 'deps'
env = {k: v for k, v in os.environ.items() if k != 'NoDefaultCurrentDirectoryInExePath'}
LOGS = P / 'build' / 'logs'
ZIG_GLIBC = '2.31'   # the Steam Runtime 3 ("sniper") baseline

def tool(name, hint):
    path = shutil.which(name) or (r'C:\Program Files\CMake\bin\cmake.exe' if WIN and name == 'cmake' and
                                  Path(r'C:\Program Files\CMake\bin\cmake.exe').exists() else None)
    if not path: sys.exit(f'FAIL: {name} not found ({hint}; see BUILDING.md)')
    return path

def run(step, cmd, cwd=P, **kw):
    LOGS.mkdir(parents=True, exist_ok=True)
    t0 = time.time()
    r = subprocess.run([str(c) for c in cmd], cwd=cwd, env=env, capture_output=True, text=True, **kw)
    (LOGS / f'{step}.txt').write_text(r.stdout + r.stderr, encoding='utf-8')
    print(f'{step:<16} {"ok" if r.returncode == 0 else "FAILED"} ({time.time() - t0:.0f} s)', flush=True)
    if r.returncode:
        print((r.stdout + r.stderr)[-4000:]); sys.exit(f'FAIL: {step} (full log: {LOGS / (step + ".txt")})')

def zig_toolchain(zig):
    """Compiler wrappers for CMake: zig cc/c++ for x86_64-linux-gnu.<ZIG_GLIBC>, plus ar/ranlib.
    The C++ wrapper drops -nostdlib from compile-only commands: clang ignores it there, zig would
    drop the libc++ headers (nativefiledialog-extended passes it)."""
    w = DEPS / 'zigwrap'; w.mkdir(parents=True, exist_ok=True)
    target = f'x86_64-linux-gnu.{ZIG_GLIBC}'
    scripts = {
        'cc': f'#!/bin/sh\nexec "{zig}" cc -target {target} "$@"\n',
        'c++': ('#!/bin/sh\nc=0\nfor a in "$@"; do [ "$a" = "-c" ] && c=1; done\nn=$#\n'
                'for a in "$@"; do\n    if [ $c = 1 ] && [ "$a" = "-nostdlib" ]; then :; else set -- "$@" "$a"; fi\ndone\n'
                f'shift $n\nexec "{zig}" c++ -target {target} "$@"\n'),
        'ar': f'#!/bin/sh\nexec "{zig}" ar "$@"\n',
        'ranlib': f'#!/bin/sh\nexec "{zig}" ranlib "$@"\n',
    }
    for name, text in scripts.items():
        f = w / name
        f.write_text(text, newline='\n'); f.chmod(0o755)
    env['ZIG_GLOBAL_CACHE_DIR'] = env['ZIG_LOCAL_CACHE_DIR'] = str(DEPS / 'zig-cache')
    sdl2 = next((d for d in ('/usr/lib/x86_64-linux-gnu/cmake/SDL2', '/usr/lib64/cmake/SDL2', '/usr/lib/cmake/SDL2')
                 if Path(d, 'SDL2Config.cmake').exists() or Path(d, 'sdl2-config.cmake').exists()), None)
    if not sdl2: sys.exit('FAIL: SDL2 development files not found (libsdl2-dev; see BUILDING.md)')
    # System headers come after zig's own libc/libc++ headers; system libraries for linking only
    # (the player's system provides SDL2, GTK 3 and the Vulkan loader at run time).
    sys_inc = '-idirafter /usr/include -idirafter /usr/include/x86_64-linux-gnu'
    link = '-L/usr/lib/x86_64-linux-gnu -L/usr/lib64 -L/usr/lib -Wl,--allow-shlib-undefined'
    return [f'-DCMAKE_C_COMPILER={w / "cc"}', f'-DCMAKE_CXX_COMPILER={w / "c++"}', f'-DCMAKE_AR={w / "ar"}',
            f'-DCMAKE_RANLIB={w / "ranlib"}', '-DCMAKE_LIBRARY_ARCHITECTURE=x86_64-linux-gnu', f'-DSDL2_DIR={sdl2}',
            f'-DCMAKE_C_FLAGS={sys_inc}', f'-DCMAKE_CXX_FLAGS={sys_inc}', f'-DCMAKE_EXE_LINKER_FLAGS={link}',
            f'-DCMAKE_SHARED_LINKER_FLAGS={link}', f'-DCMAKE_MODULE_LINKER_FLAGS={link}']

def main():
    cmake = tool('cmake', 'CMake 3.20 or newer')
    if '--clean' in args: shutil.rmtree(P / 'build', ignore_errors=True)
    runtime_only = '--runtime-only' in args
    # 1. The ROM (before anything slow, so a wrong file fails fast).
    if not runtime_only:
        hangtime_paths.rom_bytes()
        print(f'rom              ok ({hangtime_paths.rom_path().name}, SHA-256 verified)')

    # 2. Upstream sources: the submodules of a fresh clone (the development layout has them beside).
    n64recomp, rt64 = hangtime_paths.n64recomp_dir(), hangtime_paths.rt64_dir()
    if not (n64recomp / 'CMakeLists.txt').exists() or not (rt64 / 'CMakeLists.txt').exists():
        run('submodules', [tool('git', 'Git'), 'submodule', 'update', '--init', '--recursive', '--depth', '1'])
        n64recomp, rt64 = hangtime_paths.n64recomp_dir(), hangtime_paths.rt64_dir()
    for d in (n64recomp, rt64):
        if not (d / 'CMakeLists.txt').exists(): sys.exit(f'FAIL: {d} is empty (git submodule update --init --recursive)')

    # 3. Tools and libraries. A changed toolchain profile rebuilds the dependencies.
    compat = P / 'full_runtime' / 'glibc_compat.h'
    zig = None if WIN else (opt('--zig') or env.get('ZIG'))
    if WIN:
        gen = ['-G', 'Visual Studio 17 2022', '-A', 'x64']
        build = lambda d, *targets: [cmake, '--build', d, '--config', 'Release', '--parallel', JOBS, *(a for t in targets for a in ('--target', t))]
        recomp_bin = DEPS / 'n64recomp' / 'Release'
        profile, tool_opts, rt64_opts = 'windows-1', [], []
    else:
        ninja = tool('ninja', 'Ninja')
        gen = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', f'-DCMAKE_MAKE_PROGRAM={ninja}']
        if zig:
            zig = str(Path(zig).resolve())
            if not Path(zig).exists(): sys.exit(f'FAIL: Zig not found at {zig}')
            gen += zig_toolchain(zig)
            profile, tool_opts, rt64_opts = f'linux-zig-glibc{ZIG_GLIBC}-1', [], []
        else:
            cc = env.get('CC') or shutil.which('clang') or shutil.which('gcc')
            cxx = env.get('CXX') or shutil.which('clang++') or shutil.which('g++')
            if not cc or not cxx: sys.exit('FAIL: no C/C++ compiler (clang or gcc; see BUILDING.md)')
            gen += [f'-DCMAKE_C_COMPILER={cc}', f'-DCMAKE_CXX_COMPILER={cxx}']
            profile = 'linux-compat-1'
            tool_opts = ['-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc']
            rt64_opts = [f'-DCMAKE_C_FLAGS=-include {compat}', f'-DCMAKE_CXX_FLAGS=-include {compat}']
        build = lambda d, *targets: [cmake, '--build', d, '--parallel', JOBS, *(a for t in targets for a in ('--target', t))]
        recomp_bin = DEPS / 'n64recomp'
    exe = '.exe' if WIN else ''
    stamp = DEPS / 'profile.txt'
    out = P / 'full_runtime' / 'build' if WIN else P / 'build' / 'linux'
    if (stamp.read_text().strip() if stamp.exists() else None) != profile:   # also: deps from before the stamp
        for d in (DEPS / 'n64recomp', DEPS / 'rt64') + (() if WIN else (out,)): shutil.rmtree(d, ignore_errors=True)
    if not (recomp_bin / f'N64Recomp{exe}').exists() or not (recomp_bin / f'RSPRecomp{exe}').exists():
        run('n64recomp-cfg', [cmake, '-S', n64recomp, '-B', DEPS / 'n64recomp', *gen, *tool_opts])
        run('n64recomp', build(DEPS / 'n64recomp', 'N64RecompCLI', 'RSPRecomp'))
    rt64_build = DEPS / 'rt64'
    rt64_lib = rt64_build / ('Release/rt64.lib' if WIN else 'rt64.a')
    if not rt64_lib.exists():
        rt64_opts = ['-DRT64_STATIC=ON'] + ([] if WIN else ['-DRT64_SDL_WINDOW_VULKAN=ON']) + rt64_opts
        run('rt64-cfg', [cmake, '-S', rt64, '-B', rt64_build, *gen, *rt64_opts])
        run('rt64', build(rt64_build, 'rt64'), timeout=7200)
    DEPS.mkdir(parents=True, exist_ok=True); stamp.write_text(profile + '\n')

    # 4. Game code from the ROM (build products only; git-ignored).
    if not runtime_only:
        (P / 'full_generated').mkdir(exist_ok=True)
        run('inventory', [sys.executable, P / 'full_inventory.py'])
        run('config', [sys.executable, P / 'prepare_full.py'])
        run('recompile', [recomp_bin / f'N64Recomp{exe}', P / 'full.toml'])
        run('audio-microcode', [recomp_bin / f'RSPRecomp{exe}', P / 'full_generated' / 'audio_rsp.toml'])

    # 5. Runtime + game module.
    run('configure', [cmake, '-S', P / 'full_runtime', '-B', out, *gen, f'-DRT64_BUILD={rt64_build}', f'-DRT64_ROOT={rt64}',
                      f'-DHANGTIME_RUNTIME_ONLY={"ON" if runtime_only else "OFF"}'])
    run('build', build(out), timeout=7200)
    final = out / 'Release' if WIN else out
    if runtime_only:
        print(f'PASS: {final / ("hangtime" + exe)} built (runtime only; no game module without a ROM)'); return
    print(f'PASS: {final / ("hangtime" + exe)} and {final / ("hangtime_game" + (".dll" if WIN else ".so"))} built. '
          f'Run {"hangtime.exe" if WIN else "./hangtime"} from that folder (README.md "Playing").')

if __name__ == '__main__':
    main()
