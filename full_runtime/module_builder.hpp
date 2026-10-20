#pragma once
// First-run build of the game module on the player's machine (RELEASE_LEGAL.md "Architecture"):
// runs N64Recomp and RSPRecomp on the player's verified ROM with the shipped configuration, then
// compiles the generated code with the bundled Zig toolchain into hangtime_game.dll / .so in the
// player's data folder. Package layout beside the executable (package_release.py):
//   setup/full.toml, setup/full.symbols.toml, setup/audio_rsp.toml   recompiler configuration
//   setup/sdk/include/..., setup/sdk/src/{game_module_api.cpp,rsp.cpp} what the module compiles against
//   tools/N64Recomp[.exe], tools/RSPRecomp[.exe], tools/zig/zig[.exe]
//     (Linux: the same three as one archive, tools/buildkit.tar.xz, unpacked only while building)
//   hangtime.lib (Windows: the runtime's import library)
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>

struct ModuleBuildPaths {
    std::filesystem::path setup, tools, zig, import_lib, work, output;
};
struct ModuleBuildProgress {
    std::atomic<float> fraction{0};
    std::atomic<bool> done{false}, ok{false};
    std::mutex mutex;
    std::string step;      // current step, for the progress screen
    std::string error;     // why it failed (with the log path)
    std::filesystem::path log;
    void set_step(const std::string& s) { std::lock_guard<std::mutex> l(mutex); step = s; }
    std::string get_step() { std::lock_guard<std::mutex> l(mutex); return step; }
};

ModuleBuildPaths module_build_paths();                       // from the executable and data folders
bool module_build_available(const ModuleBuildPaths& paths);  // the setup kit and tools are present
// Linux: removes a loose kit and Zig cache that an earlier package left (unused once the kit is an archive).
void module_build_tidy(const ModuleBuildPaths& paths);
// Blocking; run it on a worker thread. Writes paths.output on success.
void module_build(const ModuleBuildPaths& paths, const std::filesystem::path& rom, ModuleBuildProgress& progress);
