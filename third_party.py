"""Third-party components: one list for THIRD_PARTY_NOTICES.md and the release packages.

usage: python third_party.py [--notices OUT.md]   (default: print the table)

Each entry: name, version, SPDX license, copyright, where it ends up, and the license text
(root key + path; roots from hangtime_paths.py). Products:
  runtime  compiled into hangtime[.exe] (distributed)
  package  shipped as a separate file in release packages
  module   compiled into hangtime_game.dll/.so on the player's machine (never distributed)
  source   this repository contains it as source only
Sources of truth: the license files themselves (surveyed 2026-10-19; RELEASE_LEGAL.md).
"""
import sys
from pathlib import Path

P = Path(__file__).resolve().parent
sys.path.insert(0, str(P))
import hangtime_paths

def roots():
    return {'vendor': P / 'vendor' / 'N64ModernRuntime', 'rt64': hangtime_paths.rt64_dir(),
            'n64recomp': hangtime_paths.n64recomp_dir(), 'native': P}

C = 'src/contrib'
# (name, version, spdx, copyright, products, (root, license path) or None, note)
COMPONENTS = [
    ('N64ModernRuntime (ultramodern, librecomp)', 'cdf5abb', 'GPL-3.0', 'N64ModernRuntime contributors',
     'runtime, module', ('vendor', 'COPYING'), 'No version qualifier is stated; GPL-3.0 section 14 then allows any version.'),
    ('ares RSP vector unit (in librecomp rsp_vu.hpp)', '', 'ISC', 'Copyright (c) 2004-2021 ares team, Near et al',
     'runtime, module', ('vendor', 'librecomp/include/librecomp/rsp_vu.hpp'), 'License in the file header.'),
    ('nlohmann/json', '3.12.0', 'MIT', 'Copyright (c) 2013-2025 Niels Lohmann', 'runtime',
     ('vendor', 'thirdparty/json/json.hpp'), 'License in the file header.'),
    ('moodycamel ConcurrentQueue', '', 'BSD-2-Clause OR BSL-1.0', 'Copyright (c) 2013-2020 Cameron Desrochers',
     'runtime', ('vendor', 'thirdparty/concurrentqueue/concurrentqueue.h'), 'Used under BSD-2-Clause; license in the file header.'),
    ('Preshing lightweight semaphore (in ConcurrentQueue)', '', 'Zlib', 'Copyright (c) 2015 Jeff Preshing',
     'runtime', ('vendor', 'thirdparty/concurrentqueue/lightweightsemaphore.h'), 'License in the file header.'),
    ('RT64', 'de343a5', 'MIT', 'Copyright (c) 2024 RT64 Contributors', 'runtime', ('rt64', 'LICENSE'), ''),
    ('plume', 'd890ac8', 'MIT', 'Copyright (c) 2024 renderbag and contributors', 'runtime', ('rt64', f'{C}/plume/LICENSE'), ''),
    ('volk', '', 'MIT', 'Copyright (c) 2018-2025 Arseny Kapoulkine', 'runtime', ('rt64', f'{C}/plume/contrib/volk/LICENSE.md'), ''),
    ('Vulkan Memory Allocator', '', 'MIT', 'Copyright (c) 2017-2025 Advanced Micro Devices, Inc.', 'runtime',
     ('rt64', f'{C}/plume/contrib/VulkanMemoryAllocator/LICENSE.txt'), ''),
    ('D3D12 Memory Allocator', '', 'MIT', 'Copyright (c) 2019-2025 Advanced Micro Devices, Inc.', 'runtime (Windows)',
     ('rt64', f'{C}/plume/contrib/D3D12MemoryAllocator/LICENSE.txt'), 'Also NOTICES.txt in the same folder.'),
    ('Vulkan-Headers', '', 'Apache-2.0', 'Copyright 2015-2025 The Khronos Group Inc.', 'runtime',
     ('rt64', f'{C}/plume/contrib/Vulkan-Headers/LICENSE.md'), ''),
    ('re-spirv', '5d6b756', 'MIT', 'Copyright (c) 2024 renderbag and contributors', 'runtime', ('rt64', f'{C}/re-spirv/LICENSE'), ''),
    ('SPIRV-Headers', '', 'MIT-Khronos-old', 'Copyright (c) 2015-2024 The Khronos Group Inc.', 'runtime',
     ('rt64', f'{C}/re-spirv/external/SPIRV-Headers/LICENSE'), ''),
    ('HLSL++', '3.6', 'MIT', 'Copyright (c) 2017-2024 Emilio Lopez', 'runtime', ('rt64', f'{C}/hlslpp/LICENSE'), ''),
    ('ddspp', '1.11', 'MIT', 'Copyright (c) 2018-2023 Emilio Lopez', 'runtime', ('rt64', f'{C}/ddspp/LICENSE'), ''),
    ('Dear ImGui', '', 'MIT', 'Copyright (c) 2014-2026 Omar Cornut', 'runtime', ('rt64', f'{C}/imgui/LICENSE.txt'), ''),
    ('ImPlot', '', 'MIT', 'Copyright (c) 2020 Evan Pezent', 'runtime', ('rt64', f'{C}/implot/LICENSE'), ''),
    ('im3d', '', 'MIT', 'Copyright (c) 2016-2022 John Chapman', 'runtime', ('rt64', f'{C}/im3d/LICENSE'), ''),
    ('stb', '', 'MIT OR Unlicense', 'Copyright (c) 2017 Sean Barrett', 'runtime', ('rt64', f'{C}/stb/LICENSE'), 'Used under MIT.'),
    ('xxHash', '', 'BSD-2-Clause', 'Copyright (c) 2012-2021 Yann Collet', 'runtime', ('rt64', f'{C}/xxHash/LICENSE'), ''),
    ('zstd', '', 'BSD-3-Clause OR GPL-2.0-only', 'Copyright (c) Meta Platforms, Inc. and affiliates', 'runtime',
     ('rt64', f'{C}/zstd/LICENSE'), 'Used under BSD-3-Clause.'),
    ('miniz', '3.0.0', 'MIT', 'Copyright 2013-2014 RAD Game Tools and Valve Software; 2010-2014 Rich Geldreich and Tenacious Software LLC',
     'runtime', ('rt64', f'{C}/miniz/miniz.c'), 'License in the file header.'),
    ('nativefiledialog-extended', '1.1.1', 'Zlib', 'Bernard Teo, Michael Labbe', 'runtime',
     ('rt64', f'{C}/nativefiledialog-extended/LICENSE'), ''),
    ('utf8conv', '', 'MIT', 'Copyright (c) 2016-2022 by Giovanni Dicanio', 'runtime (Windows)',
     ('native', 'third_party/utf8conv/LICENSE'), 'License from the author\'s repository GiovanniDicanio/Utf8Conv (third_party/utf8conv/SOURCE.txt).'),
    ('SDL2', '2.26.3', 'Zlib', 'Copyright (C) 1997-2023 Sam Lantinga', 'runtime (Windows: SDL2.dll in packages; Linux: system library)',
     ('rt64', f'{C}/mupen64plus-win32-deps/SDL2-2.26.3/COPYING.txt'), 'README-SDL.txt is shipped with SDL2.dll.'),
    ('DirectX Shader Compiler (dxcompiler.dll)', '1.7.0.4147', 'NCSA', 'Copyright (c) 2003-2015 University of Illinois at Urbana-Champaign; Microsoft Corporation',
     'package (Windows)', ('native', 'third_party/DirectXShaderCompiler/LICENSE-dxcompiler.txt'),
     'Binary from RT64 (src/contrib/dxc); license texts from DXC release v1.7.2212 (third_party/DirectXShaderCompiler/SOURCE.txt).'),
    ('DXIL signing library (dxil.dll)', '101.7.2212.14', 'LicenseRef-Microsoft-DXC (proprietary)', 'Copyright (c) Microsoft Corporation',
     'package (Windows)', ('native', 'third_party/DirectXShaderCompiler/LICENSE-MS.txt'),
     'Binary from RT64 (src/contrib/dxc), identical to DXC release v1.7.2212; Microsoft\'s terms in LICENSE-MS.txt. Shipped as other RT64-based projects do (third_party/DirectXShaderCompiler/SOURCE.txt).'),
    ('SDL_GameControllerDB (gamecontrollerdb.txt)', 'c1d5289', 'Zlib', 'Copyright (C) 1997-2025 Sam Lantinga and contributors',
     'package (beside the program)', ('native', 'third_party/SDL_GameControllerDB/LICENSE'),
     'Community controller mappings, loaded at startup (main.cpp); source and commit in third_party/SDL_GameControllerDB/SOURCE.txt.'),
    ('Roboto Medium font', '2.001152', 'Apache-2.0', 'Copyright 2011 Google Inc.', 'package (assets/)',
     ('rt64', f'{C}/plume/contrib/Vulkan-Headers/LICENSES/Apache-2.0.txt'), 'Apache-2.0 text (same license); "Roboto" is a trademark of Google.'),
    ('N64Recomp, RSPRecomp', 'ffb39cd', 'MIT', 'Copyright (c) 2024 Wiseguy', 'package (tools/), source (setup/sdk recomp.h)', ('n64recomp', 'LICENSE'), ''),
    ('fmt', '12.1.0', 'MIT', 'Copyright (c) 2012-present Victor Zverovich and {fmt} contributors', 'package (in tools/)', ('n64recomp', 'lib/fmt/LICENSE'), ''),
    ('rabbitizer', '1.7.10', 'MIT', 'Copyright (c) 2022 Decompollaborate', 'package (in tools/)', ('n64recomp', 'lib/rabbitizer/LICENSE'), ''),
    ('toml++', '3.4.0', 'MIT', 'Copyright (c) Mark Gillard', 'package (in tools/)', ('n64recomp', 'lib/tomlplusplus/LICENSE'), ''),
    ('ELFIO', '', 'MIT', 'Copyright (C) 2001-present Serge Lamikhov-Center', 'package (in tools/)', ('n64recomp', 'lib/ELFIO/LICENSE.txt'), ''),
    ('Zig', '0.17.0', 'MIT (bundled libraries under their own licenses: LLVM libc++/libunwind/compiler-rt Apache-2.0 WITH LLVM-exception, '
     'mingw-w64 ZPL-2.1 and others, glibc LGPL-2.1-or-later, Linux headers GPL-2.0 WITH Linux-syscall-note)', 'Copyright (c) Zig contributors',
     'package (tools/zig), module (runtime libraries linked into it)', None,
     'Packaged unmodified with its own license files (tools/zig; mingw-w64: tools/zig/lib/libc/mingw/COPYING); packages add the LGPL-2.1 and GPL-2.0 texts (LICENSES/).'),
]

def table():
    rows = ['| Component | Version | License | Copyright | Used in | Notes |', '|---|---|---|---|---|---|']
    for name, ver, spdx, cr, prod, lic, note in COMPONENTS:
        where = f' ({lic[0]}: `{lic[1]}`)' if lic else ''
        rows.append(f'| {name} | {ver} | {spdx} | {cr} | {prod} | {(note + where).strip()} |')
    return '\n'.join(rows)

NOTICES = """# Third-party notices

{{PROJECT_NAME}} contains or is built with the following third-party software. Each keeps its
own license; the texts are in the named files of this repository, its submodules (`external/`)
or, in release packages, in `LICENSES/`. Items marked OPEN are being resolved before the first
release (docs/LICENSING.md).

Products: **runtime** = compiled into `hangtime[.exe]`; **package** = a separate file in release
packages; **module** = compiled into the game module on the player's own machine (never
distributed); **source** = included as source.

{table}

Operating-system libraries are used, not distributed: Direct3D 12, DXGI and the Microsoft Visual
C++ runtime on Windows; SDL2 (Zlib), GTK 3 (LGPL-2.1-or-later), the Vulkan loader (Apache-2.0)
and the C/C++ runtime on Linux.
"""

def main():
    if '--notices' in sys.argv:
        out = Path(sys.argv[sys.argv.index('--notices') + 1])
        out.write_text(NOTICES.replace('{table}', table()), encoding='utf-8')
        print(f'wrote {out} ({len(COMPONENTS)} components)')
    else:
        print(table())
    r = roots()
    missing = [f'{n}: {lic[0]}/{lic[1]}' for n, _, _, _, _, lic, _ in COMPONENTS if lic and not (r[lic[0]] / lic[1]).exists()]
    if missing: print('license files not found:\n  ' + '\n  '.join(missing))

if __name__ == '__main__':
    main()
