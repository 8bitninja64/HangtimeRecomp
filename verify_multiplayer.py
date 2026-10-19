"""Local multiplayer (two and four players) through gamepads (SDL virtual game controllers, read through
SDL's GameController API like physical pads; main.cpp "Players 1-4").

Runs (~3.5 min each, state-gated input like verify_input.py):
  headtohead  --pad-order 1324: pad 2 is N64 controller 3 (player 3, the other team). Player 1 goes
              through the menus on pad 1; pad 2 presses START at team select (the game's JOIN IN),
              picks its own team and locks it in with Shoot; in play pad 2 shoots repeatedly.
  teammates   --pad-order 1234: the same with pad 2 as controller 2 (player 1's teammate), idle
              in play (it never shoots).
  four        four pads on controllers 1-4. Player 1 starts; pads 2, 3 and 4 each press START at
              one visit of team select (the game takes one JOIN IN per visit: after each join it
              shows that player's SELECT PLAYER and returns), then all four pick teams. In play
              pads 1 and 3 shoot, pads 2 and 4 only pass.
Checks on the game's own state (read-only, FULL_GAME.md): the pads land on the expected ports;
the joining player's arcade switch word (0x800A65B0 + 2*player, built by 0x80024888) reacts to
pad 2; the game re-enters player select for the join (mode 2 -> 11 -> 2) and reaches live play
(mode 3); the stat records (0x800A6D78 + 0x20*player) show the joined player is human: in the
head-to-head run player 3 takes shots (FGA > 0) only through pad 2; in the teammates run player 2
stays at 0 attempts all quarter (the CPU never plays a human's player), while the CPU-played
players score; in the four-player run three joins happen, every player's switch word reacts to
its own pad, players 2 and 4 (passing only) never attempt a shot all quarter (no player is
CPU-played) and players 1/3 do. Screenshots captures/mp/{run}_*.png are game content: local only.
"""
import re, subprocess, sys
from pathlib import Path

P = Path(__file__).resolve().parent
for _d in ('test_logs', 'captures'): (P / _d).mkdir(exist_ok=True)   # git-ignored; absent in a fresh clone

def menu_p1(ts=2, shoot=False):
    """Player 1 starts the game and picks a team at the ts-th visit of team select (one visit
    more per join)."""
    f = '--pad-press'
    a = [f'{f} m1+{v}:START' for v in range(200, 2000, 45)] + [f'{f} m12+40:START', f'{f} m12+120:START']
    a += [f'{f} m11+40:START', f'{f} m11+120:START']
    a += [f'{f} m2#{ts}+150:LX+', f'{f} m2#{ts}+240:RB'] + [f'{f} m2#{ts}+{v}:A' for v in range(500, 1101, 90)]
    if shoot: a += [f'{f} m3+{v}:A' for v in range(630, 6000, 60)]
    return a

def pad2(shoot, k=2, join=1, ts=2):
    """Pad k presses START at the join-th visit of team select (the game's JOIN IN; the game then
    shows that player's SELECT PLAYER and returns to team select), then picks a team at visit ts."""
    f = '--pad-press'
    a = [f'{f} m2#{join}+60:START@{k}', f'{f} m2#{join}+100:START@{k}']   # JOIN IN at team select
    a += [f'{f} m2#{ts}+180:LX+@{k}', f'{f} m2#{ts}+270:RB@{k}'] + [f'{f} m2#{ts}+{v}:A@{k}' for v in range(530, 1131, 90)]
    if shoot: a += [f'{f} m3+{v}:A@{k}' for v in range(600, 6000, 60)]     # Shoot in play
    return a

RUNS = {'headtohead': ('1324', 3, True), 'teammates': ('1234', 2, False)}

def capture(name, args):
    r = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(P / 'full_runtime/capture_window.ps1'),
                        '-Shots', '60,120,170,200', '-OutPrefix', str(P / f'captures/mp/{name}'), '-Log', str(P / f'test_logs/mp_{name}'),
                        '-GameArgs', ' '.join(args)], cwd=P, capture_output=True, text=True, timeout=400)
    text = (P / f'test_logs/mp_{name}.err.txt').read_text(errors='replace')
    assert r.returncode == 0, r.stdout[-500:] + text[-2000:]
    assert not re.search(r'^(FAIL|CRASH)', text, re.M), text[-2000:]
    modes = [int(m) for m in re.findall(r'^MODE .* mode=(-?\d+)$', text, re.M)]
    seq = [m for i, m in enumerate(modes) if i == 0 or m != modes[i - 1]]
    stats = [[tuple(int(x) for x in p.split('/')) for p in s.split(',')] for s in re.findall(r'^PROGRESS .* stats=([0-9/,]+)', text, re.M)]
    assert stats, 'no PROGRESS stats'
    return text, seq, stats

def switch_words(text, port):
    sw = [int(v, 16) for v in re.findall(rf'^INPUT .* sw{"" if port == 1 else port}=([0-9A-F]+)', text, re.M)]
    assert any(v & 0x10 for v in sw) and any(v & 0x08 for v in sw), f'player {port} switch word never showed Shoot/right from its pad'
    return sw

def run_four():
    f = '--pad-press'
    args = ['--background', '--no-keyboard', '--input-trace', '--virtual-pad', '4', '--pad-order', '1234'] + menu_p1(4, shoot=True)
    for k in (2, 3, 4): args += pad2(k == 3, k, join=k - 1, ts=4)
    for k in (2, 4): args += [f'{f} m3+{v}:B@{k}' for v in range(615, 6000, 60)]   # Pass only
    text, seq, stats = capture('four', args)
    for k in (1, 2, 3, 4): assert re.search(rf'^GAMEPAD player={k} ', text, re.M), f'no pad on port {k}'
    joins = sum(1 for i in range(len(seq)) if seq[i:i + 3] == [2, 11, 2])
    assert joins >= 3 and 3 in seq, f'expected three joins (2 -> 11 -> 2) and live play: modes {seq}'
    for k in (1, 2, 3, 4): switch_words(text, k)
    last = stats[-1]
    assert all(s[1][1] == 0 and s[3][1] == 0 for s in stats), f'player 2 or 4 shot although their pads only passed (CPU-played?): {last}'
    assert last[0][1] + last[2][1] > 0, f'players 1 and 3 took no shots: {last}'
    print(f'four: pads on ports 1-4; modes {seq}; {joins} joins; every switch word reacted; final stats FGM/FGA/AST per player {last}', flush=True)

def run(name):
    if name == 'four': return run_four()
    order, port, shoot = RUNS[name]
    args = ['--background', '--no-keyboard', '--input-trace', '--virtual-pad', '2', '--pad-order', order] + menu_p1() + pad2(shoot)
    text, seq, stats = capture(name, args)
    assert re.search(r'^GAMEPAD player=1 ', text, re.M) and re.search(rf'^GAMEPAD player={port} ', text, re.M), 'pads not on ports 1 and %d' % port
    joined = any(seq[i:i + 3] == [2, 11, 2] for i in range(len(seq)))
    assert joined and 3 in seq, f'no join (2 -> 11 -> 2) and live play: modes {seq}'
    sw = switch_words(text, port)
    last = stats[-1]
    fga = [p[1] for p in last]
    if shoot: assert fga[port - 1] > 0, f'player {port} took no shots with pad 2: {last}'
    else: assert all(s[port - 1][1] == 0 for s in stats), f'player {port} shot although pad 2 was idle (CPU-played?): {last}'
    cpu = [i for i in range(4) if i not in (0, port - 1)]
    assert any(fga[i] > 0 for i in cpu), f'CPU players never shot: {last}'
    print(f'{name}: pads on ports 1 and {port}; modes {seq}; player {port} switch word reacted ({sum(1 for v in sw if v)} changes); '
          f'final stats FGM/FGA/AST per player {last}', flush=True)

for name in (sys.argv[1:] or [*RUNS, 'four']):
    run(name)
print('PASS: gamepads: the second pad joined as player 3 (head-to-head, shooting in the game\'s stats) and as player 2 '
      '(teammate, human: never CPU-played); four pads joined as players 1-4, all human; screenshots captures/mp/*.png')
