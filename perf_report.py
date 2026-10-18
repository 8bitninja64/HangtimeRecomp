"""Frame-timing report from a hangtime --perf event log (full_runtime/perf.hpp).

usage: python perf_report.py LOG.csv [--from SECONDS] [--json OUT.json]

A "screen" is one presented VI (ultramodern's per-VI screen update). The game double-buffers,
so a screen showing a different framebuffer origin than the previous screen is a new game frame;
the same origin twice is a missed VI (the game's next frame was not swapped in time). The game
has no frame skip, so every missed VI also delays its logic (one input poll per game frame).

Reported: VI timer intervals; screens, new frames and missed VIs (per game mode); frame time in
VIs and ms; display-list processing (RT64 processDisplayLists, which ends in DP completion) and
present (RT64 updateScreen) durations; what the gfx thread was doing at the VI before each missed
screen: in a display list (DL still processing when the VI fired), presenting, or idle (the game
had not submitted its display list yet); RT64's own profiler averages (R/r lines).
"""
import bisect, json, statistics, sys
from collections import Counter, defaultdict

def pct(values, p):
    if not values: return float('nan')
    v = sorted(values); k = (len(v) - 1) * p / 100.0; f = int(k); c = min(f + 1, len(v) - 1)
    return v[f] + (v[c] - v[f]) * (k - f)

def summary(values):
    if not values: return 'n=0'
    return (f'n={len(values)} avg={statistics.fmean(values):.2f} p50={pct(values, 50):.2f} p95={pct(values, 95):.2f} '
            f'p99={pct(values, 99):.2f} max={max(values):.2f}')

def load(path):
    ev = []
    with open(path) as f:
        next(f)
        for line in f:
            p = line.rstrip('\n').split(',')
            if len(p) == 4: ev.append((p[0], int(p[1]), int(p[2]), int(p[3])))
    return ev

def analyze(ev, t_from_us=0):
    vi = [t for k, t, a, b in ev if k == 'V']
    modes = [(t, a) for k, t, a, b in ev if k == 'M']
    mode_t = [t for t, _ in modes]
    def mode_at(t):
        i = bisect.bisect_right(mode_t, t) - 1
        return modes[i][1] if i >= 0 else -1
    dls, open_dl = [], {}
    screens, open_s = [], {}
    rt64, ring, skips, ws_us = [], [], [], []
    for k, t, a, b in ev:
        if k == 'D': open_dl[a] = t
        elif k == 'd' and a in open_dl: dls.append((open_dl.pop(a), t))
        elif k == 'S': open_s[b] = (t, a)
        elif k == 's' and b in open_s: t0, origin = open_s.pop(b); screens.append((t0, t, origin, b))
        elif k == 'R': rt64.append([t, a, b, 0, 0])
        elif k == 'r' and rt64: rt64[-1][3:] = [a, b]
        elif k == 'h': ring.append((t, a, b))
        elif k == 'k': skips.append(t)
        elif k == 'w': ws_us.append((t, a))
    dls = [d for d in dls if d[0] >= t_from_us]
    screens = [s for s in screens if s[0] >= t_from_us]
    vi = [t for t in vi if t >= t_from_us]
    dl_starts = [d[0] for d in dls]
    s_starts = [s[0] for s in screens]

    def gfx_state(t):
        i = bisect.bisect_right(dl_starts, t) - 1
        if i >= 0 and dls[i][0] <= t < dls[i][1]: return 'in_dl'
        j = bisect.bisect_right(s_starts, t) - 1
        if j >= 0 and screens[j][0] <= t < screens[j][1]: return 'presenting'
        return 'idle'

    out = {}
    vi_iv = [(b - a) / 1000 for a, b in zip(vi, vi[1:])]
    span_s = (vi[-1] - vi[0]) / 1e6 if len(vi) > 1 else 0
    out['span_s'] = round(span_s, 1)
    out['vi'] = {'count': len(vi), 'expected_at_60hz': round(span_s * 60), 'interval_ms': summary(vi_iv),
                 'late_over_20ms': sum(1 for x in vi_iv if x > 20)}
    # Screens: new frame vs repeated origin (only once the game drives the VI: origin != 0).
    new, missed, frame_vis, last_new_idx, last_origin = 0, [], [], None, None
    per_mode = defaultdict(lambda: [0, 0])
    for t0, t1, origin, idx in screens:
        if origin == 0: last_origin = None; continue
        m = mode_at(t0)
        if last_origin is None or origin != last_origin:
            new += 1; per_mode[m][0] += 1
            if last_new_idx is not None: frame_vis.append(idx - last_new_idx)
            last_new_idx = idx
        else:
            missed.append((t0, idx, m)); per_mode[m][1] += 1
        last_origin = origin
    # What the gfx thread was doing at the VI that preceded each missed screen.
    vi_sorted = vi
    causes = Counter()
    for t0, idx, m in missed:
        i = bisect.bisect_right(vi_sorted, t0) - 1
        causes[gfx_state(vi_sorted[i - 1]) if i >= 1 else 'unknown'] += 1
    screens_n = sum(1 for s in screens if s[2] != 0)
    out['screens'] = {'count': screens_n, 'new_frames': new, 'missed_vis': len(missed),
                      'fps_equivalent': round(60.0 * new / screens_n, 2) if screens_n else 0,
                      'missed_by_gfx_state_at_previous_vi': dict(causes)}
    out['frame_time_vis'] = dict(sorted(Counter(frame_vis).items()))
    out['per_mode'] = {str(m): {'new': n, 'missed': x, 'missed_pct': round(100.0 * x / max(1, n + x), 2)} for m, (n, x) in sorted(per_mode.items())}
    out['dl_ms'] = summary([(b - a) / 1000 for a, b in dls])
    out['present_ms'] = summary([(t1 - t0) / 1000 for t0, t1, _, _ in screens])
    # Display list start relative to the preceding VI (how late in the 16.7 ms window the game submits).
    sub = []
    for a, b in dls:
        i = bisect.bisect_right(vi_sorted, a) - 1
        if i >= 0: sub.append((a - vi_sorted[i]) / 1000)
    out['dl_start_after_vi_ms'] = summary(sub)
    out['dl_end_after_vi_ms'] = summary([((b - vi_sorted[bisect.bisect_right(vi_sorted, a) - 1]) / 1000)
                                         for a, b in dls if bisect.bisect_right(vi_sorted, a) >= 1])
    if rt64:
        rt = [r for r in rt64 if r[0] >= t_from_us]
        out['rt64_avg_ms'] = {k: round(statistics.fmean(r[j] for r in rt) / 1000, 3) for k, j in
                              (('renderer_gpu', 1), ('workload', 2), ('present_interval', 3), ('dl_cpu', 4))} if rt else {}
    # Real RT64 swap-chain presents and rendered workloads per second (profiler ring positions, mod 120;
    # valid while fewer than 120 happen between samples, i.e. below 120 per second).
    ring = [r for r in ring if r[0] >= t_from_us]
    if len(ring) > 1:
        pres = [((b[1] - a[1]) % 120) / ((b[0] - a[0]) / 1e6) for a, b in zip(ring, ring[1:])]
        work = [((b[2] - a[2]) % 120) / ((b[0] - a[0]) / 1e6) for a, b in zip(ring, ring[1:])]
        out['rt64_presents_per_s'] = summary(pres)
        out['rt64_workloads_per_s'] = summary(work)
    if ws_us: out['widescreen_rewrite_ms'] = summary([a / 1000 for t, a in ws_us if t >= t_from_us])
    out['present_skips'] = sum(1 for t in skips if t >= t_from_us)   # presentation guard (rt64_context.cpp)
    # Worst stalls: longest gaps between new frames.
    worst = []
    last = None
    for t0, t1, origin, idx in screens:
        if origin == 0: last = None; continue
        if last is not None and origin != last[1]:
            worst.append(((t0 - last[0]) / 1000, round(last[0] / 1e6, 2), mode_at(t0)))
        if last is None or origin != last[1]: last = (t0, origin)
    out['worst_frame_gaps_ms'] = [{'gap_ms': round(g, 1), 't_s': t, 'mode': m} for g, t, m in sorted(worst, reverse=True)[:10]]
    out['missed_per_minute'] = {}
    for t0, idx, m in missed:
        k = int(t0 / 60e6); out['missed_per_minute'][k] = out['missed_per_minute'].get(k, 0) + 1
    return out

if __name__ == '__main__':
    args = sys.argv[1:]
    t_from = float(args[args.index('--from') + 1]) * 1e6 if '--from' in args else 0
    r = analyze(load(args[0]), t_from)
    for k, v in r.items(): print(f'{k}: {v}')
    if '--json' in args:
        with open(args[args.index('--json') + 1], 'w') as f: json.dump(r, f, indent=1)
