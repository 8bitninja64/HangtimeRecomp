"""Resolve `--sample` stacks (hangtime.exe+0xRVA) to function names via the linker map.
Usage: python resolve_stacks.py <stderr log> [map file]"""
import bisect, re, sys
from pathlib import Path
log = Path(sys.argv[1]).read_text(errors='replace').splitlines()
mp = Path(sys.argv[2] if len(sys.argv) > 2 else Path(__file__).parent / 'build/Release/hangtime.map')
base, syms = None, []
for line in mp.read_text(errors='replace').splitlines():
    m = re.match(r'\s*Preferred load address is ([0-9a-fA-F]+)', line)
    if m: base = int(m.group(1), 16)
    m = re.match(r'\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s+', line)
    if m and base: syms.append((int(m.group(2), 16) - base, m.group(1)))
syms.sort(); rvas = [s[0] for s in syms]
for line in log:
    m = re.match(r'(\s*STACK \d+) hangtime\.exe\+0x([0-9A-F]+)', line)
    if m:
        off = int(m.group(2), 16); i = bisect.bisect_right(rvas, off) - 1
        print(f'{m.group(1)} {syms[i][1] if i >= 0 else "?"} +0x{off - syms[i][0]:X}')
    elif line.startswith('THREAD') or ('STACK' in line and 'hangtime.exe' not in line and ('ntdll' in line or 'KERNEL' in line)):
        print(line)
