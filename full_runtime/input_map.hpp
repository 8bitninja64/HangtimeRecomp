#pragma once
// Host input bindings for controller 1: which keyboard keys / gamepad controls produce each N64
// control. Shared by the game's controller callback (main.cpp) and the launcher's Controls page.
// Settings store them as `key.<CONTROL>=<SDL scancode names, comma separated>` and
// `pad.<CONTROL>=<SDL GameController names>` (axes as +name / -name).
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "SDL.h"

struct N64Control { const char* id; const char* label; uint16_t mask; int stick; };  // stick: 0 none, 1 up 2 down 3 left 4 right
const std::vector<N64Control>& n64_controls();   // in display order
// Game actions on each control with the game's default action table (0x80035FE0).
const char* n64_control_action(const char* id);

struct InputBindings {
    std::map<std::string, std::vector<SDL_Scancode>> keys;   // control id -> keys
    std::map<std::string, std::vector<std::string>> pad;     // control id -> SDL GameController tokens
    int deadzone = 15;                                       // analog stick dead zone, percent
    static InputBindings defaults();
    void load(const std::map<std::string, std::string>& kv);  // overrides from settings (key./pad./deadzone)
    void save(std::map<std::string, std::string>& kv) const;
    std::string describe_keys(const std::string& id) const;
    std::string describe_pad(const std::string& id) const;
};

// Reads the current host state into N64 buttons and stick (-1..1). Thread-safe for SDL state reads.
void input_poll(const InputBindings& b, SDL_GameController* pad, bool use_keyboard, uint16_t* buttons, float* x, float* y);
// Pad token helpers (also used for rebinding): the pressed control on `pad`, "" if none.
std::string pad_pressed_token(SDL_GameController* pad);
