"""Read-only ROM review helper: exact range, calls and globals of original functions.

Usage: python review_function.py 0x800243B0 [0x8005FF30 ...] [--known]
Bounds follow prepare_graphics.py rules: the range ends after the delay slot of
the first `jr ra` at or beyond every in-function branch target. `--known` marks
callees already reviewed or hooked in game_main_ranges/prepare_graphics metadata.
"""
import struct, sys
from pathlib import Path
P = Path(__file__).resolve().parent
sys.path.insert(0, str(P))
import hangtime_paths
data = hangtime_paths.rom_bytes()   # big-endian image, SHA-256 checked (hangtime_paths.py)
TEXT_END = 0x800780B0  # rspboot starts here; CPU text is [0x80000400, 0x800780B0)

def word(pc): return struct.unpack_from('>I', data, pc - 0x80000400 + 0x1000)[0]
def simm(w): return (w & 0xFFFF) - 0x10000 if w & 0x8000 else w & 0xFFFF

def branch_target(pc, w):
    op = w >> 26
    if op in (4, 5, 6, 7, 20, 21, 22, 23) or (op == 1 and ((w >> 16) & 31) in (0, 1, 2, 3, 16, 17, 18, 19)) \
            or (op == 17 and ((w >> 21) & 31) == 8):
        return pc + 4 + simm(w) * 4
    return None

def review(start):
    assert 0x80000400 <= start < TEXT_END and start % 4 == 0
    pc, reach = start, start
    calls, jalrs, jumps, his, globals_ = [], [], [], {}, set()
    while True:
        assert pc < TEXT_END, 'ran past CPU text'
        w = word(pc); op = w >> 26
        t = branch_target(pc, w)
        if t is not None: reach = max(reach, t)
        if op == 3: calls.append((pc, ((pc + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2)))
        if op == 2: jumps.append((pc, ((pc + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2)))
        if op == 0 and (w & 63) == 9: jalrs.append(pc)
        rt, rs = (w >> 16) & 31, (w >> 21) & 31
        if op == 15: his[rt] = (w & 0xFFFF) << 16
        elif op in (9, 8, 32, 33, 34, 35, 36, 37, 40, 41, 43, 49, 57, 53, 61) and rs in his:
            globals_.add((his[rs] + simm(w)) & 0xFFFFFFFF)
            if op in (8, 9) and rt == rs: his.pop(rs, None)
        if w == 0x03E00008 and pc >= reach:
            end = pc + 8; break
        pc += 4
    bad = [(pc, branch_target(pc, word(pc))) for pc in range(start, end, 4)
           if branch_target(pc, word(pc)) is not None and not start <= branch_target(pc, word(pc)) < end]
    return end, calls, jalrs, jumps, sorted(globals_), bad

def known_targets():
    # Every reviewed range and hook from the last prepare_graphics.py run (incl. its own lists).
    import re
    text = (P / 'graphics.symbols.toml').read_text()
    return {int(a, 16): n for n, a in re.findall(r'name="([^"]+)",vram=0x([0-9A-Fa-f]+)', text)}

if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    known = known_targets() if '--known' in sys.argv else {}
    for a in args:
        s = int(a, 16)
        end, calls, jalrs, jumps, gl, bad = review(s)
        print(f'{s:08X}-{end:08X} size=0x{end - s:X}')
        for pc, t in calls:
            print(f'  call {pc:08X} -> {t:08X} {known.get(t, "UNREVIEWED") if known else ""}')
        for pc in jalrs: print(f'  jalr {pc:08X} (needs explicit dispatch review)')
        for pc, t in jumps: print(f'  j    {pc:08X} -> {t:08X} (tail jump; prepare_graphics rejects)')
        for pc, t in bad: print(f'  branch {pc:08X} -> {t:08X} OUTSIDE range')
        if gl: print('  globals ' + ' '.join(f'{g:08X}' for g in gl))
