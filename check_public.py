"""Content checks for the public source tree (export_public.py runs them; so does CI).

usage: python check_public.py [DIR] [--release]
       python check_public.py --make-fingerprints   (maintainers: needs the ROM, see hangtime_paths.py)

Fails on:
  - ROM data: any 16-byte window of the ROM at the sampled offsets in tools/rom_fingerprints.dat
    (truncated SHA-256 of every 128th aligned 16-byte ROM window; one-way, contains no ROM bytes),
    searched at every byte offset of every file, raw and as hex words (`0x1234ABCD`, `1234abcd`)
  - ROM images, archives, executables, libraries, objects, debug databases, captures (by name)
  - generated recompiled code (N64Recomp output markers) and build products
  - binary files outside an explicit allowlist; files over 1 MB outside vendor/
  - secrets (tokens, private keys), absolute personal paths, e-mail addresses not on the allowlist
  - --release: also a missing LICENSE / THIRD_PARTY_NOTICES.md or a license still marked pending
These checks supplement manual and legal review; passing them proves nothing about rights.
"""
import hashlib, os, re, sys
from pathlib import Path

P = Path(__file__).resolve().parent
FP = Path('tools/rom_fingerprints.dat')
FORBIDDEN_EXT = {'.z64', '.n64', '.v64', '.rom', '.bin', '.exe', '.dll', '.so', '.dylib', '.lib', '.a', '.o', '.obj',
                 '.pdb', '.ilk', '.map', '.exp', '.zip', '.7z', '.rar', '.gz', '.xz', '.tar', '.ppm', '.pcm', '.wav',
                 '.png', '.jpg', '.jpeg', '.bmp', '.gif', '.csv', '.ram', '.sav', '.eep', '.mpk'}
FORBIDDEN_NAME = re.compile(r'(^|/)(full_generated|generated|build|captures|test_logs|research|backups)(/|$)|'
                            r'audio_rsp_generated\.cpp$|recomp_overlays\.inl$|funcs_\d+\.c$|hangtime\.ini$|'
                            r'full\.toml$|full\.symbols\.toml$|full_inventory\.json$', re.I)
BINARY_ALLOW = {FP.as_posix()}
# N64Recomp output: function definitions, and its per-instruction disassembly comments.
GENERATED = re.compile(rb'RECOMP_FUNC\s+void\s+func_[0-9A-F]{8}\s*\(|(//\s0x8[0-9A-F]{7}:\s+[a-z][^\n]*\n[^\n]*\n){3}', re.I)
SECRET = re.compile(rb'(gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}|AKIA[0-9A-Z]{16}|'
                    rb'-----BEGIN [A-Z ]*PRIVATE KEY|xox[baprs]-[A-Za-z0-9-]{10,}|sk-[A-Za-z0-9]{32,})')
PERSONAL = re.compile(rb'[A-Za-z]:[\\/]+Users[\\/]+[^\\/\s"\'<>]+|/home/[a-z_][a-z0-9_-]*/|/mnt/[a-z]/|/Users/[A-Za-z][^/\s]*/')
PERSONAL_OK = re.compile(rb'Users[\\/]+(<[^>]+>|you|USER|%USERNAME%|\$USER|username|name)[\\/]', re.I)
EMAIL = re.compile(rb'[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}')
EMAIL_OK = re.compile(rb'(noreply@anthropic\.com|@users\.noreply\.github\.com|noreply@github\.com|@example\.(com|org)|'
                      rb'git@github\.com)$', re.I)
HEXWORD = re.compile(rb'(?<![0-9A-Fa-f])(?:0x)?([0-9A-Fa-f]{8})(?![0-9A-Fa-f])')

def sample_offsets(rom):
    for o in range(0, len(rom) - 16, 128):
        w = rom[o:o + 16]
        if w.count(0) <= 6 and w.count(0xFF) <= 6 and len(set(w)) >= 6: yield o

def make_fingerprints():
    sys.path.insert(0, str(P)); import hangtime_paths
    rom = hangtime_paths.rom_bytes()
    fps = sorted({hashlib.sha256(rom[o:o + 16]).digest()[:8] for o in sample_offsets(rom)})
    (P / FP).parent.mkdir(exist_ok=True)
    (P / FP).write_bytes(b''.join(fps))
    print(f'{FP}: {len(fps)} fingerprints of 16-byte ROM windows (every 128 bytes)')

def rom_hits(data, fps):
    """Offsets where a 16-byte window's truncated SHA-256 is a ROM fingerprint."""
    sha = hashlib.sha256
    return [i for i in range(len(data) - 15) if sha(data[i:i + 16]).digest()[:8] in fps]

def main():
    if '--make-fingerprints' in sys.argv: return make_fingerprints()
    pos = [a for a in sys.argv[1:] if not a.startswith('--')]
    root = Path(pos[0]).resolve() if pos else P
    fpfile = root / FP if (root / FP).exists() else P / FP
    raw = fpfile.read_bytes() if fpfile.exists() else b''
    fps = {raw[i:i + 8] for i in range(0, len(raw), 8)}
    problems, n = [], 0
    skip_dirs = {'.git', 'external'}
    for dirpath, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in skip_dirs or Path(dirpath) != root]
        for name in files:
            f = Path(dirpath) / name
            rel = f.relative_to(root).as_posix()
            n += 1
            data = f.read_bytes()
            bad = lambda why: problems.append(f'{rel}: {why}')
            if rel in BINARY_ALLOW: continue
            if f.suffix.lower() in FORBIDDEN_EXT: bad(f'forbidden file type {f.suffix}')
            if FORBIDDEN_NAME.search(rel): bad('generated, build, capture or private path')
            if len(data) > 1_000_000 and not rel.startswith('vendor/'): bad(f'{len(data) // 1024} KiB (over 1 MB)')
            if b'\0' in data[:8192]: bad('binary content')
            if GENERATED.search(data): bad('recompiled (N64Recomp-generated) code')
            for m in SECRET.finditer(data): bad(f'secret-like string {m.group(0)[:12]!r}...')
            for m in PERSONAL.finditer(data):
                if not PERSONAL_OK.search(m.group(0)): bad(f'personal/absolute path {m.group(0)[:40]!r}')
            for m in EMAIL.finditer(data):
                if not EMAIL_OK.search(m.group(0)) and not rel.startswith('vendor/'): bad(f'e-mail address {m.group(0)!r}')
            if fps:
                hits = rom_hits(data, fps)
                words = b''.join(bytes.fromhex(m.decode()) for m in HEXWORD.findall(data))
                hex_hits = rom_hits(words, fps) if len(words) >= 16 else []
                if hits: bad(f'{len(hits)} ROM data windows (first at byte {hits[0]})')
                if hex_hits: bad(f'{len(hex_hits)} ROM data windows written as hex words')
    if '--release' in sys.argv:
        for req in ('LICENSE', 'THIRD_PARTY_NOTICES.md'):
            if not (root / req).exists(): problems.append(f'{req}: missing')
        lic = root / 'LICENSE'
        if lic.exists() and b'PENDING' in lic.read_bytes()[:400]: problems.append('LICENSE: still pending the owner\'s decision')
    print(f'check_public: {n} files in {root}; ROM fingerprints: {len(fps) or "none (ROM scan skipped)"}')
    for p in problems: print('  FAIL', p)
    if problems: sys.exit(f'check_public: {len(problems)} problem(s)')
    print('check_public: PASS (automated checks only; not a legal review)')

if __name__ == '__main__':
    main()
