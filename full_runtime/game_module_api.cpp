// Game module side of game_module.hpp: built together with the generated sources (full_generated/,
// audio_rsp_generated.cpp) into hangtime_game.dll / hangtime_game.so. Nothing else goes into it.
#include "game_module.hpp"
#include "hangtime_full.h"
#include "funcs.h"
#include "recomp_overlays.inl"   // section_table / num_sections (generated)

RspExitReason hangtime_audio_rsp(uint8_t* rdram, uint32_t ucode_addr);   // audio_rsp_generated.cpp

#ifdef _WIN32
#define HANGTIME_EXPORT __declspec(dllexport)
#else
#define HANGTIME_EXPORT __attribute__((visibility("default")))
#endif

extern "C" HANGTIME_EXPORT const HangtimeGameApi* hangtime_game_api() {
    static const HangtimeGameApi api{
        HANGTIME_GAME_API_VERSION,
        "6e1304d5bf8922a652f4063f9d26120ac8c3e9af2a5f8a4f4bc733cf003edf83",   // NBA Hangtime (USA)
        section_table, num_sections, func_80000400, hangtime_audio_rsp,
        recomp::rsp::constants_init, recomp::rsp::set_callbacks, recomp::rsp::run_task,
    };
    return &api;
}
