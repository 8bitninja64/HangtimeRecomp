"""Updates in place from a GitHub release (full_runtime/update_check.hpp), against a local fake
GitHub: a threaded HTTP server on 127.0.0.1 answers /repos/test/hangtime/releases/latest with a
release v9.9.9 whose asset is the Windows release package in dist/ (package_release.py) and its
SHA-256 "digest", as the GitHub API publishes it; HANGTIME_UPDATE_API points the program at it.

The installed copy is the PERSONAL package (game module built from the ROM, no build kit), as an
owner's test install is; the release package carries the first-start build kit, so the update must
also retire the module and the new version must rebuild it from the ROM.
  badsum   the digest is wrong: --install-update refuses ("damaged"), nothing in the install changes
  cli      --install-update installs: setup/ and tools/ arrive, the module is retired, hangtime.ini
           and saves/ are byte-identical, replaced files are *.old-update; the next start removes them
  ui       the launcher, driven by HANGTIME_LAUNCHER_SCRIPT: the update tile opens the UPDATE page,
           Install and restart installs, the old process exits and a new one (another PID) runs
           the installed executable with its launcher
  play     the updated install (from ui) started on the ROM: it rebuilds the game module from the
           ROM with the new kit and the game boots (MODE lines), saves folder still in place
Needs dist/HangtimeRecomp-win64-*.zip (release or -INTERNAL) and -PERSONAL.zip from the current build
(python package_release.py; python package_release.py --personal). Work folder:
..\\experiments\\test_update (local only).
Usage: verify_update.py [badsum|cli|ui|play ...]
"""
import hashlib, http.server, json, os, shutil, subprocess, sys, threading, time, zipfile
from pathlib import Path

P = Path(__file__).resolve().parent
WORK = P.parent / 'experiments' / 'test_update'
DIST = P / 'dist'
ROM = Path(os.environ.get('HANGTIME_ROM', P.parent / 'rom' / 'NBA Hangtime (USA).z64'))
NEW = sorted(z for z in DIST.glob('HangtimeRecomp-win64-*.zip') if 'PERSONAL' not in z.name)   # a release (or INTERNAL) package
OLD = sorted(DIST.glob('HangtimeRecomp-win64-*-PERSONAL.zip'))
assert NEW and OLD, 'build both packages first (package_release.py, package_release.py --personal)'
NEW, OLD = NEW[-1], OLD[-1]
INI = 'rom={rom}\nupdate_repo=test/hangtime\nkeyboard_player=3\npad_order=1324\ncontrollers=4\nskip_launcher=0\n'


def serve(digest):
    root = WORK / 'server'
    shutil.rmtree(root, ignore_errors=True)
    (root / 'repos/test/hangtime/releases').mkdir(parents=True)
    (root / 'dl').mkdir()
    shutil.copy2(NEW, root / 'dl' / NEW.name)
    handler = lambda *a, **k: http.server.SimpleHTTPRequestHandler(*a, directory=str(root), **k)
    http.server.SimpleHTTPRequestHandler.log_message = lambda *a: None
    srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), handler)
    base = f'http://127.0.0.1:{srv.server_port}'
    release = {'tag_name': 'v9.9.9', 'html_url': 'https://github.com/test/hangtime/releases/tag/v9.9.9',
               'body': 'Test release served by verify_update.py.',
               'assets': [{'name': NEW.name, 'size': NEW.stat().st_size, 'browser_download_url': f'{base}/dl/{NEW.name}',
                           'digest': 'sha256:' + digest}]}
    (root / 'repos/test/hangtime/releases/latest').write_text(json.dumps(release), encoding='utf-8')
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv, base


def install():
    d = WORK / 'install'
    shutil.rmtree(d, ignore_errors=True)
    with zipfile.ZipFile(OLD) as z: z.extractall(d)
    root = next(p for p in d.iterdir() if (p / 'hangtime.exe').exists())
    (root / 'hangtime.ini').write_text(INI.format(rom=ROM), encoding='ascii')
    (root / 'saves').mkdir(exist_ok=True)
    (root / 'saves' / 'controller_pak_1.mpk').write_bytes(bytes(range(256)) * 128)
    return root


def snapshot(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in root.rglob('*') if p.is_file()}


def env(base):
    e = dict(os.environ, HANGTIME_UPDATE_API=base)
    e.pop('HANGTIME_LAUNCHER_SCRIPT', None)
    return e


def pids():
    out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq hangtime.exe', '/FO', 'CSV', '/NH'], capture_output=True, text=True).stdout
    return {int(l.split('","')[1]) for l in out.splitlines() if l.startswith('"hangtime.exe"')}


def kill_all():
    subprocess.run(['taskkill', '/F', '/IM', 'hangtime.exe'], capture_output=True)
    time.sleep(1)


def check_installed(root, before, cleaned=False):   # cleaned: a start since then removed the backups
    after = snapshot(root)
    assert (root / 'setup' / 'audio_rsp.toml').exists() and (root / 'tools' / 'zig' / 'zig.exe').exists(), 'build kit not installed'
    assert not (root / 'hangtime_game.dll').exists() and (cleaned or (root / 'hangtime_game.dll.old-update').exists()), 'module not retired'
    assert after.get('saves\\controller_pak_1.mpk') == before.get('saves\\controller_pak_1.mpk'), 'the Controller Pak changed'
    ini = set((root / 'hangtime.ini').read_text(encoding='utf-8').splitlines())   # a launcher rewrites it in its own layout
    missing = [l for l in INI.format(rom=ROM).splitlines() if l not in ini]
    assert not missing, f'settings lost: {missing}'
    with zipfile.ZipFile(NEW) as z:
        top = z.namelist()[0].split('/')[0]
        exe_new = hashlib.sha256(z.read(f'{top}/hangtime.exe')).hexdigest()
    assert after['hangtime.exe'] == exe_new, 'hangtime.exe not replaced'
    olds = [k for k in after if k.endswith('.old-update')]
    return olds


def run_badsum():
    root = install(); before = snapshot(root)
    srv, base = serve('0' * 64)
    r = subprocess.run([str(root / 'hangtime.exe'), '--install-update', 'test/hangtime'], env=env(base), capture_output=True, text=True, timeout=600)
    srv.shutdown()
    assert r.returncode == 2 and 'damaged' in r.stdout, r.stdout + r.stderr[-1500:]
    after = {k: v for k, v in snapshot(root).items() if not k.startswith('.update')}
    assert after == before, 'the install changed although the download was refused'
    print('badsum: refused (' + r.stdout.strip().split('error=')[-1] + '); install unchanged', flush=True)


def run_cli():
    root = install(); before = snapshot(root)
    srv, base = serve(hashlib.sha256(NEW.read_bytes()).hexdigest())
    t0 = time.time()
    r = subprocess.run([str(root / 'hangtime.exe'), '--install-update', 'test/hangtime'], env=env(base), capture_output=True, text=True, timeout=900)
    srv.shutdown()
    assert r.returncode == 0 and 'result=installed' in r.stdout, r.stdout + r.stderr[-2000:]
    olds = check_installed(root, before)
    assert olds and (root / '.update' / 'cleanup.txt').exists(), 'no backups listed for cleanup'
    # the next start removes the backups (the launcher opens; closed after a few seconds)
    p = subprocess.Popen([str(root / 'hangtime.exe')], env=env(base))
    time.sleep(6); kill_all()
    left = [k for k in snapshot(root) if k.endswith('.old-update')]
    assert not left and not (root / '.update').exists(), f'backups not removed at the next start: {left[:5]}'
    print(f'cli: installed in {time.time() - t0:.0f} s; {len(olds)} replaced files backed up, removed at the next start; '
          f'module retired; hangtime.ini and saves unchanged', flush=True)


def run_ui():
    kill_all()
    root = install(); before = snapshot(root)
    srv, base = serve(hashlib.sha256(NEW.read_bytes()).hexdigest())
    e = env(base); e['HANGTIME_LAUNCHER_SCRIPT'] = 'down,down,right,ok,ok'   # update tile, then Install and restart
    log = WORK / 'ui_first.err.txt'
    with open(log, 'w') as f:
        first = subprocess.Popen([str(root / 'hangtime.exe')], env=e, stderr=f)
    deadline = time.time() + 600
    restarted = set()
    while time.time() < deadline:
        time.sleep(2)
        if first.poll() is not None:
            restarted = pids() - {first.pid}
            if restarted: break
    text = log.read_text(errors='replace')
    srv.shutdown()
    assert first.poll() is not None, 'the launcher did not exit after installing:\n' + text[-2000:]
    assert 'UPDATE installed v9.9.9' in text, text[-2000:]
    assert restarted, 'no new process after the update'
    time.sleep(5)
    assert pids() == restarted, 'the restarted program did not stay open (launcher)'
    kill_all()
    left = check_installed(root, before, cleaned=True)
    print(f'ui: tile -> UPDATE page -> Install and restart; installed; old process exited, new process {sorted(restarted)} '
          f'opened the launcher and removed {"all" if not left else len(left)} backups; hangtime.ini and saves unchanged', flush=True)
    return root


def run_play():
    root = next(p for p in (WORK / 'install').iterdir() if (p / 'hangtime.exe').exists())
    assert (root / 'setup').exists() and not (root / 'hangtime_game.dll').exists(), 'run the ui case first'
    log = WORK / 'play.err.txt'
    t0 = time.time()
    with open(log, 'w') as f:
        p = subprocess.Popen([str(root / 'hangtime.exe'), str(ROM), '--background', '--no-keyboard',
                              '--press-frame', 'm1+200..2000/45:START'], env=env('http://127.0.0.1:9'), stderr=f)
    ok = False
    while time.time() - t0 < 1500 and p.poll() is None:
        time.sleep(5)
        text = log.read_text(errors='replace')
        if 'mode=12' in text: ok = True; break
    kill_all()
    text = log.read_text(errors='replace')
    assert 'GAME MODULE build ok' in text and ok, text[-2500:]
    assert (root / 'hangtime_game.dll').exists() and (root / 'saves' / 'controller_pak_1.mpk').exists()
    built = [l for l in text.splitlines() if l.startswith('GAME MODULE build ok')][0]
    print(f'play: the updated program rebuilt the game module from the ROM ({built}) and the game ran to the menus (mode 12)', flush=True)


WORK.mkdir(parents=True, exist_ok=True)
CASES = {'badsum': run_badsum, 'cli': run_cli, 'ui': run_ui, 'play': run_play}
for name in (sys.argv[1:] or CASES):
    CASES[name]()
if not sys.argv[1:]: print('PASS: update in place: bad checksum refused; package installed with ROM, settings and saves kept; '
      'launcher install restarts into the new version; the new version rebuilt the game module and played')
