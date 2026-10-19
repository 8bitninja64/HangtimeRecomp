// Whole-program build: recompiler hooks and libultra pieces not provided by librecomp.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include "librecomp/recomp.h"
#include "librecomp/sections.h"
#include "game_module.hpp"

// Indirect calls: every partitioned function of the single fixed code section (from the game
// module's table, game_module.hpp).
static std::unordered_map<uint32_t, recomp_func_t*> function_lookup;
void hangtime_register_functions() {
    const HangtimeGameApi* game = hangtime_game();
    for (size_t s = 0; s < game->num_sections; ++s) {
        const SectionTableEntry& section = game->sections[s];
        for (size_t f = 0; f < section.num_funcs; ++f)
            function_lookup[section.ram_addr + section.funcs[f].offset] = section.funcs[f].func;
    }
}
extern "C" recomp_func_t* get_function(int32_t vram) {
    auto it = function_lookup.find((uint32_t)vram);
    if (it == function_lookup.end()) {
        fprintf(stderr, "FAIL: indirect call to unknown function 0x%08X\n", (uint32_t)vram);
        fflush(stderr);
        std::abort();
    }
    return it->second;
}

// Same semantics as librecomp recomp.cpp (FR bit is the only status bit games may change).
extern "C" void cop0_status_write(recomp_context* ctx, gpr value) {
    uint32_t old_sr = ctx->status_reg, new_sr = (uint32_t)value, changed = old_sr ^ new_sr;
    if (changed & 0x04000000u) {
        if (new_sr & 0x04000000u) { ctx->f_odd = &ctx->f1.u32l; ctx->mips3_float_mode = true; }
        else { ctx->f_odd = &ctx->f0.u32h; ctx->mips3_float_mode = false; }
        changed &= ~0x04000000u;
    }
    // IE and the interrupt-mask byte are tracked (they gate scheduler safe points, as in the
    // harness); the game's own critical sections toggle them through mtc0 Status.
    changed &= ~0x0000FF01u;
    if (changed) { fprintf(stderr, "FAIL: unhandled status register bits changed: 0x%08X\n", changed); std::abort(); }
    ctx->status_reg = new_sr;
}
extern "C" gpr cop0_status_read(recomp_context* ctx) { return (gpr)(int32_t)ctx->status_reg; }
extern "C" void switch_error(const char* func, uint32_t vram, uint32_t jtbl) {
    fprintf(stderr, "FAIL: switch-case out of bounds in %s at 0x%08X (table 0x%08X)\n", func, vram, jtbl); std::abort();
}
extern "C" void do_break(uint32_t vram) { fprintf(stderr, "FAIL: break at 0x%08X\n", vram); std::abort(); }

// Scheduler safe point (same policy as the verified harness): every 1024th backward branch with
// interrupts enabled, deliver pending external (VI/SP/DP/PI/AI) messages and yield to a
// higher-priority runnable thread. The CPU context must be unchanged.
#include "ultramodern/ultramodern.hpp"
void dequeue_external_messages(uint8_t* rdram);
static thread_local unsigned safe_point_calls = 0;
static thread_local bool inside_safe_point = false;
// Reviewed idle spins: loops whose body only re-reads loop-invariant memory (or nothing), so on
// the console they just burn CPU until an interrupt-driven thread changes state:
//   0x800555B4 `b .`  the libultra idle thread (0x80055554, priority 0 after starting main)
//   0x80056238 `b .`  terminal halt loop (0x80056208)
//   0x8005619C, 0x800561E8  frame-end waits on bytes/words 0x8009A338 / 0x8009A31C (0x80056164)
// Nothing can change what they read until an external message readies another guest thread, so
// at these branches the host blocks for the next external message (<= 1 ms) and delivers it,
// every iteration. Guest-visible order is unchanged; a host core is no longer pinned at 100%.
static const bool idle_wait_enabled = !getenv("HANGTIME_SPIN_IDLE");  // diagnostic A/B: =1 keeps the original spin
static bool idle_spin(uint32_t pc) { return idle_wait_enabled && (pc == 0x800555B4u || pc == 0x80056238u || pc == 0x8005619Cu || pc == 0x800561E8u); }
extern "C" void hangtime_safe_point(uint8_t* rdram, recomp_context* ctx, uint32_t pc) {
    if (!inside_safe_point && (ctx->status_reg & 1) && idle_spin(pc)) {
        inside_safe_point = true;
        dequeue_external_messages(rdram);                       // anything already pending, and
        ultramodern::check_running_queue(rdram);                // a runnable higher-priority thread, first
        ultramodern::wait_for_external_message_timed(rdram, 1); // then sleep until the next event
        dequeue_external_messages(rdram);
        ultramodern::check_running_queue(rdram);
        inside_safe_point = false;
        return;
    }
    if (inside_safe_point || !(ctx->status_reg & 1) || (++safe_point_calls & 1023) != 0) return;
    inside_safe_point = true;
    dequeue_external_messages(rdram);
    ultramodern::check_running_queue(rdram);
    inside_safe_point = false;
}

// process_context.cpp reaches guest memory through the harness accessors; same word-swapped
// layout as recomp.h, with KSEG0/KSEG1 aliases mapped onto physical RDRAM.
extern "C" int32_t* thread_word(uint8_t* ram, uint32_t a) {
    if (a & 3) { fprintf(stderr, "FAIL: unaligned word access 0x%08X\n", a); std::abort(); }
    return reinterpret_cast<int32_t*>(ram + (a & 0x1FFFFFFF));
}
extern "C" int16_t* thread_half(uint8_t* ram, uint32_t a) { return reinterpret_cast<int16_t*>(ram + ((a & 0x1FFFFFFF) ^ 2)); }
extern "C" int8_t* thread_byte(uint8_t* ram, uint32_t a) { return reinterpret_cast<int8_t*>(ram + ((a & 0x1FFFFFFF) ^ 3)); }

// The game's process switching: original 0x80068C8C (restore) and 0x80068D0C (stack enter),
// replaced by the fiber-based transfers verified in the harness (PROCESS_RESTORE.md).
#include "process_context.h"
extern "C" int hangtime_process_always_enabled;
struct EnableProcessContext { EnableProcessContext() { hangtime_process_always_enabled = 1; } } enable_process_context;
extern "C" void main_context_restore(uint8_t* rdram, recomp_context* ctx) { hangtime_context_restore(rdram, ctx); }
extern "C" void main_stack_enter(uint8_t* rdram, recomp_context* ctx) { hangtime_process_enter(rdram, ctx); }

// libultra internals reachable only from replaced managers/handlers. Fail closed if the game
// ever reaches them. (The SI primitives the Controller Pak uses: controller_pak.cpp.)
#define FAIL_CLOSED(name) extern "C" void name##_recomp(uint8_t*, recomp_context*) { \
    fprintf(stderr, "FAIL: unreviewed libultra internal " #name " reached\n"); fflush(stderr); std::abort(); }
FAIL_CLOSED(__osEnqueueAndYield)
FAIL_CLOSED(__osEnqueueThread) FAIL_CLOSED(__osPopThread) FAIL_CLOSED(__osPiRawStartDma)
