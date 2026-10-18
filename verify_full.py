"""Smoke test of the whole-program executable.

Default: original startup through the title menu (scripted Start presses, ~20 s).
--match: frame-scripted input through player select and SELECT TEAMS; the original team-select
timer locks the squads, then the matchup screen, tip-off and >= 90 s of a live match (~185 s).
--widescreen (with any mode): the same run in the optional true-16:9 presentation
(full_runtime/widescreen.cpp); additionally checks that it was active, never fell back to the
original display list, and rendered live-play frames. Logs/captures get a _ws suffix.
--full-match: the same input plus Shoot (C-Down) presses that lock in the team without the
timer, then a complete match: four quarters, the quarter breaks
(coaching tips), halftime stats and substitutions, END GAME STATS and FINAL RESULTS (~13 min).

Input is delivered through the normal controller callback; screenshots are of the real window.
Checks on the run log:
  - the process stays alive for the whole run, no FAIL / CRASH lines (fail-closed stubs,
    process-context and runtime errors abort loudly),
  - the original render loop is live (>= 50 display lists per 60 VI after start-up),
  - the game unblanked the VI with its own mode (h_start != 0) and swaps both framebuffers,
  - --full-match: the game's own period counter (PROGRESS period=, a read-only observation of
    0x800A6662, see FULL_GAME.md) advances 0 -> 1 -> 2 -> 3 -> 4 in order, the team scores
    (0x800A6688/8A) never decrease during the match and the game keeps rendering its result
    screens for >= 30 s after the final period.
Screenshots (captures/full_*.png) are game content: local only, git-ignored. Inspect them; the
2026-10-17 baselines show: one-controller notice, Williams/Midway logos, title menu, player select
(default); team select, TONIGHT'S MATCHUP, tip-off, live play with a running clock and score
(--match); additionally COACHING TIPS between quarters, 1ST HALF STATS, SUBSTITUTIONS, 3rd/4th
quarter play, END GAME STATS and FINAL RESULTS with per-quarter scores (--full-match).
"""
import re, subprocess, sys
from pathlib import Path

P = Path(__file__).resolve().parent
for _d in ('test_logs', 'captures'): (P / _d).mkdir(exist_ok=True)   # git-ignored; absent in a fresh clone
exe = P / 'full_runtime/build/Release/hangtime.exe'
assert exe.exists(), 'run build_full.py first'
full = '--full-match' in sys.argv
match = full or '--match' in sys.argv
wide = '--widescreen' in sys.argv   # the same run with the optional 16:9 presentation (widescreen.cpp)
# Test windows open without keyboard focus and ignore the physical keyboard (typing elsewhere
# cannot leak in). Menu presses are gated on the game's mode word (m<mode>+<polls>, first entry;
# full_runtime/main.cpp): screen durations vary between runs, fixed VI times once landed a press on
# the wrong screen. Modes: 1 boot/attract, 12 title menu, 11 player select, 2 CHOOSE OPTION + team
# select, 3 matchup + quarters, 4 quarter breaks/stats.
isolate = '--background --no-keyboard '
match_presses = isolate + ('--press-frame m1+200..2000/45:START --press-frame m12+40:START --press-frame m12+120:START '
                           '--press-frame m11+40:START --press-frame m11+120:START --press-frame m2+150:DR --press-frame m2+240:A')
if full:
    # Team chosen with Shoot (C-Down in the game's default button table, 0x80035FE0).
    name, shots, timeout = 'full_fullmatch', tuple(range(30, 811, 30)), 900
    args = match_presses + ' --press-frame m2+500..1100/90:CD'
elif match:   # no Shoot: the original team-select timer locks the squad
    name, shots, timeout = 'full_match', (30, 75, 85, 120, 150, 185), 300
    args = match_presses
else:
    name, shots, timeout = 'full', (4, 9, 14, 20), 180
    args = isolate + '--press-frame m1+200..2000/45:START --press-frame m12+40:START --press-frame m12+120:START'
if wide: name, args = name + '_ws', args + ' --widescreen'
if match: args += ' --wav ' + str(P / f'captures/{name}.wav')
# Every VI's presented origin (perf.cpp 'S' events). PROGRESS lines sample the origin only every
# 60th VI: with the game swapping on every VI that always lands on the same buffer once no VI is
# missed, so the both-framebuffers check reads every VI instead.
args += ' --perf ' + str(P / f'captures/{name}_perf.csv')
log = P / f'test_logs/{name}_smoke'
cap = P / f'captures/{name}'
r = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(P / 'full_runtime/capture_window.ps1'),
                    '-Shots', ','.join(map(str, shots)), '-OutPrefix', str(cap), '-GameArgs', args,
                    '-Log', str(log)], cwd=P, capture_output=True, text=True, timeout=timeout)
print(r.stdout.strip().splitlines()[-1] if full and r.stdout.strip() else r.stdout.strip())
text = (P / f'test_logs/{name}_smoke.err.txt').read_text(errors='replace')
assert r.returncode == 0, 'executable exited early:\n' + r.stdout[-500:] + text[-3000:]
assert not re.search(r'^(FAIL|CRASH)', text, re.M), text[-3000:]
prog = [(int(v), int(d), int(o, 16), int(h, 16)) for v, d, o, h in re.findall(r'PROGRESS vi=(\d+) display_lists=(\d+) origin=([0-9A-F]+) hstart=([0-9A-F]+)', text)]
late = [p for p in prog if p[0] >= 180]
assert len(late) >= (700 if full else 150 if match else 10), f'too few progress samples: {len(prog)}'
rates = [b[1] - a[1] for a, b in zip(late, late[1:])]
assert min(rates) >= 50, f'render loop stalled: display lists per 60 VI = {rates}'
assert all(p[3] != 0 for p in late), 'VI still blanked'
import perf_report
screens = [(b, a) for k, t, a, b in perf_report.load(P / f'captures/{name}_perf.csv') if k == 'S']
origins = {a & 0x7FFF000 for b, a in screens if b >= 180}
assert origins >= {0x3B5000, 0x3DA000}, f'both original framebuffers not presented (origins seen {sorted(map(hex, origins))})'
for t in shots: assert (P / f'captures/{name}_{t}s.png').exists()
if wide:   # the rewriter never fell back to the original list; the view clamp ran in live play
    assert 'WIDESCREEN ratio=' in text and not re.search(r'^WIDESCREEN: ', text, re.M), 'widescreen not active or display list overflow'
    if match: assert re.search(r'^WSVIEW .* camx=(?!0 )', text, re.M), 'no live-play widescreen frames'
if full:   # the game's own period counter and scores (read-only observations, FULL_GAME.md)
    state = [(int(v), int(q), int(a), int(b)) for v, q, a, b in re.findall(r'PROGRESS vi=(\d+) .* period=(-?\d+) score=(-?\d+)-(-?\d+)', text)]
    assert len(state) == len(prog), 'PROGRESS lines without period/score (rebuild hangtime.exe)'
    tip = next(i for i, s in enumerate(state) if s[0] > 1440)                    # after team select
    stop = next((i for i in range(tip + 1, len(state)) if state[i][1] < state[i - 1][1]), len(state))  # next match init
    game = state[tip:stop]
    periods = [s[1] for s in game]
    assert all(b - a in (0, 1) for a, b in zip(periods, periods[1:])) and set(periods) >= {0, 1, 2, 3, 4}, f'period sequence {sorted(set(periods))}'
    end = periods.index(max(periods))          # 4 after regulation; 5-7 when overtime was played
    assert all(b[2] >= a[2] and b[3] >= a[3] for a, b in zip(game[:end + 1], game[1:end + 1])), 'a team score decreased during the match'
    final = game[end]
    assert final[2] + final[3] > 0 and final[2] != final[3], f'implausible final score {final[2]}-{final[3]}'
    assert prog[-1][0] - final[0] >= 30 * 60, f'run ended {prog[-1][0] - final[0]} VI after the final period'
    starts = {q: next(s[0] for s in game if s[1] == q) for q in sorted(set(periods))}
    print('periods completed at VI ' + ', '.join(f'{q}:{v}' for q, v in starts.items() if q) + f'; final score {final[2]}-{final[3]}')
if match:   # the audio actually handed to SDL (local only: game content)
    wav = P / f'captures/{name}.wav'
    a = subprocess.run([sys.executable, str(P / 'full_runtime/analyze_wav.py'), str(wav), '--min-audible', '400' if full else '60'],
                       capture_output=True, text=True)
    (P / f'test_logs/{name}_audio.txt').write_text(a.stdout + a.stderr)
    print(a.stdout.strip().splitlines()[-1] if a.stdout.strip() else a.stderr)
    assert a.returncode == 0, a.stdout + a.stderr
what = ('a complete match: four quarters, halftime and the result screens' if full else
        'scripted menus, team select, tip-off and a live match' if match else 'boot to title/player select')
what += ' in 16:9 widescreen' if wide else ''
print(f'PASS: whole-program {what}; {late[-1][0]} VI; display lists per second {min(rates)}-{max(rates)}; '
      f'VI unblanked; screenshots captures/{name}_*.png')
