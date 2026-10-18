#pragma once
// Included by every generated whole-program source (full.toml recomp_include).
#include "librecomp/recomp.h"
#include "process_context.h"   // HT_* unwinding/resume macros and save notification
#ifdef __cplusplus
extern "C" {
#endif
// Backward-branch scheduler safe point (ported from the verified harness): lets the original
// event/VI threads run while a game thread busy-waits on state they update.
void hangtime_safe_point(uint8_t* rdram, recomp_context* ctx, uint32_t pc);
// Optional widescreen (widescreen.cpp; no effect unless --widescreen): frame start (0x8005643C),
// floor draw (0x80061594), sprite dispatcher entry/exit (0x8005B770 / its jr ra 0x8005C140).
void hangtime_ws_frame_begin(uint8_t* rdram, recomp_context* ctx);
void hangtime_ws_floor(uint8_t* rdram, recomp_context* ctx);
void hangtime_ws_sprite_begin(uint8_t* rdram, recomp_context* ctx);
void hangtime_ws_sprite_end(uint8_t* rdram, recomp_context* ctx);
void hangtime_ws_caller(uint32_t call_site);   // before each of the dispatcher's 12 call sites
void hangtime_ws_object(uint8_t* rdram, uint64_t object, uint32_t draw);   // object draw routine entry (draw(object, view))
void hangtime_ws_player_cull(uint8_t* rdram, recomp_context* ctx, int step); // player draw 0x800606D4 screen cull (0, 1 tests; 2 restore)
// Cheats (cheats.cpp; no effect unless a cheat is enabled): matchup code application (0x80048E60).
void hangtime_cheats_matchup(uint8_t* rdram, recomp_context* ctx);
// Called by generated code but not declared in funcs.h (MSVC's C accepted implicit declarations;
// clang/gcc C99 do not). Defined in runtime_support.cpp: the process switch replacements
// (full.toml ignored list), osPfsIsPlug and the fail-closed libultra internals.
void main_context_restore(uint8_t* rdram, recomp_context* ctx);
void main_stack_enter(uint8_t* rdram, recomp_context* ctx);
void osPfsIsPlug_recomp(uint8_t* rdram, recomp_context* ctx);
void __osEnqueueAndYield_recomp(uint8_t* rdram, recomp_context* ctx);
void __osEnqueueThread_recomp(uint8_t* rdram, recomp_context* ctx);
void __osPopThread_recomp(uint8_t* rdram, recomp_context* ctx);
void __osPfsGetInitData_recomp(uint8_t* rdram, recomp_context* ctx);
void __osPfsRequestData_recomp(uint8_t* rdram, recomp_context* ctx);
void __osPiRawStartDma_recomp(uint8_t* rdram, recomp_context* ctx);
void __osSiGetAccess_recomp(uint8_t* rdram, recomp_context* ctx);
void __osSiRawStartDma_recomp(uint8_t* rdram, recomp_context* ctx);
void __osSiRelAccess_recomp(uint8_t* rdram, recomp_context* ctx);
#ifdef __cplusplus
}
#endif
