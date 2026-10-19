#pragma once
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

// Player settings (hangtime.ini). Unknown keys are kept, so newer files survive older builds.
struct LauncherSettings {
    std::filesystem::path rom;      // last ROM that verified
    int window_mode = 0;            // 0 windowed, 1 borderless fullscreen, 2 exclusive fullscreen
    bool widescreen = true;         // true 16:9 live play by default (owner decision 2026-10-18); F10 / Graphics toggles
    int resolution = 0;             // render height: 0 = follow the window, else 720/1080/1440/2160
    int msaa = 0;
    bool skip_launcher = false;     // start the game directly (F1 in game or --launcher reopens it)
    int master_volume = 100;        // percent, applied to the stream handed to the audio device
    std::string audio_device;       // "" = system default
    std::string render_gpu;         // "" = automatic; else a GPU name (render_gpu.hpp): Vulkan on that GPU only
    std::set<std::string> cheats;   // enabled cheat ids (cheats.hpp); never enabled by default
    std::map<std::string, std::string> extra;   // input bindings (key./pad./deadzone) and unknown keys
};
std::string launcher_utf8(const std::filesystem::path& p);  // UTF-8 as std::string (C++20 u8string is char8_t)
std::filesystem::path launcher_exe_dir();
std::filesystem::path launcher_data_dir();
LauncherSettings launcher_load_settings();
void launcher_save_settings(const LauncherSettings& s);
std::string launcher_sha256_hex(const std::vector<uint8_t>& data);
bool launcher_load_rom(const std::filesystem::path& path, std::vector<uint8_t>& rom, std::string& error);
std::optional<std::filesystem::path> launcher_pick_rom();
void launcher_setup_log(bool console);
// Starts this executable (or `exe`, e.g. a newly installed one) again with args.
bool launcher_restart(const std::vector<std::string>& args, const std::filesystem::path& exe = {});
extern const char* const HANGTIME_ROM_SHA256;
extern const char* const HANGTIME_VERSION;
