#pragma once
// Boundary between the runtime (hangtime executable: our code, N64ModernRuntime, RT64, SDL, ...)
// and the game module (hangtime_game.dll / hangtime_game.so: the code N64Recomp and RSPRecomp
// generate from the player's own ROM, plus game_module_api.cpp). The runtime contains no code
// derived from the game; it loads the module at start (RELEASE_LEGAL.md "Architecture").
// The module calls runtime functions directly (they are exported by the executable); the runtime
// reaches the module only through this table.
#include <cstddef>
#include <cstdint>
#include <string>
#include "librecomp/sections.h"
#include "librecomp/rsp.hpp"
#include "recomp.h"

constexpr uint32_t HANGTIME_GAME_API_VERSION = 1;

struct HangtimeGameApi {
    uint32_t version;                       // HANGTIME_GAME_API_VERSION the module was built with
    const char* rom_sha256;                 // the ROM image the module was generated from
    const SectionTableEntry* sections;      // recompiled functions (one fixed section)
    size_t num_sections;
    recomp_func_t* boot_entry;              // IPL3 entry 0x80000400
    RspUcodeFunc* audio_microcode;          // the game's audio RSP microcode, recompiled
    // librecomp's RSP task runner (vendored rsp.cpp, unmodified) is built into the module, so the
    // RSP memory it fills and the microcode that reads it live in one image.
    void (*rsp_init)();
    void (*rsp_set_callbacks)(const recomp::rsp::callbacks_t&);
    bool (*rsp_run_task)(uint8_t* rdram, const OSTask* task);
};

// Exported by the module (C linkage).
#define HANGTIME_GAME_API_SYMBOL "hangtime_game_api"
using HangtimeGameApiFunc = const HangtimeGameApi*();

// Runtime side (game_module_loader.cpp): loads the module beside the executable. Returns nullptr
// and an explanation for the player if it is missing, built for another ROM or another runtime.
const HangtimeGameApi* hangtime_game_load(std::string& error);
const HangtimeGameApi* hangtime_game();     // the loaded module (after a successful load)
