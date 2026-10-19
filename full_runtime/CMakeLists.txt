cmake_minimum_required(VERSION 3.20)
project(hangtime VERSION 0.1.0 LANGUAGES C CXX)
# CI and source checks without a ROM: build only the runtime (no game module, no generated code).
option(HANGTIME_RUNTIME_ONLY "Build only the runtime executable (no ROM needed)" OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Two products (RELEASE_LEGAL.md "Architecture"):
#  * hangtime[.exe]       the runtime: our code (full_runtime/, process_context.cpp), the pinned
#                         N64ModernRuntime (vendor/), the pinned RT64 build, SDL2. No game code.
#  * hangtime_game.dll/.so the game module: the code N64Recomp/RSPRecomp generate from the
#                         player's ROM (full_generated/, audio_rsp_generated.cpp) + game_module_api.cpp.
#                         Loaded by the runtime at start (game_module.hpp); never distributed.
# Upstream sources (hangtime_paths.py): the pinned git submodules in external/ (a fresh clone),
# else the original development layout beside the repository (../N64Recomp, ../runtime/rt64).
# Windows: RT64 built static (build.py: RT64_BUILD, default <rt64>/build-native; D3D12 + Vulkan),
# RT64's bundled SDL2. Linux: RT64 built with -DRT64_STATIC=ON -DRT64_SDL_WINDOW_VULKAN=ON (Vulkan
# through an SDL window), passed as -DRT64_BUILD=...; system SDL2 and GTK3 (file picker).
get_filename_component(NATIVE "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
if (EXISTS "${NATIVE}/external/rt64/CMakeLists.txt")
    set(RT64 "${NATIVE}/external/rt64")
else()
    get_filename_component(RT64 "${NATIVE}/../runtime/rt64" ABSOLUTE)
endif()
if (EXISTS "${NATIVE}/external/N64Recomp/include/recomp.h")
    set(N64RECOMP "${NATIVE}/external/N64Recomp")
else()
    get_filename_component(N64RECOMP "${NATIVE}/../N64Recomp" ABSOLUTE)
endif()
# Experiments only: link a modified RT64 copy (its source and build-native) in a separate build dir.
set(RT64_ROOT "" CACHE PATH "Alternative RT64 tree (default: external/rt64 or ../runtime/rt64)")
if (RT64_ROOT)
    set(RT64 "${RT64_ROOT}")
endif()
if (WIN32)
    set(RT64_BUILD "${RT64}/build-native" CACHE PATH "RT64 Windows build directory (static, Visual Studio)")
    set(SDL2_DIR "${RT64}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3")
    set(SDL2_INCLUDE "${SDL2_DIR}/include")
else()
    set(RT64_BUILD "${RT64}/build-linux" CACHE PATH "RT64 Linux build directory (static, SDL Vulkan window)")
    find_package(SDL2 REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    set(SDL2_INCLUDE ${SDL2_INCLUDE_DIRS})
endif()
set(RUNTIME "${NATIVE}/vendor/N64ModernRuntime")

file(GLOB GAME_SOURCES "${NATIVE}/full_generated/funcs_*.c")
file(GLOB ULTRAMODERN_SOURCES "${RUNTIME}/ultramodern/src/*.cpp")
# ai.cpp is replaced by console-accurate AI semantics in main.cpp.
# rsp.cpp (RSP memory + task runner) is built into the game module with the microcode it runs.
set(LIBRECOMP_SOURCES cont dp eep files flash math_routines pak pi sp ultra_stubs ultra_translation vi)
list(TRANSFORM LIBRECOMP_SOURCES PREPEND "${RUNTIME}/librecomp/src/")
list(TRANSFORM LIBRECOMP_SOURCES APPEND ".cpp")

# Compile settings shared by the runtime and the game module.
function(hangtime_settings target)
    target_compile_definitions(${target} PRIVATE HANGTIME_PROJECT_VERSION="${PROJECT_VERSION}")
    if (WIN32)
        target_compile_definitions(${target} PRIVATE NOMINMAX HLSL_CPU SDL_MAIN_HANDLED)
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:C,CXX>:/O2 /MP>)
    else()
        # RT64's headers need the same window/API definitions its own build used.
        target_compile_definitions(${target} PRIVATE HLSL_CPU SDL_MAIN_HANDLED PLUME_SDL_VULKAN_ENABLED RT64_SDL_WINDOW_VULKAN)
        # MSVC semantics the generated code was verified under: wrapping signed arithmetic, no
        # type-based alias analysis.
        target_compile_options(${target} PRIVATE -O2 -fwrapv -fno-strict-aliasing -Wno-unused-value -Wno-parentheses-equality)
        # Portability (SteamOS and other older distributions): original glibc symbol versions.
        target_compile_options(${target} PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/glibc_compat.h")
        # librecomp's RSP vector unit uses SSSE3/SSE4.1 intrinsics (MSVC allows them unflagged;
        # every x86-64 CPU this targets, Steam Deck included, has them).
        if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
            target_compile_options(${target} PRIVATE -msse4.1)
        endif()
    endif()
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/include" "${N64RECOMP}/include" "${NATIVE}"
        "${RUNTIME}/ultramodern/include" "${RUNTIME}/librecomp/include" "${RUNTIME}/librecomp/include/librecomp"
        "${RUNTIME}/librecomp/src" "${RUNTIME}/thirdparty" "${RUNTIME}/thirdparty/concurrentqueue" "${RUNTIME}/thirdparty/sse2neon"
        "${RT64}/src" "${RT64}/src/contrib" "${RT64}/src/contrib/plume" "${RT64}/src/contrib/imgui"
        "${RT64}/src/contrib/hlslpp/include" "${RT64}/src/contrib/mupen64plus-core/src/api"
        "${RT64}/src/contrib/nativefiledialog-extended/src/include" "${RT64}/src/contrib/dxc/inc"
        ${SDL2_INCLUDE})
endfunction()

# ---- runtime objects (no generated or game-derived sources) ----
add_library(hangtime_rt OBJECT
    main.cpp launcher.cpp launcher_ui.cpp input_map.cpp cheats.cpp dl_dump.cpp widescreen.cpp rt64_context.cpp perf.cpp render_gpu.cpp
    game_module_loader.cpp module_builder.cpp partition.cpp "${RT64}/src/contrib/imgui/backends/imgui_impl_sdlrenderer2.cpp" runtime_support.cpp "${NATIVE}/process_context.cpp"
    ${ULTRAMODERN_SOURCES} ${LIBRECOMP_SOURCES})
hangtime_settings(hangtime_rt)

# ---- game module objects (generated from the ROM) ----
if (HANGTIME_RUNTIME_ONLY)
    add_executable(hangtime $<TARGET_OBJECTS:hangtime_rt>)
else()
add_library(hangtime_game_objs OBJECT ${GAME_SOURCES} "${NATIVE}/audio_rsp_generated.cpp" "${RUNTIME}/librecomp/src/rsp.cpp" game_module_api.cpp)
hangtime_settings(hangtime_game_objs)
target_include_directories(hangtime_game_objs PRIVATE "${NATIVE}/full_generated")
if (NOT WIN32)
    set_target_properties(hangtime_rt hangtime_game_objs PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

if (WIN32)
    # The executable exports exactly what the module's objects need from it (gen_exports.py).
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    get_filename_component(MSVC_BIN "${CMAKE_LINKER}" DIRECTORY)
    set(EXPORTS_DEF "${CMAKE_CURRENT_BINARY_DIR}/hangtime_exports.def")
    add_custom_command(OUTPUT "${EXPORTS_DEF}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/gen_exports.py" "${MSVC_BIN}/dumpbin.exe" "${EXPORTS_DEF}"
                --game $<TARGET_OBJECTS:hangtime_game_objs> --runtime $<TARGET_OBJECTS:hangtime_rt>
        DEPENDS hangtime_rt hangtime_game_objs $<TARGET_OBJECTS:hangtime_game_objs> $<TARGET_OBJECTS:hangtime_rt> gen_exports.py
        COMMAND_EXPAND_LISTS VERBATIM)
    add_executable(hangtime $<TARGET_OBJECTS:hangtime_rt> "${EXPORTS_DEF}")
else()
    add_executable(hangtime $<TARGET_OBJECTS:hangtime_rt>)
endif()
set_target_properties(hangtime PROPERTIES ENABLE_EXPORTS ON)   # Linux: -rdynamic; Windows: import library for the module

add_library(hangtime_game MODULE $<TARGET_OBJECTS:hangtime_game_objs>)
set_target_properties(hangtime_game PROPERTIES PREFIX "")      # hangtime_game.dll / hangtime_game.so
if (WIN32)
    target_link_libraries(hangtime_game PRIVATE hangtime)       # the executable's import library
    target_link_options(hangtime_game PRIVATE /MAP)
endif()
endif()   # HANGTIME_RUNTIME_ONLY

if (NOT WIN32)
    target_link_libraries(hangtime PRIVATE -Wl,--start-group
        "${RT64_BUILD}/rt64.a"
        "${RT64_BUILD}/src/contrib/plume/libplume.a"
        "${RT64_BUILD}/src/contrib/re-spirv/libre-spirv.a"
        "${RT64_BUILD}/src/contrib/nativefiledialog-extended/src/libnfd.a"
        "${RT64_BUILD}/src/contrib/zstd/build/cmake/lib/libzstd.a"
        -Wl,--end-group
        SDL2::SDL2 ${GTK3_LIBRARIES} pthread dl)
    # Portability: the C++ runtime linked in (no libstdc++ version requirement on the player's
    # system), and nothing from the static libraries exported to the game module.
    target_link_options(hangtime PRIVATE -static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL)
    if (TARGET hangtime_game)
        target_link_options(hangtime_game PRIVATE -static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL)
    endif()
    # Launcher font (Roboto Medium, Apache-2.0, from RT64's Dear ImGui copy).
    add_custom_command(TARGET hangtime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:hangtime>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RT64}/src/contrib/imgui/misc/fonts/Roboto-Medium.ttf" "$<TARGET_FILE_DIR:hangtime>/assets/")
    return()
endif()
target_link_libraries(hangtime PRIVATE
    "${RT64_BUILD}/Release/rt64.lib"
    "${RT64_BUILD}/src/contrib/plume/Release/plume.lib"
    "${RT64_BUILD}/src/contrib/re-spirv/Release/re-spirv.lib"
    "${RT64_BUILD}/src/contrib/nativefiledialog-extended/src/Release/nfd.lib"
    "${RT64_BUILD}/src/contrib/zstd/build/cmake/lib/Release/zstd_static.lib"
    "${RT64}/src/contrib/dxc/lib/x64/dxcompiler.lib"
    "${SDL2_DIR}/lib/x64/SDL2.lib"
    d3d12 dxgi shcore delayimp comctl32 bcrypt shell32 ole32 winmm)
# A GUI-subsystem program (no console window on double-click); main() stays the entry point.
set_target_properties(hangtime PROPERTIES WIN32_EXECUTABLE TRUE)
target_link_options(hangtime PRIVATE /ENTRY:mainCRTStartup)
# RT64 compiles specialized shaders at runtime with DXC; keep its DLLs beside the executable.
foreach(dll "${RT64}/src/contrib/dxc/bin/x64/dxcompiler.dll" "${RT64}/src/contrib/dxc/bin/x64/dxil.dll" "${SDL2_DIR}/lib/x64/SDL2.dll")
  add_custom_command(TARGET hangtime POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll}" "$<TARGET_FILE_DIR:hangtime>")
endforeach()
# Diagnostic: linker map for --sample stack resolution (resolve_stacks.py).
target_link_options(hangtime PRIVATE /MAP)
# Launcher font (Roboto Medium, Apache-2.0, from RT64's Dear ImGui copy).
add_custom_command(TARGET hangtime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:hangtime>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RT64}/src/contrib/imgui/misc/fonts/Roboto-Medium.ttf" "$<TARGET_FILE_DIR:hangtime>/assets/")
