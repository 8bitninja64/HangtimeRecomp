"""The runtime's own partition (full_runtime/partition.cpp, used by the first-run module build)
must equal the generators' output (full_inventory.py + prepare_full.py) byte for byte.

usage: python verify_partition.py [--rom PATH] [--exe PATH]   (needs your ROM; see hangtime_paths.py)
"""
import subprocess, sys, tempfile
from pathlib import Path

P = Path(__file__).resolve().parent
sys.path.insert(0, str(P))
import hangtime_paths
import os
if '--rom' in sys.argv: os.environ['HANGTIME_ROM'] = str(Path(sys.argv[sys.argv.index('--rom') + 1]).resolve())
hangtime_paths.rom_bytes()   # a clear message when the ROM is missing or wrong

exe = Path(sys.argv[sys.argv.index('--exe') + 1]) if '--exe' in sys.argv else \
      (P / 'full_runtime/build/Release/hangtime.exe' if sys.platform == 'win32' else P / 'build/linux/hangtime')
for script in ('full_inventory.py', 'prepare_full.py'):
    g = subprocess.run([sys.executable, str(P / script)], capture_output=True, text=True)
    assert g.returncode == 0, f'{script} failed: ' + g.stdout + g.stderr
with tempfile.TemporaryDirectory() as tmp:
    r = subprocess.run([str(exe), '--partition', str(hangtime_paths.rom_path()), tmp], capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    sym = (Path(tmp) / 'full.symbols.toml').read_bytes()
    hooks = (Path(tmp) / 'full.hooks.toml').read_bytes()
# The generators write text files (CRLF on Windows); compare with LF line ends.
CRLF, LF = bytes([13, 10]), bytes([10])
py_sym = (P / 'full.symbols.toml').read_bytes().replace(CRLF, LF)
py_full = (P / 'full.toml').read_bytes().replace(CRLF, LF)
py_hooks = py_full[py_full.index(b'[patches]'):]
assert sym == py_sym, 'full.symbols.toml differs between partition.cpp and prepare_full.py'
assert hooks == py_hooks, 'hooks differ between partition.cpp and prepare_full.py'
print(f'PASS: {r.stdout.strip()}; full.symbols.toml ({len(sym)} bytes) and hooks ({len(hooks)} bytes) identical to the generators')
