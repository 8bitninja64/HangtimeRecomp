"""Cheat regression for the whole program (full_runtime/cheats.cpp, FULL_GAME.md "Cheats").

Four sequential runs of the --match input, each reaching the live match:
  none - no cheat: the game's own cheat flags (0x800A6EB4..0x800A6EDE, CHEATFLAGS lines) stay 0
         for the whole match and music fills the audio stream;
  a    - rooftop, ABA ball, big head, no tag arrow, unlimited turbo (turbo held) and the PC cheat
         Always On Fire: the flags the game keeps, and player 1's fire bit (0x800A6EB0) on every
         progress sample once the match runs;
  b    - huge head, baby mode, no music: flags, and silent gaps in the WAV (no music bed);
  c    - shot percentage, hyper speed, power, steal, goaltending, stealth turbo, no pushing,
         no drift, no CPU assistance (Shoot pressed during play): flags.
Matchup-code flags must read 000F (the game's post-pass, 0x80048E60, gives a code entered on one
team to all four players); button-sequence flags carry player 1's bit. Screenshots land in
captures/cheats/ for inspection (not part of the oracle). Captures stay local.
Usage: verify_cheats.py [none|a|b|c ...]
"""
import math, re, struct, subprocess, sys, wave
from pathlib import Path

P = Path(__file__).resolve().parent
for _d in ('test_logs', 'captures'): (P / _d).mkdir(exist_ok=True)   # git-ignored; absent in a fresh clone
exe = P / 'full_runtime/build/Release/hangtime.exe'
assert exe.exists(), 'run build_full.py first'
(P / 'captures/cheats').mkdir(parents=True, exist_ok=True)
MATCH = ('--background --no-keyboard --no-launcher --press-frame m1+200..2000/45:START --press-frame m12+40:START '
         '--press-frame m12+120:START --press-frame m11+40:START --press-frame m11+120:START '
         '--press-frame m2+150:DR --press-frame m2+240:A --press-frame m2+500..1100/90:CD')   # Shoot locks the team
FLAGS = [0x6EB4 + 2 * i for i in range(22)]   # 0x800A6EB4..0x800A6EDE
def idx(a): return FLAGS.index(a)

def cheats(*ids): return ' '.join('--cheat ' + i for i in ids)
RUNS = {
    'none': dict(args='', shots=(60, 75, 90), wav=True, expect={}),
    'a': dict(args=cheats('rooftop', 'aba_ball', 'big_head', 'no_tag_arrow', 'unlimited_turbo', 'always_on_fire') +
              ' --press-frame m3+900..4000/6:A', shots=(45, 55, 65, 75), wav=False,
              expect={0x6EB4: 1, 0x6EBA: 1, 0x6EC0: 1, 0x6EC2: 1, 0x6EDA: 0xF}, fire=True),
    'b': dict(args=cheats('huge_head', 'baby_mode', 'no_music'), shots=(50, 60, 70, 80), wav=True,
              expect={0x6EB6: 1, 0x6EC8: 0xF, 0x6ECA: 0xF}, quiet=True),
    'c': dict(args=cheats('shot_percentage', 'hyper_speed', 'max_power', 'max_steal', 'legal_goaltending', 'stealth_turbo',
                          'no_pushing', 'no_drift', 'no_cpu_assist') + ' --press-frame m3+700..6000/90:CD',
              shots=(80, 90, 100, 110, 120), wav=False,
              expect={0x6EB8: 1, 0x6EBC: 1, 0x6EBE: 1, 0x6ECC: 0xF, 0x6ED0: 0xF, 0x6ED2: 0xF, 0x6ED6: 0xF, 0x6ED8: 0xF,
                      0x6EDC: 0xF}),
}

def quiet_fraction(path, seconds=20):
    """Share of 50 ms windows below -50 dBFS in the last `seconds` of a capture."""
    with wave.open(str(path), 'rb') as w:
        rate, n = w.getframerate(), w.getnframes()
        s = struct.unpack(f'<{n * 2}h', w.readframes(n))
    win, quiet, total = rate // 20, 0, 0
    for a in range(max(0, n - seconds * rate), n - win, win):
        seg = s[a * 2:(a + win) * 2]
        quiet += math.sqrt(sum(x * x for x in seg) / len(seg)) < 32768 * 10 ** (-50 / 20); total += 1
    return quiet / max(total, 1)

def run(name, cfg):
    args = MATCH + (' ' + cfg['args'] if cfg['args'] else '')
    wav = P / f'captures/cheats/{name}.wav'
    if cfg['wav']: args += f' --wav {wav}'
    log = P / f'test_logs/cheats_{name}'
    r = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(P / 'full_runtime/capture_window.ps1'),
                        '-Shots', ','.join(map(str, cfg['shots'])), '-OutPrefix', str(P / f'captures/cheats/{name}'),
                        '-GameArgs', args, '-Log', str(log)], cwd=P, capture_output=True, text=True, timeout=max(cfg['shots']) + 120)
    text = Path(str(log) + '.err.txt').read_text(errors='replace')
    assert r.returncode == 0, f'{name}: executable exited early:\n' + r.stdout[-500:] + text[-3000:]
    assert not re.search(r'^(FAIL|CRASH)', text, re.M), text[-3000:]
    modes = [(int(p), int(v), int(m)) for p, v, m in re.findall(r'^MODE poll=(\d+) vi=(\d+) t=[\d.]+ mode=(\d+)', text, re.M)]
    m3 = [m for m in modes if m[2] == 3]
    assert m3, f'{name}: never reached the match (modes {[m[2] for m in modes]})'
    flags = [[int(x, 16) for x in l.split()] for l in re.findall(r'^CHEATFLAGS poll=\d+ ((?:[0-9A-F]{4} ?){22})$', text, re.M)]
    assert flags, f'{name}: no CHEATFLAGS line'
    enabled = 'CHEATS enabled' in text
    assert enabled == bool(cfg['expect']), f'{name}: CHEATS enabled line {"missing" if not enabled else "unexpected"}'
    want = [cfg['expect'].get(a, 0) for a in FLAGS]
    for f in flags:   # the flags hold for the whole match (no re-init, no clearing by the game)
        assert f == want, f'{name}: flags {" ".join(f"{x:04X}" for x in f)} != expected {" ".join(f"{x:04X}" for x in want)}'
    prog = [(int(v), int(f, 16)) for v, f in re.findall(r'^PROGRESS vi=(\d+) .* fire=([0-9A-F]+)(?: |$)', text, re.M)]
    live = [p for p in prog if p[0] >= m3[0][1] + 120]
    assert len(live) >= 10, f'{name}: only {len(live)} progress samples in the match'
    notes = [f'{len(flags)} flag state(s), {len(live)} match samples']
    if cfg.get('fire'):
        off = [v for v, f in live if not f & 1]
        assert not off, f'{name}: player 1 not on fire at VI {off[:5]}'
        notes.append('player 1 on fire at every sample')
    if cfg['wav']:
        q = quiet_fraction(wav)
        if cfg.get('quiet'): assert q >= 0.05, f'{name}: no silent gaps ({q:.1%}); music still playing?'
        else: assert q < 0.01, f'{name}: {q:.1%} silent windows with music on'
        notes.append(f'{q:.1%} silent 50 ms windows')
    print(f'PASS cheats {name}: ' + '; '.join(notes))
    return q if cfg['wav'] else None

names = [a for a in sys.argv[1:] if a in RUNS] or list(RUNS)
for n in names: run(n, RUNS[n])
print(f'PASS: cheat regression ({", ".join(names)})')
