"""Whole-program function partition of the NBA Hangtime CPU text (read-only ROM analysis).

The game has no code overlays: all CPU code is the IPL3 image ROM 0x1000 -> 0x80000400, and
CPU text ends where rspboot begins (0x800780B0). This tool partitions that text into functions
for N64Recomp and records hardware-touching routines that must map to runtime services.

Starts come from: the boot entry, every JAL target, every code pointer found in the data image
(words in [0x80000400,0x800780B0) that land on a plausible function start), and gaps after a
function's natural end that still contain code. Each function ends at the next start.

Usage: python full_inventory.py            -> writes full_inventory.json and prints a summary
"""
import json, struct, sys
from pathlib import Path

P = Path(__file__).resolve().parent
sys.path.insert(0, str(P))
import review_function as rf
TEXT_START, TEXT_END = 0x80000400, 0x800780B0
DATA_END = 0x800A3880  # initialized image end (BSS starts here)
NOP = 0

def word(a): return rf.word(a)

def natural_end(start, limit):
    """End after the delay slot of the first `jr ra` beyond every forward branch target."""
    reach = start; pc = start
    while pc < limit:
        w = word(pc); t = rf.branch_target(pc, w)
        if t is not None and t > reach: reach = t
        op = w >> 26
        if w == 0x03E00008 and pc >= reach: return pc + 8
        # Unconditional jumps that never return (j to outside / b . loops) end flow as well.
        if op == 2 and pc >= reach:
            tgt = ((pc + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
            if not start <= tgt < pc + 8: return pc + 8
        if w == 0x1000FFFF and pc >= reach: return pc + 8   # b . (infinite loop)
        pc += 4
    return limit

def plausible_start(a):
    if not (TEXT_START <= a < TEXT_END and a % 4 == 0): return False
    w = word(a)
    return w != NOP

def follows_return(a):
    """True when `a` begins right after a function end: `jr ra` + delay slot, then optional
    alignment nops (or `a` is the first word of text)."""
    if a == TEXT_START: return True
    k = a - 4
    while k > a - 64 and word(k) == NOP: k -= 4
    return word(k) == 0x03E00008 or word(k - 4) == 0x03E00008

def main():
    starts = {TEXT_START}
    jal_sites = []
    for pc in range(TEXT_START, TEXT_END, 4):
        w = word(pc)
        if w >> 26 == 3:
            t = ((pc + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
            if TEXT_START <= t < TEXT_END: starts.add(t); jal_sites.append((pc, t))
    # Switch jump tables (lui rY,HI / ... / lw rX,LO(rY) / jr rX): their words are case labels.
    table_words = set(); tables = {}
    for pc in range(TEXT_START, TEXT_END, 4):
        w = word(pc)
        if not (w >> 26 == 0 and (w & 63) == 8 and ((w >> 21) & 31) != 31): continue
        reg = (w >> 21) & 31; base = None
        for k in range(pc - 4, pc - 48, -4):
            x = word(k)
            if x >> 26 == 35 and ((x >> 16) & 31) == reg:   # lw reg, LO(rY)
                ry = (x >> 21) & 31; lo = rf.simm(x)
                for m in range(k - 4, k - 48, -4):
                    y = word(m)
                    if y >> 26 == 15 and ((y >> 16) & 31) == ry: base = (((y & 0xFFFF) << 16) + lo) & 0xFFFFFFFF; break
                break
        if base is None or not TEXT_END <= base < DATA_END: continue
        entries = []
        a = base
        while a < DATA_END and TEXT_START <= word(a) < TEXT_END and abs(word(a) - pc) < 0x4000:
            entries.append(word(a)); table_words.add(a); a += 4
        tables[hex(pc)] = dict(table=hex(base), entries=len(entries))
    # Code pointers in the initialized data image (vtables, callback tables, thread entries).
    data_ptrs = set()
    for a in range(TEXT_END, DATA_END, 4):
        if a in table_words: continue
        v = word(a)
        if plausible_start(v) and (v in starts or follows_return(v)):
            data_ptrs.add(v)
    # Code pointers built in code with lui/addiu (e.g. thread entries, comparators).
    code_ptrs = set(); his = {}
    for pc in range(TEXT_START, TEXT_END, 4):
        w = word(pc); op = w >> 26; rt = (w >> 16) & 31; rs = (w >> 21) & 31
        if op == 15: his[rt] = (w & 0xFFFF) << 16; continue
        if op == 9 and rs in his:
            v = (his[rs] + rf.simm(w)) & 0xFFFFFFFF
            if plausible_start(v) and (v in starts or follows_return(v)):
                code_ptrs.add(v)
    starts |= data_ptrs | code_ptrs
    # Fill gaps: code after a function's natural end that is not covered by another start.
    changed = True
    while changed:
        changed = False
        ordered = sorted(starts)
        for i, s in enumerate(ordered):
            limit = ordered[i + 1] if i + 1 < len(ordered) else TEXT_END
            end = natural_end(s, limit)
            pc = end
            while pc < limit and word(pc) == NOP: pc += 4
            if pc < limit and pc not in starts:
                starts.add(pc); changed = True
    ordered = sorted(starts)
    funcs = []
    for i, s in enumerate(ordered):
        end = ordered[i + 1] if i + 1 < len(ordered) else TEXT_END
        funcs.append((s, end))
    # Checks: every branch stays inside its function; record jalr / jr-reg / J / hardware use.
    problems, hardware, jump_tables, tails = [], {}, [], []
    for s, e in funcs:
        hw = set()
        for pc in range(s, e, 4):
            w = word(pc); op = w >> 26; t = rf.branch_target(pc, w)
            if t is not None and not s <= t < e: problems.append(dict(func=hex(s), pc=hex(pc), target=hex(t)))
            if op == 0 and (w & 63) == 8 and ((w >> 21) & 31) != 31: jump_tables.append(dict(func=hex(s), pc=hex(pc)))
            if op == 2:
                tgt = ((pc + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
                if not s <= tgt < e: tails.append(dict(func=hex(s), pc=hex(pc), target=hex(tgt)))
            if op == 16: hw.add('cop0')
            if op == 15 and ((w & 0xFFFF) in range(0xA400, 0xA4C0) or (w & 0xFFFF) in range(0xA000, 0xA040) or (w & 0xFFFF) == 0xBFC0):
                hw.add('mmio')
            if op == 47: hw.add('cache')
        if hw: hardware[hex(s)] = sorted(hw)
    text_bytes = sum(e - s for s, e in funcs)
    report = dict(text=[hex(TEXT_START), hex(TEXT_END)], functions=len(funcs), text_bytes=text_bytes,
                  jal_sites=len(jal_sites), jal_targets=len({t for _, t in jal_sites}),
                  data_pointer_starts=len(data_ptrs), code_pointer_starts=len(code_ptrs),
                  branch_problems=problems, tail_jumps=tails, jump_table_sites=len(jump_tables), jump_tables=tables,
                  hardware_functions=hardware, ranges=[[hex(s), hex(e)] for s, e in funcs])
    (P / 'full_inventory.json').write_text(json.dumps(report, indent=1))
    print(f"functions={len(funcs)} bytes=0x{text_bytes:X} jal_targets={report['jal_targets']} data_ptr_starts={len(data_ptrs)} "
          f"code_ptr_starts={len(code_ptrs)} branch_problems={len(problems)} tail_jumps={len(tails)} "
          f"jump_table_sites={len(jump_tables)} hardware_functions={len(hardware)}")

if __name__ == '__main__':
    main()
