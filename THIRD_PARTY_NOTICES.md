# Third-party notices

HangtimeRecomp contains or is built with the following third-party software. Each keeps its
own license; the texts are in the named files of this repository, its submodules (`external/`)
or, in release packages, in `LICENSES/`. Items marked OPEN are being resolved before the first
release (docs/LICENSING.md).

Products: **runtime** = compiled into `hangtime[.exe]`; **package** = a separate file in release
packages; **module** = compiled into the game module on the player's own machine (never
distributed); **source** = included as source.

| Component | Version | License | Copyright | Used in | Notes |
|---|---|---|---|---|---|
| N64ModernRuntime (ultramodern, librecomp) | cdf5abb | GPL-3.0 | N64ModernRuntime contributors | runtime, module | No version qualifier is stated; GPL-3.0 section 14 then allows any version. (vendor: `COPYING`) |
| ares RSP vector unit (in librecomp rsp_vu.hpp) |  | ISC | Copyright (c) 2004-2021 ares team, Near et al | runtime, module | License in the file header. (vendor: `librecomp/include/librecomp/rsp_vu.hpp`) |
| nlohmann/json | 3.12.0 | MIT | Copyright (c) 2013-2025 Niels Lohmann | runtime | License in the file header. (vendor: `thirdparty/json/json.hpp`) |
| moodycamel ConcurrentQueue |  | BSD-2-Clause OR BSL-1.0 | Copyright (c) 2013-2020 Cameron Desrochers | runtime | Used under BSD-2-Clause; license in the file header. (vendor: `thirdparty/concurrentqueue/concurrentqueue.h`) |
| Preshing lightweight semaphore (in ConcurrentQueue) |  | Zlib | Copyright (c) 2015 Jeff Preshing | runtime | License in the file header. (vendor: `thirdparty/concurrentqueue/lightweightsemaphore.h`) |
| RT64 | de343a5 | MIT | Copyright (c) 2024 RT64 Contributors | runtime | (rt64: `LICENSE`) |
| plume | d890ac8 | MIT | Copyright (c) 2024 renderbag and contributors | runtime | (rt64: `src/contrib/plume/LICENSE`) |
| volk |  | MIT | Copyright (c) 2018-2025 Arseny Kapoulkine | runtime | (rt64: `src/contrib/plume/contrib/volk/LICENSE.md`) |
| Vulkan Memory Allocator |  | MIT | Copyright (c) 2017-2025 Advanced Micro Devices, Inc. | runtime | (rt64: `src/contrib/plume/contrib/VulkanMemoryAllocator/LICENSE.txt`) |
| D3D12 Memory Allocator |  | MIT | Copyright (c) 2019-2025 Advanced Micro Devices, Inc. | runtime (Windows) | Also NOTICES.txt in the same folder. (rt64: `src/contrib/plume/contrib/D3D12MemoryAllocator/LICENSE.txt`) |
| Vulkan-Headers |  | Apache-2.0 | Copyright 2015-2025 The Khronos Group Inc. | runtime | (rt64: `src/contrib/plume/contrib/Vulkan-Headers/LICENSE.md`) |
| re-spirv | 5d6b756 | MIT | Copyright (c) 2024 renderbag and contributors | runtime | (rt64: `src/contrib/re-spirv/LICENSE`) |
| SPIRV-Headers |  | MIT-Khronos-old | Copyright (c) 2015-2024 The Khronos Group Inc. | runtime | (rt64: `src/contrib/re-spirv/external/SPIRV-Headers/LICENSE`) |
| HLSL++ | 3.6 | MIT | Copyright (c) 2017-2024 Emilio Lopez | runtime | (rt64: `src/contrib/hlslpp/LICENSE`) |
| ddspp | 1.11 | MIT | Copyright (c) 2018-2023 Emilio Lopez | runtime | (rt64: `src/contrib/ddspp/LICENSE`) |
| Dear ImGui |  | MIT | Copyright (c) 2014-2026 Omar Cornut | runtime | (rt64: `src/contrib/imgui/LICENSE.txt`) |
| ImPlot |  | MIT | Copyright (c) 2020 Evan Pezent | runtime | (rt64: `src/contrib/implot/LICENSE`) |
| im3d |  | MIT | Copyright (c) 2016-2022 John Chapman | runtime | (rt64: `src/contrib/im3d/LICENSE`) |
| stb |  | MIT OR Unlicense | Copyright (c) 2017 Sean Barrett | runtime | Used under MIT. (rt64: `src/contrib/stb/LICENSE`) |
| xxHash |  | BSD-2-Clause | Copyright (c) 2012-2021 Yann Collet | runtime | (rt64: `src/contrib/xxHash/LICENSE`) |
| zstd |  | BSD-3-Clause OR GPL-2.0-only | Copyright (c) Meta Platforms, Inc. and affiliates | runtime | Used under BSD-3-Clause. (rt64: `src/contrib/zstd/LICENSE`) |
| miniz | 3.0.0 | MIT | Copyright 2013-2014 RAD Game Tools and Valve Software; 2010-2014 Rich Geldreich and Tenacious Software LLC | runtime | License in the file header. (rt64: `src/contrib/miniz/miniz.c`) |
| nativefiledialog-extended | 1.1.1 | Zlib | Bernard Teo, Michael Labbe | runtime | (rt64: `src/contrib/nativefiledialog-extended/LICENSE`) |
| utf8conv |  | MIT | Copyright (c) 2016-2022 by Giovanni Dicanio | runtime (Windows) | License from the author's repository GiovanniDicanio/Utf8Conv (third_party/utf8conv/SOURCE.txt). (native: `third_party/utf8conv/LICENSE`) |
| SDL2 | 2.26.3 | Zlib | Copyright (C) 1997-2023 Sam Lantinga | runtime (Windows: SDL2.dll in packages; Linux: system library) | README-SDL.txt is shipped with SDL2.dll. (rt64: `src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/COPYING.txt`) |
| DirectX Shader Compiler (dxcompiler.dll) | 1.7.0.4147 | NCSA | Copyright (c) 2003-2015 University of Illinois at Urbana-Champaign; Microsoft Corporation | package (Windows) | Binary from RT64 (src/contrib/dxc); license texts from DXC release v1.7.2212 (third_party/DirectXShaderCompiler/SOURCE.txt). (native: `third_party/DirectXShaderCompiler/LICENSE-dxcompiler.txt`) |
| DXIL signing library (dxil.dll) | 101.7.2212.14 | LicenseRef-Microsoft-DXC (proprietary) | Copyright (c) Microsoft Corporation | package (Windows) | Binary from RT64 (src/contrib/dxc), identical to DXC release v1.7.2212; Microsoft's terms in LICENSE-MS.txt. Shipped as other RT64-based projects do (third_party/DirectXShaderCompiler/SOURCE.txt). (native: `third_party/DirectXShaderCompiler/LICENSE-MS.txt`) |
| SDL_GameControllerDB (gamecontrollerdb.txt) | c1d5289 | Zlib | Copyright (C) 1997-2025 Sam Lantinga and contributors | package (beside the program) | Community controller mappings, loaded at startup (main.cpp); source and commit in third_party/SDL_GameControllerDB/SOURCE.txt. (native: `third_party/SDL_GameControllerDB/LICENSE`) |
| Roboto Medium font | 2.001152 | Apache-2.0 | Copyright 2011 Google Inc. | package (assets/) | Apache-2.0 text (same license); "Roboto" is a trademark of Google. (rt64: `src/contrib/plume/contrib/Vulkan-Headers/LICENSES/Apache-2.0.txt`) |
| N64Recomp, RSPRecomp | ffb39cd | MIT | Copyright (c) 2024 Wiseguy | package (tools/), source (setup/sdk recomp.h) | (n64recomp: `LICENSE`) |
| fmt | 12.1.0 | MIT | Copyright (c) 2012-present Victor Zverovich and {fmt} contributors | package (in tools/) | (n64recomp: `lib/fmt/LICENSE`) |
| rabbitizer | 1.7.10 | MIT | Copyright (c) 2022 Decompollaborate | package (in tools/) | (n64recomp: `lib/rabbitizer/LICENSE`) |
| toml++ | 3.4.0 | MIT | Copyright (c) Mark Gillard | package (in tools/) | (n64recomp: `lib/tomlplusplus/LICENSE`) |
| ELFIO |  | MIT | Copyright (C) 2001-present Serge Lamikhov-Center | package (in tools/) | (n64recomp: `lib/ELFIO/LICENSE.txt`) |
| Zig | 0.17.0 | MIT (bundled libraries under their own licenses: LLVM libc++/libunwind/compiler-rt Apache-2.0 WITH LLVM-exception, mingw-w64 ZPL-2.1 and others, glibc LGPL-2.1-or-later, Linux headers GPL-2.0 WITH Linux-syscall-note) | Copyright (c) Zig contributors | package (tools/zig), module (runtime libraries linked into it) | Packaged unmodified with its own license files (tools/zig; mingw-w64: tools/zig/lib/libc/mingw/COPYING); packages add the LGPL-2.1 and GPL-2.0 texts (LICENSES/). |

Operating-system libraries are used, not distributed: Direct3D 12, DXGI and the Microsoft Visual
C++ runtime on Windows; SDL2 (Zlib), GTK 3 (LGPL-2.1-or-later), the Vulkan loader (Apache-2.0)
and the C/C++ runtime on Linux.
