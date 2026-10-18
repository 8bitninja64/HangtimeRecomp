"""Controls test of the whole-program executable through the real host input paths.

Two runs, each driven only by one device, from boot through the menus and team select into a
live match and through its first quarter (~3 min each):
  keyboard  --key-press: WM_KEYDOWN/WM_KEYUP posted to the game window, so keys go through SDL's
            own window procedure and keyboard state like physical keys;
  gamepad   --virtual-pad --pad-press: an SDL virtual game controller (standard mapping), read
            through SDL's GameController API like a physical pad; physical keyboard ignored.
Windows open in the background (no keyboard focus), so typing elsewhere cannot leak in.
Presses are gated on the game's mode word (m<mode>+<polls>, see full_runtime/main.cpp), because
screen durations vary between runs.

Checks on the --input-trace log (the game's own decoded state, read-only):
  - every control reaches the expected bit of the game's arcade switch word (0x800A65B0:
    1/2/4/8 up/down/left/right, 0x10 Shoot, 0x20 Pass, 0x40 Turbo, 0x80 Start),
  - player 1 is really controlled: through the first quarter a scripted player calls for the ball
    (Pass) and shoots; the game's own stat record for player 1 (0x800A6D78, PROGRESS stats=)
    must show field-goal attempts (the CPU never plays player 1: idle runs stay 0/0),
  - the team is locked in with Shoot and the match starts (mode 3) — the menus were navigated by
    that device alone,
  - no FAIL/CRASH, the render loop stays live.
Screenshots captures/input_{keyboard,gamepad}_*.png are game content: local only.
"""
import re, subprocess, sys
from pathlib import Path

P = Path(__file__).resolve().parent
for _d in ('test_logs', 'captures'): (P / _d).mkdir(exist_ok=True)   # git-ignored; absent in a fresh clone

def menu(flag, start, right, select, shoot):
    """State-gated menu path: Start through the title, player 1, CHOOSE OPTION -> SELECT TEAMS, Shoot."""
    a = [f'{flag} m1+{v}:{start}' for v in range(200, 2000, 45)]            # notice, attract -> title menu
    a += [f'{flag} m12+40:{start}', f'{flag} m12+120:{start}']              # START GAME
    a += [f'{flag} m11+40:{start}', f'{flag} m11+120:{start}']              # player 1
    a += [f'{flag} m2+150:{right}', f'{flag} m2+240:{select}']              # CHOOSE OPTION: SELECT TEAMS
    a += [f'{flag} m2+{v}:{shoot}' for v in range(500, 1101, 90)]           # team select: Shoot locks in
    return a

def play(flag, controls, passing, shoot, start=900, step=40):
    """In the match (mode 3): each control in turn, then Pass + Shoot every 60 polls of the quarter."""
    return [f'{flag} m3+{start + i * step}:{c}' for i, c in enumerate(controls)] + \
           [f'{flag} m3+1500..7000/60:{passing}', f'{flag} m3+1525..7000/60:{shoot}']

KEYBOARD = {  # key -> (trace field, value, expected switch bit)
    'RETURN': ('host', 0x1000, 0x80), 'S': ('host', 0x0004, 0x10), 'D': ('host', 0x0001, 0x20),
    'X': ('host', 0x8000, 0x40), 'LSHIFT': ('host', 0x8000, 0x40), 'SPACE': ('host', 0x8000, 0x40),
    'RIGHT': ('stick', (100, 0), 0x08), 'LEFT': ('stick', (-100, 0), 0x04),
    'UP': ('stick', (0, 100), 0x01), 'DOWN': ('stick', (0, -100), 0x02),
}
GAMEPAD = {
    'START': ('host', 0x1000, 0x80), 'A': ('host', 0x0004, 0x10), 'B': ('host', 0x0001, 0x20),
    'RB': ('host', 0x8000, 0x40), 'RT': ('host', 0x8000, 0x40), 'RY+': ('host', 0x0004, 0x10),
    'DU': ('host', 0x0800, 0x01), 'DD': ('host', 0x0400, 0x02), 'DL': ('host', 0x0200, 0x04), 'DR': ('host', 0x0100, 0x08),
    'LX+': ('stick', (100, 0), 0x08), 'LX-': ('stick', (-100, 0), 0x04),
    'LY-': ('stick', (0, 100), 0x01), 'LY+': ('stick', (0, -100), 0x02),
}
RUNS = {
    'keyboard': ['--background', '--input-trace'] + menu('--key-press', 'RETURN', 'RIGHT', 'X', 'S')
                + play('--key-press', ['LEFT', 'RIGHT', 'UP', 'DOWN', 'LSHIFT', 'SPACE', 'D', 'S', 'X', 'D'], 'D', 'S'),
    'gamepad': ['--background', '--no-keyboard', '--input-trace', '--virtual-pad'] + menu('--pad-press', 'START', 'LX+', 'RB', 'A')
               + play('--pad-press', ['LX-', 'LX+', 'LY-', 'LY+', 'DU', 'DD', 'DL', 'DR', 'B', 'RT', 'RY+', 'A', 'B'], 'B', 'A'),
}
EXPECT = {'keyboard': KEYBOARD, 'gamepad': GAMEPAD}
def check(name, text, min_rate=50):
    """The checks above on one run's log (verify_linux.py reuses them); returns (summary, passed)."""
    trace = [(int(h, 16), (int(x), int(y)), int(s, 16)) for h, x, y, s in
             re.findall(r'^INPUT poll=\d+ vi=\d+ host=([0-9A-F]+) stick=(-?\d+),(-?\d+) game=[0-9A-F]+ sw=([0-9A-F]+)', text, re.M)]
    modes = [int(m) for m in re.findall(r'^MODE .* mode=(-?\d+)', text, re.M)]
    prog = [int(d) for d in re.findall(r'^PROGRESS vi=\d+ display_lists=(\d+)', text, re.M)]
    rates = [b - a for a, b in zip(prog[3:], prog[4:])]
    missing = []
    for control, (field, value, bit) in EXPECT[name].items():
        hit = any((h & value) == value and s & bit for h, st, s in trace) if field == 'host' else \
              any(st == value and s & bit for h, st, s in trace)
        if not hit: missing.append(control)
    reached = 3 in modes
    stats = re.findall(r'^PROGRESS .* period=(-?\d+) .* stats=(\d+)/(\d+)/(\d+),', text, re.M)
    first_q = [(int(m), int(a), int(s)) for q, m, a, s in stats if q == '0']
    fgm, fga, ast = max(first_q, key=lambda x: x[1]) if first_q else (0, 0, 0)
    controlled = fga >= 1
    live = bool(rates) and min(rates) >= min_rate
    return (f'{name}: {len(trace)} trace lines, modes {modes}, display lists/s {min(rates) if rates else 0}-{max(rates) if rates else 0}; '
            f'missing controls {missing or "none"}; match reached {reached}; player 1 first quarter FG {fgm}/{fga}, assists {ast}',
            not missing and reached and live and controlled)

if __name__ == '__main__':
    assert (P / 'full_runtime/build/Release/hangtime.exe').exists(), 'run build_full.py first'
    shots = (30, 45, 60, 120, 175)
    ok = True
    for name, args in RUNS.items():
        log = P / f'test_logs/input_{name}'
        r = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(P / 'full_runtime/capture_window.ps1'),
                            '-Shots', ','.join(map(str, shots)), '-OutPrefix', str(P / f'captures/input_{name}'),
                            '-GameArgs', ' '.join(args), '-Log', str(log)], cwd=P, capture_output=True, text=True, timeout=300)
        text = (P / f'test_logs/input_{name}.err.txt').read_text(errors='replace')
        assert r.returncode == 0, f'{name}: executable exited early\n' + text[-2000:]
        assert not re.search(r'^(FAIL|CRASH)', text, re.M), text[-2000:]
        line, passed = check(name, text)
        print(line)
        ok &= passed
    assert ok, 'controls test failed'
    print('PASS: keyboard and gamepad each drove menus, team select (Shoot) and a first quarter of play; every control '
          "reached the game's own switch word and player 1 shot and passed in the game's stats; screenshots captures/input_*.png")
