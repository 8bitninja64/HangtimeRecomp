"""Frame-timing benchmark of the Windows build (perf_report.py on a hangtime --perf log).

usage: python bench_perf.py [--scenario match|fullmatch] [--widescreen] [--occluded] [--tag NAME]
                            [--no-wait] [--profile] [--lock PATH]
                            [--focus] [--fullscreen] [--seconds N] [--game-args '...']

Scenarios use verify_full.py's state-gated input (game mode word, not wall clock):
  match      boot -> title -> player select -> team select (timer) -> matchup -> live play, 185 s
  fullmatch  the same with the Shoot lock-in, four quarters, halftime, results, 840 s
Before each run full_runtime/wait_quiet.ps1 waits for an idle machine (no WSL test, no other
game, total CPU < 15%), since parallel WSL/llvmpipe runs use every core. The window is kept
topmost but unfocused (-Topmost): an occluded window's presentation is throttled by Windows and is
not what a player sees. --occluded leaves it wherever Windows puts it (the old test setup).
Results: captures/perf/<tag>.csv (event log, local only) and test_logs/perf_<tag>.json/.txt.
"""
import json, subprocess, sys
from pathlib import Path
import perf_report

P = Path(__file__).resolve().parent
for _d in ('test_logs', 'captures'): (P / _d).mkdir(exist_ok=True)   # git-ignored; absent in a fresh clone
argv = sys.argv[1:]
scenario = argv[argv.index('--scenario') + 1] if '--scenario' in argv else 'match'
wide = '--widescreen' in argv
tag = argv[argv.index('--tag') + 1] if '--tag' in argv else scenario + ('_ws' if wide else '')
menus = ('--background --no-keyboard --press-frame m1+200..2000/45:START --press-frame m12+40:START --press-frame m12+120:START '
         '--press-frame m11+40:START --press-frame m11+120:START --press-frame m2+150:DR --press-frame m2+240:A')
seconds = int(argv[argv.index('--seconds') + 1]) if '--seconds' in argv else {'match': 185, 'fullmatch': 840}[scenario]
args = menus + (' --press-frame m2+500..1100/90:CD' if scenario == 'fullmatch' else '') + (' --widescreen' if wide else '')
if '--focus' in argv: args += ' --focus'            # the window takes focus (as for a player); test settings still
if '--fullscreen' in argv: args += ' --fullscreen'  # borderless fullscreen
if '--game-args' in argv: args += ' ' + argv[argv.index('--game-args') + 1]
(P / 'captures/perf').mkdir(parents=True, exist_ok=True)
csv = P / f'captures/perf/{tag}.csv'
args += f' --perf {csv} --wav {P / f"captures/perf/{tag}.wav"}'
if '--profile' in argv:   # gfx-thread stack sampling (profile_report.py); perturbs timing: use separate runs
    args += f' --profile-gfx {P / f"captures/perf/{tag}.prof.csv"}'
if '--no-wait' not in argv:
    q = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(P / 'full_runtime/wait_quiet.ps1')],
                       capture_output=True, text=True)
    print(q.stdout.strip(), flush=True)
    assert q.returncode == 0, 'machine not quiet'
lock = Path(argv[argv.index('--lock') + 1]) if '--lock' in argv else None   # tells other sessions a benchmark is running
if lock: lock.write_text(f'bench_perf {tag}', encoding='utf-8')
cmd = ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(P / 'full_runtime/capture_window.ps1'),
       '-Shots', str(seconds), '-OutPrefix', str(P / f'captures/perf/{tag}'), '-Log', str(P / f'test_logs/perf_{tag}'), '-GameArgs', args]
if '--occluded' not in argv: cmd.append('-Topmost')
if '--focus' in argv: cmd.append('-Foreground')
import time
t_start = time.time()
try:
    r = subprocess.run(cmd, cwd=P, capture_output=True, text=True, timeout=seconds + 120)
finally:
    if lock: lock.unlink(missing_ok=True)
assert r.returncode == 0, r.stdout + r.stderr
print(r.stdout.strip(), flush=True)
err = (P / f'test_logs/perf_{tag}.err.txt').read_text(errors='replace')
assert 'FAIL' not in err and 'CRASH' not in err, err[-2000:]
rep = perf_report.analyze(perf_report.load(csv))
rep['scenario'], rep['widescreen'], rep['topmost'] = scenario, wide, '--occluded' not in argv
rep['focus'], rep['fullscreen'] = '--focus' in argv, '--fullscreen' in argv
if '--focus' in argv: rep['foreground_verified'] = 'foreground=True' in r.stdout and 'foreground=False' not in r.stdout
# Was another workload (a WSL test, another game) started while this run was measuring?
ps = subprocess.run(['powershell', '-NoProfile', '-Command', "Get-Process wsl,hangtime -ErrorAction SilentlyContinue | "
                     "ForEach-Object { [DateTimeOffset]::new($_.StartTime).ToUnixTimeSeconds() }"], capture_output=True, text=True)
rep['contaminated'] = any(int(x) > t_start for x in ps.stdout.split() if x.strip().isdigit())
if scenario == 'fullmatch':   # the game's own period counter and scores (as verify_full.py --full-match)
    import re
    state = [(int(q), int(a), int(b)) for q, a, b in re.findall(r'^PROGRESS vi=\d+ .* period=(-?\d+) score=(-?\d+)-(-?\d+)', err, re.M)]
    periods = [q for i, (q, a, b) in enumerate(state) if i == 0 or q != state[i - 1][0]]
    rep['periods_seen'] = periods
    rep['final_score'] = f'{state[-1][1]}-{state[-1][2]}' if state else '?'
    rep['match_complete'] = 4 in periods and periods.index(4) > periods.index(3) if 3 in periods and 4 in periods else False
w = subprocess.run([sys.executable, str(P / 'full_runtime/analyze_wav.py'), str(P / f'captures/perf/{tag}.wav')], capture_output=True, text=True)
rep['audio'] = (w.stdout.strip().splitlines() or ['?'])[-1]
(P / f'test_logs/perf_{tag}.json').write_text(json.dumps(rep, indent=1), encoding='utf-8')
text = '\n'.join(f'{k}: {v}' for k, v in rep.items())
(P / f'test_logs/perf_{tag}.txt').write_text(text + '\n', encoding='utf-8')
print(text)
