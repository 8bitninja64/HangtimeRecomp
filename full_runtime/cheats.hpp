#pragma once
// Central cheat registry. The launcher lists it; the game applies enabled, available cheats
// (cheats.cpp). A cheat is `available` only once it has been verified against this ROM's code
// and tested (FULL_GAME.md "Cheats"); unverified ones are shown greyed out with the reason.
// Nothing is enabled by default; with no cheat enabled no hook changes anything.
#include <cstdint>
#include <set>
#include <string>
#include <vector>

enum class CheatOrigin { Original, PC };              // original N64 code vs PC enhancement
enum class CheatGroup { Gameplay, Appearance, Special };
enum class CheatScope { Global, Team, Player };

struct CheatInfo {
    const char* id;
    const char* name;
    const char* description;
    CheatOrigin origin;
    CheatGroup group;
    CheatScope scope;
    const char* how;          // original activation (matchup code / button sequence), "" for PC cheats
    bool available;           // verified in this ROM and implemented
    const char* note;         // why unavailable, or caveats
};

const std::vector<CheatInfo>& cheat_registry();
const CheatInfo* cheat_find(const std::string& id);

// Game side (cheats.cpp): set the enabled ids before the game starts.
void cheats_configure(const std::set<std::string>& enabled);
bool cheats_any_enabled();
void cheats_frame(uint8_t* rdram);   // once per game input frame (game thread)
