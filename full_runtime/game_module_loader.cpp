// Runtime side of game_module.hpp: finds and loads the game module beside the executable.
#include "game_module.hpp"
#include <cstring>
#include <filesystem>
#include "launcher.hpp"
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

static const HangtimeGameApi* loaded = nullptr;
const HangtimeGameApi* hangtime_game() { return loaded; }

const HangtimeGameApi* hangtime_game_load(std::string& error) {
    if (loaded) return loaded;
    // The player's data folder (built there on first run, module_builder.cpp), else beside the
    // executable (developer builds).
#ifdef _WIN32
    const char* name = "hangtime_game.dll";
#else
    const char* name = "hangtime_game.so";
#endif
    std::filesystem::path path = launcher_data_dir() / name;
    if (!std::filesystem::exists(path)) path = launcher_exe_dir() / name;
#ifdef _WIN32
    // No system "Bad Image" dialog for a damaged file: it is reported and rebuilt instead.
    DWORD old_mode = 0;
    SetThreadErrorMode(SEM_FAILCRITICALERRORS, &old_mode);
    HMODULE m = LoadLibraryW(path.c_str());
    SetThreadErrorMode(old_mode, nullptr);
    void* sym = m ? (void*)GetProcAddress(m, HANGTIME_GAME_API_SYMBOL) : nullptr;
    const std::string why = m ? "" : "Windows error " + std::to_string(GetLastError());
#else
    void* m = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    void* sym = m ? dlsym(m, HANGTIME_GAME_API_SYMBOL) : nullptr;
    const char* dl = m ? nullptr : dlerror();
    const std::string why = dl ? dl : "";
#endif
    if (!std::filesystem::exists(path)) {
        error = "The game module is missing:\n" + launcher_utf8(path) + "\n\nIt is built from your own NBA Hangtime ROM (see the README).";
        return nullptr;
    }
    // A rejected module is unloaded so that the first-run builder can replace the file.
    auto reject = [&](const std::string& why_rejected) -> const HangtimeGameApi* {
        error = why_rejected;
#ifdef _WIN32
        if (m) FreeLibrary(m);
#else
        if (m) dlclose(m);
#endif
        return nullptr;
    };
    if (!m || !sym) return reject("The game module could not be loaded:\n" + launcher_utf8(path) + (why.empty() ? "" : "\n" + why));
    const HangtimeGameApi* api = reinterpret_cast<HangtimeGameApiFunc*>(sym)();
    if (!api || api->version != HANGTIME_GAME_API_VERSION)
        return reject("The game module was built for a different version of this program; rebuild it from your ROM.");
    if (std::strcmp(api->rom_sha256, HANGTIME_ROM_SHA256) != 0)
        return reject("The game module was built from a different ROM image.");
    loaded = api;
    return loaded;
}
