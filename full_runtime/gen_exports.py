"""Windows build step: the executable's export list for the game module (game_module.hpp).

usage: python gen_exports.py DUMPBIN OUT.def --game OBJ... --runtime OBJ...

Exports exactly the symbols the game module's objects use but do not define, and that the
runtime's objects define (C functions such as the libultra replacements and hangtime_* hooks,
plus librecomp's RSP tables as DATA). Everything else the module needs comes from its own C
runtime library. On Linux the executable exports its symbols to the module dynamically instead.
"""
import subprocess, sys

def symbols(dumpbin, objs):
    defined, undefined, data = set(), set(), set()
    for i in range(0, len(objs), 40):
        out = subprocess.run([dumpbin, '/nologo', '/symbols', *objs[i:i + 40]], capture_output=True, text=True, errors='replace').stdout
        for line in out.splitlines():
            if '| ' not in line or 'External' not in line: continue
            left, name = line.split('| ', 1)
            name = name.split(' ')[0]
            if ' UNDEF ' in left: undefined.add(name)
            elif ' SECT' in left:
                defined.add(name)
                if '()' not in left: data.add(name)
    return defined, undefined - defined, data

args = sys.argv[1:]
dumpbin, out = args[0], args[1]
game = args[args.index('--game') + 1:args.index('--runtime')]
runtime = args[args.index('--runtime') + 1:]
g_def, g_undef, _ = symbols(dumpbin, game)
r_def, _, r_data = symbols(dumpbin, runtime)
exports = sorted(g_undef & r_def)
with open(out, 'w') as f:
    f.write('EXPORTS\n')
    for name in exports: f.write(f'    {name}{" DATA" if name in r_data else ""}\n')
print(f'gen_exports: {len(exports)} runtime symbols exported to the game module ({sum(1 for n in exports if n in r_data)} data)')
