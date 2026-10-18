"""Generate the whole-program N64Recomp configuration (full.toml / full.symbols.toml).

Every function of the CPU text partition (full_inventory.json) is listed. libultra routines get
their libultra names (libultra_map.py) so N64Recomp maps them to N64ModernRuntime
reimplementations or skips them; all others are recompiled as func_XXXXXXXX.
Run full_inventory.py first. Output code goes to full_generated/ (build product, git-ignored).
"""
import json, sys
from pathlib import Path

P = Path(__file__).resolve().parent
sys.path.insert(0, str(P))
from libultra_map import LIBULTRA
import hangtime_paths
import review_function as rf

ROM = hangtime_paths.rom_z64_path(P / 'full_generated')   # checked big-endian image (hangtime_paths.py)
inv = json.loads((P / 'full_inventory.json').read_text())
funcs = [(int(a, 16), int(b, 16)) for a, b in inv['ranges']]
starts = {s for s, _ in funcs}
missing = [hex(a) for a in LIBULTRA if a not in starts]
assert not missing, f'libultra names on non-function addresses: {missing}'

# The game's own process switching (verified by the harness, PROCESS_RESTORE.md): save stays
# recompiled; restore and stack-enter are replaced by the fiber-based process_context.
SAVE, RESTORE, ENTER = 0x80068C40, 0x80068C8C, 0x80068D0C
REPLACED = {RESTORE: 'main_context_restore', ENTER: 'main_stack_enter'}

def name(a):
    if a in REPLACED: return REPLACED[a]
    return LIBULTRA[a][0] if a in LIBULTRA else f'func_{a:08X}'

entries = [f'{{name="{name(s)}",vram=0x{s:X},size=0x{e - s:X}}}' for s, e in funcs]
# One section: the IPL3 image's initialized part (text + rodata/data), ROM 0x1000 -> 0x80000400.
(P / 'full.symbols.toml').write_text(
    '[[section]]\nname="hangtime_image"\nrom=0x1000\nvram=0x80000400\nsize=0xA3480\nfunctions=[\n'
    + ',\n'.join(entries) + '\n]\n')

def jal_target(pc, w): return ((pc + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
def is_jalr(w): return (w >> 26) == 0 and (w & 63) == 9
def is_jump_or_branch(w):
    op = w >> 26
    return rf.branch_target(0, w) is not None or op in (2, 3) or (op == 0 and (w & 63) in (8, 9))

# Hook generation ported from prepare_graphics.py, applied to every recompiled function:
# safe points on backward branches, frame invalidation at jr ra, guest link + resume label
# around calls to SAVE, and an unwinding check after every other call.
hook_text = {}
def add(fn, pc, text): hook_text.setdefault((fn, pc), []).append(text)
counts = dict(safe_points=0, saves=0, calls=0, returns=0)
for s, e in funcs:
    if s in LIBULTRA or s in REPLACED: continue
    fn = name(s)
    resume = [pc + 8 for pc in range(s, e, 4) if (rf.word(pc) >> 26) == 3 and jal_target(pc, rf.word(pc)) == SAVE]
    cases = ''.join(f'case 0x{r:08X}u: goto R_{r:08X}; ' for r in resume)
    for pc in range(s, e, 4):
        w = rf.word(pc); op = w >> 26
        t = rf.branch_target(pc, w)
        if t is not None and s <= t <= pc:
            add(fn, pc, f'hangtime_safe_point(rdram, ctx, 0x{pc:X}u);'); counts['safe_points'] += 1
        if w == 0x03E00008:
            add(fn, pc, 'HT_FORGET_FRAME();'); counts['returns'] += 1
        if not (op == 3 or is_jalr(w)): continue
        assert pc == s or not is_jump_or_branch(rf.word(pc - 4)), f'call in delay slot {fn} {pc:#x}'
        if pc + 8 >= e: continue  # tail position: nothing follows the call in this function
        tgt = jal_target(pc, w) if op == 3 else None
        if tgt == SAVE:
            add(fn, pc, f'ctx->r31 = (int64_t)(int32_t)0x{pc + 8:X};')
            add(fn, pc + 8, f'hangtime_note_save(rdram, ctx, 0x{pc + 8:08X}u, (uintptr_t)&hi); R_{pc + 8:08X}: ;')
            counts['saves'] += 1
            continue
        add(fn, pc + 8, f'HT_AFTER_CALL_RESUMABLE({cases});' if cases else 'HT_AFTER_CALL();'); counts['calls'] += 1

# Optional widescreen presentation (full_runtime/widescreen.cpp, inert unless --widescreen): frame
# start, floor draw and the sprite dispatcher's entry and single return. Placed first at a pc.
WIDESCREEN = {0x8005643C: 'hangtime_ws_frame_begin(rdram, ctx);', 0x80061594: 'hangtime_ws_floor(rdram, ctx);',
              0x8005B770: 'hangtime_ws_sprite_begin(rdram, ctx);', 0x8005C140: 'hangtime_ws_sprite_end(rdram, ctx);'}
func_of = {pc: next(s for s, e in funcs if s <= pc < e) for pc in WIDESCREEN}
assert rf.word(0x8005C140) == 0x03E00008 and all(func_of[pc] == 0x8005B770 for pc in (0x8005B770, 0x8005C140))
for pc, text in WIDESCREEN.items():
    hook_text.setdefault((name(func_of[pc]), pc), []).insert(0, text)
# Object draw routines (class tables at 0x8009A840.., draw(object, view)) record the object and the
# view they draw with, so dispatcher calls can be attributed to world or screen (HUD) layers.
for pc in (0x8005FFAC, 0x800606D4, 0x80061BEC, 0x8006243C):
    hook_text.setdefault((name(pc), pc), []).insert(0, f'hangtime_ws_object(rdram, ctx->r4, 0x{pc:08X}u);')
# The player draw routine's own whole-player screen cull (right edge < 0 at 0x80060AD0, left edge
# >= 320 at 0x80060AE0/E4): widened to the 16:9 view, original registers restored after the tests
# (fall through 0x80060AEC, skip target / function exit 0x80061478). Inert unless --widescreen.
assert all(rf.word(pc) == w for pc, w in ((0x80060AC8, 0x8DD90000), (0x80060AD0, 0x07200269), (0x80060AD8, 0x8F0F0000),
                                          (0x80060AE0, 0x29E10140), (0x80060AE4, 0x10200264), (0x80061478, 0x27BD00A0)))
assert any(s == 0x800606D4 and e == 0x80061484 for s, e in funcs)
for pc, step in ((0x80060AD0, 0), (0x80060AE0, 1), (0x80060AEC, 2), (0x80061478, 2)):
    hook_text.setdefault((name(0x800606D4), pc), []).insert(0, f'hangtime_ws_player_cull(rdram, ctx, {step});')
# Cheats (full_runtime/cheats.cpp, inert unless a cheat is enabled): the matchup screen's code
# application, where the game itself applies matchup and button-sequence cheats.
hook_text.setdefault((name(0x80048E60), 0x80048E60), []).insert(0, 'hangtime_cheats_matchup(rdram, ctx);')
# Each call site of the dispatcher records its address (generated code does not set ra on jal).
dispatch_calls = [pc for s, e in funcs for pc in range(s, e, 4) if (rf.word(pc) >> 26) == 3 and jal_target(pc, rf.word(pc)) == 0x8005B770]
assert len(dispatch_calls) == 12, dispatch_calls
for pc in dispatch_calls:
    s = next(s for s, e in funcs if s <= pc < e)
    hook_text.setdefault((name(s), pc), []).insert(0, f'hangtime_ws_caller(0x{pc:08X}u);')

hooks = []
for (fn, pc), texts in hook_text.items():
    texts.sort(key=lambda t: t.startswith('hangtime_safe_point'))
    body = ' '.join(texts).replace('\\', '\\\\').replace('"', '\\"')
    hooks.append(f'[[patches.hook]]\nfunc="{fn}"\nbefore_vram=0x{pc:X}\ntext="{body}"\n')
(P / 'full.toml').write_text(
    '[input]\nsymbols_file_path="full.symbols.toml"\n'
    f"rom_file_path='{ROM.as_posix()}'\n"
    'output_func_path="full_generated"\n'
    'recomp_include=\'#include "hangtime_full.h"\'\n\n'
    '[patches]\nignored=["main_context_restore","main_stack_enter"]\n\n'
    + '\n'.join(hooks))
# The audio microcode configuration with this machine's paths (for RSPRecomp; a build product).
audio = (P / 'audio_rsp.toml').read_text()
audio = '\n'.join(f"rom_file_path = '{ROM.as_posix()}'" if l.startswith('rom_file_path') else
                  f"output_file_path = '{(P / 'audio_rsp_generated.cpp').as_posix()}'" if l.startswith('output_file_path') else l
                  for l in audio.splitlines()) + '\n'
(P / 'full_generated' / 'audio_rsp.toml').write_text(audio)
print(f'PASS: {len(funcs)} functions, {len(LIBULTRA)} libultra names, {counts}')
