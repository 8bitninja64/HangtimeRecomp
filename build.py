"""One-command build from a fresh clone (Windows and Linux): runtime + game module from your ROM.

usage: python build.py [--rom PATH] [--jobs N] [--clean] [--runtime-only]

--runtime-only (CI, no ROM): steps 2, 3 and the runtime of step 5; no game module.

  1. checks your ROM (NBA Hangtime (USA); SHA-256; .z64/.n64/.v64) - see hangtime_paths.py for
     where it is looked for (--rom, $HANGTIME_ROM, rom/, ../rom)
  2. fetches the pinned upstream sources (git submodules in external/) if they are missing
  3. builds the tools and libraries into build/deps/: N64Recomp + RSPRecomp, RT64 (static)
  4. partitions and recompiles the game code from your ROM (full_inventory.py, prepare_full.py,
     N64Recomp, RSPRecomp) into full_generated/ and audio_rsp_generated.cpp - git-ignored, never
     distributed (RELEASE_LEGAL.md)
  5. builds the runtime and the game module:
       Windows: full_runtime/build/Release/hangtime.exe + hangtime_game.dll (Visual Studio 2022)
       Linux:   build/linux/hangtime + hangtime_game.so (Ninja; clang or gcc)
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

    # 3. Tools and libraries.
    if WIN:
        gen = ['-G', 'Visual Studio 17 2022', '-A', 'x64']
        build = lambda d, *targets: [cmake, '--build', d, '--config', 'Release', '--parallel', JOBS, *(a for t in targets for a in ('--target', t))]
        recomp_bin = DEPS / 'n64recomp' / 'Release'
    else:
        ninja = tool('ninja', 'Ninja')
        cc = env.get('CC') or shutil.which('clang') or shutil.which('gcc')
        cxx = env.get('CXX') or shutil.which('clang++') or shutil.which('g++')
        if not cc or not cxx: sys.exit('FAIL: no C/C++ compiler (clang or gcc; see BUILDING.md)')
        gen = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', f'-DCMAKE_C_COMPILER={cc}', f'-DCMAKE_CXX_COMPILER={cxx}',
               f'-DCMAKE_MAKE_PROGRAM={ninja}']
        build = lambda d, *targets: [cmake, '--build', d, '--parallel', JOBS, *(a for t in targets for a in ('--target', t))]
        recomp_bin = DEPS / 'n64recomp'
    exe = '.exe' if WIN else ''
    if not (recomp_bin / f'N64Recomp{exe}').exists() or not (recomp_bin / f'RSPRecomp{exe}').exists():
        run('n64recomp-cfg', [cmake, '-S', n64recomp, '-B', DEPS / 'n64recomp', *gen])
        run('n64recomp', build(DEPS / 'n64recomp', 'N64RecompCLI', 'RSPRecomp'))
    rt64_build = DEPS / 'rt64'
    rt64_lib = rt64_build / ('Release/rt64.lib' if WIN else 'rt64.a')
    if not rt64_lib.exists():
        rt64_opts = ['-DRT64_STATIC=ON'] + ([] if WIN else ['-DRT64_SDL_WINDOW_VULKAN=ON'])
        run('rt64-cfg', [cmake, '-S', rt64, '-B', rt64_build, *gen, *rt64_opts])
        run('rt64', build(rt64_build, 'rt64'), timeout=7200)

    # 4. Game code from the ROM (build products only; git-ignored).
    if not runtime_only:
        (P / 'full_generated').mkdir(exist_ok=True)
        run('inventory', [sys.executable, P / 'full_inventory.py'])
        run('config', [sys.executable, P / 'prepare_full.py'])
        run('recompile', [recomp_bin / f'N64Recomp{exe}', P / 'full.toml'])
        run('audio-microcode', [recomp_bin / f'RSPRecomp{exe}', P / 'full_generated' / 'audio_rsp.toml'])

    # 5. Runtime + game module.
    out = P / 'full_runtime' / 'build' if WIN else P / 'build' / 'linux'
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
