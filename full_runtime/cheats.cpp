// Cheat registry and the game-side application of enabled cheats (see cheats.hpp, FULL_GAME.md
// "Cheats"). Original cheats are applied through the game's own mechanism, at the moment the
// game itself applies codes: the end of the TONIGHT'S MATCHUP screen (0x80048E60, after code
// entry). Every match's init (0x80033C1C -> 0x8003063C / 0x80030688) clears all cheat flags, so
// a disabled cheat is gone from the next match on and nothing persists into saves or later games.
#include "cheats.hpp"
#include <cstdio>
#include <cstring>
#include "hangtime_full.h"

uint32_t hangtime_poll_count();   // main.cpp

namespace {
using O = CheatOrigin; using G = CheatGroup; using S = CheatScope;
// Matchup codes: the halfword stored per player at 0x800A7738 + 2p; the game's table at
// 0x8009F548 maps each to its flag (0x80048F70). After entry, 0x80048E60 turns every entered flag
// into 0x000F (all four players, CPU included) when humans are on one team; with humans on both
// teams it requires an entry from each team (0x80048E98). Every flag's reader was located in the
// ROM (FULL_GAME.md "Cheats"); "measured" notes cite the run that showed the effect.
// Button sequences (process 0x80022D68, matchup screen): the 0x80023B6C steps of each handler.
std::vector<CheatInfo> registry = {
    {"unlimited_turbo", "Unlimited Turbo", "Turbo meter never drains", O::Original, G::Gameplay, S::Global, "Matchup code 461", true,
     "measured: meter stays full under held turbo"},
    {"hyper_speed", "Hyper Speed", "Players run faster", O::Original, G::Gameplay, S::Global, "Matchup code 552", true,
     "the game adds 0x5800 to running speed (0x8000DE54); effect not yet measured"},
    {"max_power", "Maximum Power", "Stronger pushes and dunks", O::Original, G::Gameplay, S::Global, "Matchup code 802", true,
     "read by the game at 0x80018CAC and 0x8001C274; effect not yet measured"},
    {"max_steal", "Maximum Steal", "Steals succeed more often", O::Original, G::Gameplay, S::Global, "Matchup code 709", true,
     "read by the game at 0x80018CAC; effect not yet measured"},
    {"no_pushing", "No Pushing", "Pushing is disabled", O::Original, G::Gameplay, S::Global, "Matchup code 390", true,
     "read by the game at 0x8000F0E4; effect not yet measured"},
    {"legal_goaltending", "Legal Goaltending", "Goaltending is allowed", O::Original, G::Gameplay, S::Global, "Matchup code 937", true,
     "read by the game at 0x80018CAC; effect not yet measured"},
    {"stealth_turbo", "Stealth Turbo", "Turbo use is not shown", O::Original, G::Gameplay, S::Global, "Matchup code 273", true,
     "read by the game at 0x8002AE50; effect not yet measured"},
    {"max_speed", "Maximum Speed", "Speed attribute at maximum", O::Original, G::Gameplay, S::Global, "Matchup code 284", false,
     "in this ROM only the tip-off announcement reads its flag"},
    {"max_block", "Maximum Blocking", "Block attribute at maximum", O::Original, G::Gameplay, S::Global, "Matchup code 616", false,
     "in this ROM nothing reads its flag"},
    {"fast_passing", "Fast Passing", "Passes travel faster", O::Original, G::Gameplay, S::Global, "Matchup code 120", false,
     "in this ROM only the tip-off announcement reads its flag"},
    {"baby_mode", "Baby Mode", "Small players", O::Original, G::Appearance, S::Global, "Matchup code 025", true, "measured: all players small"},
    {"big_head", "Big Head Mode", "Player 1 has a big head", O::Original, G::Appearance, S::Player, "Hold Up + Pass + Turbo", true,
     "measured; replaces Huge Head (the game's handlers are exclusive)"},
    {"huge_head", "Huge Head Mode", "Player 1 has a huge head", O::Original, G::Appearance, S::Player, "Up, Up, Pass, Turbo", true,
     "measured; replaces Big Head"},
    {"tournament", "Tournament Mode", "Turns all other cheats off", O::Original, G::Special, S::Global, "Matchup code 111", false,
     "the game accepts it only with two or more human players (0x80049064)"},
    {"no_music", "No Music", "Music off during the match", O::Original, G::Special, S::Global, "Matchup code 048", true,
     "measured: silent gaps between sound effects"},
    {"aba_ball", "ABA Basketball", "Red, white and blue ball", O::Original, G::Special, S::Global, "Right, Right+Shoot, Right, Right+Turbo, Right, Right+Pass", true, "measured"},
    {"rooftop", "Rooftop Court", "Play on the rooftop court", O::Original, G::Special, S::Global, "Left, Left+Turbo, Left, Left+Turbo", true, "measured"},
    {"no_drift", "No Drift", "No drifting in the air", O::Original, G::Special, S::Player, "Down, Down, Shoot, Turbo", true,
     "read by the game at 0x8001AD3C; effect not yet measured"},
    {"no_tag_arrow", "No Tag Arrow", "Hide player 1's arrow", O::Original, G::Special, S::Player, "Left, Left, Pass, Turbo", true, "measured"},
    {"no_cpu_assist", "No CPU Assistance", "Disable the computer's catch-up help", O::Original, G::Special, S::Global, "Right, Right+Pass, Right, Right+Pass", true,
     "read by the game at 0x8001A314; effect not yet measured"},
    {"shot_percentage", "Show Shot Percentage", "Shows the chance of each shot", O::Original, G::Special, S::Player, "Rotate the stick clockwise from Up", true,
     "measured: percentage replaces the quarter during shots"},
    // PC enhancements
    {"always_on_fire", "Always On Fire (player 1)", "Player 1 stays on fire all match", O::PC, G::Gameplay, S::Player, "", true,
     "measured; holds the game's own 3-basket streak so the game ignites and keeps the fire"},
};
std::set<std::string> enabled_cheats;
uint8_t* ram = nullptr;
int16_t& half(uint32_t a) { return *reinterpret_cast<int16_t*>(ram + ((a & 0x1FFFFFFE) ^ 2)); }

struct MatchupCode { const char* id; int16_t code; };
const MatchupCode MATCHUP[] = {
    {"tournament", 0x111}, {"baby_mode", 0x025}, {"no_music", 0x048}, {"legal_goaltending", 0x937}, {"max_block", 0x616},
    {"max_steal", 0x709}, {"max_power", 0x802}, {"max_speed", 0x284}, {"hyper_speed", 0x552}, {"stealth_turbo", 0x273},
    {"unlimited_turbo", 0x461}, {"no_pushing", 0x390}, {"fast_passing", 0x120}};
bool on(const char* id) { return enabled_cheats.count(id) > 0; }
}

const std::vector<CheatInfo>& cheat_registry() { return registry; }
const CheatInfo* cheat_find(const std::string& id) {
    for (const auto& c : registry) if (id == c.id) return &c;
    return nullptr;
}
void cheats_configure(const std::set<std::string>& enabled) {
    enabled_cheats.clear();
    for (const auto& id : enabled) if (const CheatInfo* c = cheat_find(id); c && c->available) enabled_cheats.insert(id);   // unavailable ids are ignored
    if (enabled_cheats.count("big_head") && enabled_cheats.count("huge_head")) enabled_cheats.erase("big_head");          // the game's own handlers are exclusive
    if (!enabled_cheats.empty()) {
        fprintf(stderr, "CHEATS enabled:");
        for (const auto& id : enabled_cheats) fprintf(stderr, " %s", id.c_str());
        fprintf(stderr, "\n");
    }
}
bool cheats_any_enabled() { return !enabled_cheats.empty(); }

// 0x80048E60 entry (end of the matchup screen, after code entry; the button-sequence handlers of
// 0x80022D68 are still running). Applies enabled cheats to every joined human player.
extern "C" void hangtime_cheats_matchup(uint8_t* rdram, recomp_context* ctx) {
    if (enabled_cheats.empty()) return;
    ram = rdram;
    const int joined = half(0x800A6EAE);
    for (int p = 0; p < 4; ++p) {
        if (!(joined & (1 << p))) continue;
        const int16_t bit = (int16_t)(1 << p);
        // Matchup codes: run the game's own applier 0x80048F70 with this player's code slot
        // temporarily holding the code (the player's own entry is restored and applied afterwards
        // by the game as usual).
        for (const auto& m : MATCHUP) {
            if (!on(m.id)) continue;
            const int16_t saved = half(0x800A7738 + 2 * p);
            half(0x800A7738 + 2 * p) = m.code;
            recomp_context c = *ctx;
            c.r4 = p; c.r29 = ctx->r29 - 0x100;   // a fresh frame below the caller's
            LOOKUP_FUNC(0x80048F70)(rdram, &c);   // the game's code applier, from the game module's table
            half(0x800A7738 + 2 * p) = saved;
        }
        // Button-sequence cheats: exactly the writes of their handlers (0x80023030..0x800238CC).
        if (on("big_head")) { half(0x800A6EB4) |= bit; half(0x800A6EB6) &= ~bit; }
        if (on("huge_head")) { half(0x800A6EB6) |= bit; half(0x800A6EB4) &= ~bit; }
        if (on("shot_percentage")) half(0x800A6EB8) |= bit;
        if (on("no_tag_arrow")) half(0x800A6EBA) |= bit;
        if (on("no_drift")) half(0x800A6EBC) |= bit;
        if (on("no_cpu_assist")) half(0x800A6EBE) = 1;
        if (on("aba_ball")) half(0x800A6EC0) = 1;
        if (on("rooftop")) half(0x800A6EC2) = 1;
    }
    fprintf(stderr, "CHEATS applied at matchup: players=%X flags", joined);
    for (uint32_t a = 0x800A6EB4; a <= 0x800A6EDE; a += 2) fprintf(stderr, " %04X", (uint16_t)half(a));
    fprintf(stderr, "\n");
}

// Once per game input frame (osContStartReadData, game thread). Always On Fire for player 1
// (slot 0): keep the game's own streak state at "player 0 made 3+ in a row" (0x800A712C scorer,
// 0x800A712A count, capped by the game at 4); the game's per-frame logic (0x80015FF8) then sets
// player 0's fire bit (0x800A6EB0) with its sound, and its basket logic (0x800184A8) keeps it.
// Also logs the game's cheat flags (0x800A6EB4..0x800A6EDE) whenever they change during mode 3,
// cheats enabled or not: the regression oracle (verify_cheats.py) reads the game's own final state.
void cheats_frame(uint8_t* rdram) {
    ram = rdram;
    static uint16_t last[22]; static bool logged = false;
    if (half(0x800A71C4) == 3) {
        uint16_t now[22]; bool changed = !logged;
        for (int i = 0; i < 22; ++i) { now[i] = (uint16_t)half(0x800A6EB4 + 2 * i); changed |= now[i] != last[i]; }
        if (changed) {
            fprintf(stderr, "CHEATFLAGS poll=%u", hangtime_poll_count());
            for (int i = 0; i < 22; ++i) fprintf(stderr, " %04X", now[i]);
            fprintf(stderr, "\n");
            memcpy(last, now, sizeof last); logged = true;
        }
    }
    if (!on("always_on_fire")) return;
    if (half(0x800A71C4) != 3 || !(half(0x800A6EAE) & 1)) return;   // live match with player 1 joined
    half(0x800A712C) = 0;
    if (half(0x800A712A) < 3 || half(0x800A712A) > 4) half(0x800A712A) = 4;
}
