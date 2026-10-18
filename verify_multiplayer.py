"""Two-player local multiplayer through two gamepads (SDL virtual game controllers, read through
SDL's GameController API like physical pads; main.cpp "Players 1-4").

Runs (~3.5 min each, state-gated input like verify_input.py):
  headtohead  --pad-order 1324: pad 2 is N64 controller 3 (player 3, the other team). Player 1 goes
              through the menus on pad 1; pad 2 presses START at team select (the game's JOIN IN),
              picks its own team and locks it in with Shoot; in play pad 2 shoots repeatedly.
  teammates   --pad-order 1234: the same with pad 2 as controller 2 (player 1's teammate), idle
              in play (it never shoots).
Checks on the game's own state (read-only, FULL_GAME.md): the pads land on the expected ports;
the joining player's arcade switch word (0x800A65B0 + 2*player, built by 0x80024888) reacts to
pad 2; the game re-enters player select for the join (mode 2 -> 11 -> 2) and reaches live play
(mode 3); the stat records (0x800A6D78 + 0x20*player) show the joined player is human: in the
head-to-head run player 3 takes shots (FGA > 0) only through pad 2; in the teammates run player 2
stays at 0 attempts all quarter (the CPU never plays a human's player), while the CPU-played
players score. Screenshots captures/mp/{run}_*.png are game content: local only.
"""
import re, subprocess, sys
from pathlib import Path

P = Path(__file__).resolve().parent
for _d in ('test_logs', 'captures'): (P / _d).mkdir(exist_ok=True)   # git-ignored; absent in a fresh clone

def menu_p1():
    f = '--pad-press'
    a = [f'{f} m1+{v}:START' for v in range(200, 2000, 45)] + [f'{f} m12+40:START', f'{f} m12+120:START']
    a += [f'{f} m11+40:START', f'{f} m11+120:START']
    a += [f'{f} m2#2+150:LX+', f'{f} m2#2+240:RB'] + [f'{f} m2#2+{v}:A' for v in range(500, 1101, 90)]
    return a

def pad2(shoot):
    f = '--pad-press'
    a = [f'{f} m2+60:START@2', f'{f} m2+100:START@2']                     # JOIN IN at team select
    a += [f'{f} m2#2+180:LX+@2', f'{f} m2#2+270:RB@2'] + [f'{f} m2#2+{v}:A@2' for v in range(530, 1131, 90)]
    if shoot: a += [f'{f} m3+{v}:A@2' for v in range(600, 6000, 60)]       # Shoot in play
    return a

RUNS = {'headtohead': ('1324', 3, True), 'teammates': ('1234', 2, False)}

def run(name):
    order, port, shoot = RUNS[name]
    args = ['--background', '--no-keyboard', '--input-trace', '--virtual-pad', '2', '--pad-order', order] + menu_p1() + pad2(shoot)
    shots = '60,120,170,200'
    r = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(P / 'full_runtime/capture_window.ps1'),
                        '-Shots', shots, '-OutPrefix', str(P / f'captures/mp/{name}'), '-Log', str(P / f'test_logs/mp_{name}'),
                        '-GameArgs', ' '.join(args)], cwd=P, capture_output=True, text=True, timeout=400)
    text = (P / f'test_logs/mp_{name}.err.txt').read_text(errors='replace')
    assert r.returncode == 0, r.stdout[-500:] + text[-2000:]
    assert not re.search(r'^(FAIL|CRASH)', text, re.M), text[-2000:]
    assert re.search(r'^GAMEPAD player=1 ', text, re.M) and re.search(rf'^GAMEPAD player={port} ', text, re.M), 'pads not on ports 1 and %d' % port
    modes = [int(m) for m in re.findall(r'^MODE .* mode=(-?\d+)$', text, re.M)]
    seq = [m for i, m in enumerate(modes) if i == 0 or m != modes[i - 1]]
    joined = any(seq[i:i + 3] == [2, 11, 2] for i in range(len(seq)))
    assert joined and 3 in seq, f'no join (2 -> 11 -> 2) and live play: modes {seq}'
    sw = [int(v, 16) for v in re.findall(rf'^INPUT .* sw{port}=([0-9A-F]+)', text, re.M)]
    assert any(v & 0x10 for v in sw) and any(v & 0x08 for v in sw), f'player {port} switch word never showed Shoot/right from pad 2'
    stats = [[tuple(int(x) for x in p.split('/')) for p in s.split(',')] for s in re.findall(r'^PROGRESS .* stats=([0-9/,]+)', text, re.M)]
    assert stats, 'no PROGRESS stats'
    last = stats[-1]
    fga = [p[1] for p in last]
    if shoot: assert fga[port - 1] > 0, f'player {port} took no shots with pad 2: {last}'
    else: assert all(s[port - 1][1] == 0 for s in stats), f'player {port} shot although pad 2 was idle (CPU-played?): {last}'
    cpu = [i for i in range(4) if i not in (0, port - 1)]
    assert any(fga[i] > 0 for i in cpu), f'CPU players never shot: {last}'
    print(f'{name}: pads on ports 1 and {port}; modes {seq}; player {port} switch word reacted ({sum(1 for v in sw if v)} changes); '
          f'final stats FGM/FGA/AST per player {last}', flush=True)

for name in (sys.argv[1:] or RUNS):
    run(name)
print('PASS: two gamepads: the second pad joined as player 3 (head-to-head, shooting in the game\'s stats) and as player 2 '
      '(teammate, human: never CPU-played); screenshots captures/mp/*.png')
