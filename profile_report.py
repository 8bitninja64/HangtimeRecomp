"""Summarize a hangtime --profile-gfx sample table (full_runtime/perf.hpp).

usage: python profile_report.py PROFILE.csv [--map hangtime.map] [--phase 1|2] [--bucket 0|1|2] [--top N]

Phases: 1 display list (RT64 processDisplayLists, before DP completion), 2 screen update
(RT64 updateScreen), 3 RT64 present thread, 4 RT64 workload thread, 5 RT64 texture thread
Buckets: time into the call when sampled: 0 <4 ms, 1 4-8 ms, 2 >=8 ms (so
bucket 2 shows only what long calls are doing). Prints the top self (leaf) and inclusive
functions; hangtime.exe frames are named from the linker map, other modules by module+offset.
"""
import bisect, re, sys
from collections import Counter
from pathlib import Path

args = sys.argv[1:]
opt = lambda k, d=None: args[args.index(k) + 1] if k in args else d
mp = Path(opt('--map', Path(__file__).parent / 'full_runtime/build/Release/hangtime.map'))
base, syms = None, []
for line in mp.read_text(errors='replace').splitlines():
    m = re.match(r'\s*Preferred load address is ([0-9a-fA-F]+)', line)
    if m: base = int(m.group(1), 16)
    m = re.match(r'\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s+', line)
    if m and base: syms.append((int(m.group(2), 16) - base, m.group(1)))
syms.sort(); rvas = [s[0] for s in syms]

def name(frame):
    mod, _, off = frame.partition('+0x')
    if mod.lower() != 'hangtime.exe': return mod
    i = bisect.bisect_right(rvas, int(off, 16)) - 1
    n = syms[i][1] if i >= 0 else '?'
    # MSVC decorated names: keep the readable part (?func@Class@RT64@@... -> RT64::Class::func).
    m = re.match(r'\?([^@]+)@([^@]+)@([^@]+)@@', n)
    return f'{m.group(3)}::{m.group(2)}::{m.group(1)}' if m else n

phase, bucket, top = opt('--phase'), opt('--bucket'), int(opt('--top', 25))
self_c, incl_c, total = Counter(), Counter(), 0
with open(args[0]) as f:
    next(f)
    for line in f:
        c, ph, bk, stack = line.rstrip('\n').split(',', 3)
        if (phase and ph != phase) or (bucket and bk != bucket): continue
        c = int(c); total += c
        names = [name(fr) for fr in stack.split(';')]
        self_c[names[0]] += c
        for n in set(names): incl_c[n] += c
print(f'samples: {total} (phase={phase or "all"} bucket={bucket or "all"})')
for title, cnt in (('self', self_c), ('inclusive', incl_c)):
    print(f'--- top {title}')
    for n, c in cnt.most_common(top): print(f'{100.0 * c / max(1, total):6.1f}% {c:7d}  {n}')
