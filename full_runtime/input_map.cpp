#include "input_map.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>

const std::vector<N64Control>& n64_controls() {
    static const std::vector<N64Control> c = {
        {"SU", "Stick up", 0, 1}, {"SD", "Stick down", 0, 2}, {"SL", "Stick left", 0, 3}, {"SR", "Stick right", 0, 4},
        {"CD", "C-Down", 0x0004, 0}, {"CR", "C-Right", 0x0001, 0}, {"A", "A", 0x8000, 0}, {"START", "Start", 0x1000, 0},
        {"B", "B", 0x4000, 0}, {"Z", "Z", 0x2000, 0}, {"L", "L", 0x0020, 0}, {"R", "R", 0x0010, 0},
        {"CU", "C-Up", 0x0008, 0}, {"CL", "C-Left", 0x0002, 0},
        {"DU", "D-Pad up", 0x0800, 0}, {"DD", "D-Pad down", 0x0400, 0}, {"DL", "D-Pad left", 0x0200, 0}, {"DR", "D-Pad right", 0x0100, 0},
    };
    return c;
}

const char* n64_control_action(const char* id) {
    if (!strcmp(id, "CD")) return "Shoot / Jump / Block";
    if (!strcmp(id, "CR")) return "Pass / Steal";
    if (!strcmp(id, "A")) return "Turbo";
    if (!strcmp(id, "START")) return "Start / Menus";
    if (id[0] == 'S') return "Move";
    return "";
}

InputBindings InputBindings::defaults() {
    // The verified host defaults (FULL_GAME.md "Controls"); verify_input.py tests these.
    InputBindings b;
    auto k = [&](const char* id, std::initializer_list<SDL_Scancode> s) { b.keys[id] = s; };
    k("SU", {SDL_SCANCODE_UP}); k("SD", {SDL_SCANCODE_DOWN}); k("SL", {SDL_SCANCODE_LEFT}); k("SR", {SDL_SCANCODE_RIGHT});
    k("CD", {SDL_SCANCODE_S}); k("CR", {SDL_SCANCODE_D}); k("A", {SDL_SCANCODE_X, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_SPACE});
    k("START", {SDL_SCANCODE_RETURN}); k("B", {SDL_SCANCODE_C}); k("Z", {SDL_SCANCODE_Z});
    k("L", {SDL_SCANCODE_Q}); k("R", {SDL_SCANCODE_E}); k("CU", {SDL_SCANCODE_W}); k("CL", {SDL_SCANCODE_A});
    k("DU", {SDL_SCANCODE_I}); k("DD", {SDL_SCANCODE_K}); k("DL", {SDL_SCANCODE_J}); k("DR", {SDL_SCANCODE_L});
    auto p = [&](const char* id, std::initializer_list<const char*> t) { for (auto* s : t) b.pad[id].push_back(s); };
    p("SU", {"-lefty"}); p("SD", {"+lefty"}); p("SL", {"-leftx"}); p("SR", {"+leftx"});
    p("CD", {"a", "+righty"}); p("CR", {"b", "+rightx"}); p("A", {"rightshoulder", "+righttrigger"});
    p("START", {"start"}); p("B", {"x"}); p("Z", {"back", "+lefttrigger"}); p("L", {"leftshoulder"}); p("R", {});
    p("CU", {"y", "-righty"}); p("CL", {"-rightx"});
    p("DU", {"dpup"}); p("DD", {"dpdown"}); p("DL", {"dpleft"}); p("DR", {"dpright"});
    return b;
}

static std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out; std::stringstream ss(s); std::string t;
    while (std::getline(ss, t, ',')) if (!t.empty()) out.push_back(t);
    return out;
}

void InputBindings::load(const std::map<std::string, std::string>& kv) {
    for (const auto& c : n64_controls()) {
        if (auto it = kv.find(std::string("key.") + c.id); it != kv.end()) {
            keys[c.id].clear();
            for (const auto& n : split(it->second)) if (SDL_Scancode sc = SDL_GetScancodeFromName(n.c_str()); sc != SDL_SCANCODE_UNKNOWN) keys[c.id].push_back(sc);
        }
        if (auto it = kv.find(std::string("pad.") + c.id); it != kv.end()) pad[c.id] = split(it->second);
    }
    if (auto it = kv.find("deadzone"); it != kv.end()) deadzone = std::clamp(atoi(it->second.c_str()), 0, 60);
}

void InputBindings::save(std::map<std::string, std::string>& kv) const {
    for (const auto& c : n64_controls()) {
        kv[std::string("key.") + c.id] = describe_keys(c.id);
        std::string p;
        if (auto it = pad.find(c.id); it != pad.end()) for (const auto& t : it->second) p += (p.empty() ? "" : ",") + t;
        kv[std::string("pad.") + c.id] = p;
    }
    kv["deadzone"] = std::to_string(deadzone);
}

std::string InputBindings::describe_keys(const std::string& id) const {
    std::string s;
    if (auto it = keys.find(id); it != keys.end()) for (SDL_Scancode sc : it->second) s += (s.empty() ? "" : ",") + std::string(SDL_GetScancodeName(sc));
    return s;
}

std::string pad_token_label(const std::string& t) {
    static const std::pair<const char*, const char*> names[] = {
        {"a", "A"}, {"b", "B"}, {"x", "X"}, {"y", "Y"}, {"back", "Back"}, {"start", "Start"}, {"guide", "Guide"},
        {"leftshoulder", "LB"}, {"rightshoulder", "RB"}, {"leftstick", "L3"}, {"rightstick", "R3"},
        {"dpup", "D-Pad up"}, {"dpdown", "D-Pad down"}, {"dpleft", "D-Pad left"}, {"dpright", "D-Pad right"},
        {"-lefty", "Left stick up"}, {"+lefty", "Left stick down"}, {"-leftx", "Left stick left"}, {"+leftx", "Left stick right"},
        {"-righty", "Right stick up"}, {"+righty", "Right stick down"}, {"-rightx", "Right stick left"}, {"+rightx", "Right stick right"},
        {"+lefttrigger", "LT"}, {"+righttrigger", "RT"}};
    for (auto& [k, v] : names) if (t == k) return v;
    return t;
}

std::string InputBindings::describe_pad(const std::string& id) const {
    std::string s;
    if (auto it = pad.find(id); it != pad.end()) for (const auto& t : it->second) s += (s.empty() ? "" : ", ") + pad_token_label(t);
    return s.empty() ? "-" : s;
}

static float axis_value(SDL_GameController* pad, const std::string& token) {   // token "+name" / "-name": 0..1 in that direction
    const SDL_GameControllerAxis ax = SDL_GameControllerGetAxisFromString(token.c_str() + 1);
    if (ax == SDL_CONTROLLER_AXIS_INVALID) return 0;
    const float v = SDL_GameControllerGetAxis(pad, ax) / 32767.0f;
    return token[0] == '-' ? std::max(0.0f, -v) : std::max(0.0f, v);
}

void input_poll(const InputBindings& b, SDL_GameController* pad, bool use_keyboard, uint16_t* buttons, float* x, float* y) {
    const uint8_t* k = SDL_GetKeyboardState(nullptr);
    float kx = 0, ky = 0, px = 0, py = 0;
    const float dz = b.deadzone / 100.0f;
    for (const auto& c : n64_controls()) {
        bool on = false; float analog = 0;
        if (use_keyboard) if (auto it = b.keys.find(c.id); it != b.keys.end()) for (SDL_Scancode sc : it->second) on = on || k[sc];
        if (pad) if (auto it = b.pad.find(c.id); it != b.pad.end())
            for (const auto& t : it->second) {
                if (t[0] == '+' || t[0] == '-') {
                    const float v = axis_value(pad, t);
                    if (c.stick) analog = std::max(analog, v > dz ? v : 0.0f);
                    else on = on || v > 0.5f;   // axis used as a button (C-stick, triggers)
                } else {
                    const SDL_GameControllerButton bt = SDL_GameControllerGetButtonFromString(t.c_str());
                    on = on || (bt != SDL_CONTROLLER_BUTTON_INVALID && SDL_GameControllerGetButton(pad, bt));
                }
            }
        if (c.mask && on) *buttons |= c.mask;
        if (c.stick) {
            const float kv = on ? 1.0f : 0.0f;
            if (c.stick == 1) { ky += kv; py += analog; } else if (c.stick == 2) { ky -= kv; py -= analog; }
            else if (c.stick == 3) { kx -= kv; px -= analog; } else { kx += kv; px += analog; }
        }
    }
    *x = std::abs(px) > 0 ? px : kx;   // analog wins over keys when deflected
    *y = std::abs(py) > 0 ? py : ky;
}

std::string pad_pressed_token(SDL_GameController* pad) {
    if (!pad) return "";
    for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; ++i)
        if (SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)i)) return SDL_GameControllerGetStringForButton((SDL_GameControllerButton)i);
    for (int i = 0; i < SDL_CONTROLLER_AXIS_MAX; ++i) {
        const int v = SDL_GameControllerGetAxis(pad, (SDL_GameControllerAxis)i);
        if (std::abs(v) > 24000) return std::string(v > 0 ? "+" : "-") + SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)i);
    }
    return "";
}
