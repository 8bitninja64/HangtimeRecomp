// Pre-game launcher (before the game window): Play, Graphics, Controls, Audio, Cheats, Game Files,
// About. Dear ImGui (rt64.lib's copy + the SDL_Renderer backend) on its own SDL window, so it runs
// the same on Windows and Linux. All artwork is drawn procedurally: no game or NBA assets ship.
// Navigation: mouse, keyboard (arrows / Enter / Esc) and gamepad (D-pad or stick / A / B).
#include "launcher_ui.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "SDL.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui/imgui_impl_sdl2_custom.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#include "cheats.hpp"
#include "input_map.hpp"
#include "render_gpu.hpp"
#include "module_builder.hpp"
#include "branding.hpp"
#include <thread>

namespace {
constexpr float VW = 1600, VH = 900;           // virtual canvas
enum Page { MAIN, GRAPHICS, CONTROLS, CHEATS, AUDIO, FILES, ABOUT };
const char* const PAGE_TITLE[] = {"", "GRAPHICS", "CONTROLS", "CHEATS", "AUDIO", "GAME FILES", "ABOUT"};

ImU32 rgba(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
const ImU32 GOLD = rgba(255, 196, 40), ORANGE = rgba(255, 128, 20), NAVY = rgba(14, 22, 52), PANEL = rgba(20, 34, 78, 235),
            EDGE = rgba(70, 110, 190), TEXT = rgba(235, 240, 250), DIM = rgba(150, 165, 200), RED = rgba(225, 40, 40);

struct Ui {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_GameController* pad = nullptr;
    ImFont* font = nullptr;
    float s = 1, ox = 0, oy = 0;               // virtual -> pixel transform
    Page page = MAIN;
    int sel = 0;
    std::string rom_status; bool rom_ok = false;
    std::string message;
    InputBindings bind;
    std::string rebind_id; int rebind_device = 0;    // 1 keyboard, 2 gamepad
    bool rebind_wait_release = false;
    ImVec2 P(float x, float y) const { return {ox + x * s, oy + y * s}; }
    float S(float v) const { return v * s; }
};

ImDrawList* dl() { return ImGui::GetBackgroundDrawList(); }

void text(Ui& u, float size, float x, float y, ImU32 col, const char* t, float align = 0, float spacing = 0) {
    ImFont* f = u.font;
    float w = f->CalcTextSizeA(u.S(size), FLT_MAX, 0, t).x + u.S(spacing) * std::max(0, (int)strlen(t) - 1);
    ImVec2 p = u.P(x, y); p.x -= w * align;
    if (spacing == 0) { dl()->AddText(f, u.S(size), p, col, t); return; }
    for (const char* q = t; *q;) {   // one UTF-8 character at a time
        unsigned int cp; const int n = ImTextCharFromUtf8(&cp, q, nullptr);
        const std::string c(q, q + std::max(1, n)); q += std::max(1, n);
        dl()->AddText(f, u.S(size), p, col, c.c_str()); p.x += f->CalcTextSizeA(u.S(size), FLT_MAX, 0, c.c_str()).x + u.S(spacing);
    }
}

void outlined(Ui& u, float size, float x, float y, ImU32 col_top, ImU32 col_bottom, const char* t, float align) {
    const float w = u.font->CalcTextSizeA(u.S(size), FLT_MAX, 0, t).x;
    ImVec2 p = u.P(x, y); p.x -= w * align;
    const float o = u.S(size * 0.012f);
    dl()->AddText(u.font, u.S(size), {p.x + o * 4, p.y + o * 5}, rgba(0, 0, 0, 150), t);            // drop shadow
    for (int dx = -2; dx <= 2; ++dx) for (int dy = -2; dy <= 2; ++dy)                               // thin dark outline
        if (dx || dy) dl()->AddText(u.font, u.S(size), {p.x + dx * o, p.y + dy * o}, rgba(70, 14, 6), t);
    const int v0 = dl()->VtxBuffer.Size;
    dl()->AddText(u.font, u.S(size), p, col_top, t);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl(), v0, dl()->VtxBuffer.Size, {p.x, p.y + u.S(size * 0.2f)}, {p.x, p.y + u.S(size * 0.9f)}, col_top, col_bottom);
}

// ---- procedural icons (centre cx, cy, radius r in virtual units) ----
void icon_ball(Ui& u, float cx, float cy, float r) {
    ImDrawList* d = dl(); const ImVec2 c = u.P(cx, cy); const float R = u.S(r);
    d->AddCircleFilled({c.x + R * 0.08f, c.y + R * 0.1f}, R, rgba(0, 0, 0, 90), 48);
    d->AddCircleFilled(c, R, rgba(226, 104, 28), 48);
    d->AddCircleFilled({c.x - R * 0.3f, c.y - R * 0.35f}, R * 0.45f, rgba(250, 150, 70, 120), 32);
    const ImU32 seam = rgba(30, 14, 8); const float t = u.S(r * 0.06f);
    d->AddCircle(c, R, seam, 48, t);
    d->AddLine({c.x - R, c.y}, {c.x + R, c.y}, seam, t);
    d->AddLine({c.x, c.y - R}, {c.x, c.y + R}, seam, t);
    d->AddBezierCubic({c.x - R * 0.62f, c.y - R * 0.78f}, {c.x - R * 0.15f, c.y - R * 0.3f}, {c.x - R * 0.15f, c.y + R * 0.3f}, {c.x - R * 0.62f, c.y + R * 0.78f}, seam, t);
    d->AddBezierCubic({c.x + R * 0.62f, c.y - R * 0.78f}, {c.x + R * 0.15f, c.y - R * 0.3f}, {c.x + R * 0.15f, c.y + R * 0.3f}, {c.x + R * 0.62f, c.y + R * 0.78f}, seam, t);
}
void icon_monitor(Ui& u, float cx, float cy, float r) {
    ImDrawList* d = dl();
    d->AddRectFilled(u.P(cx - r * 1.3f, cy - r * 0.85f), u.P(cx + r * 1.3f, cy + r * 0.7f), rgba(25, 30, 45), u.S(6));
    d->AddRectFilled(u.P(cx - r * 1.18f, cy - r * 0.74f), u.P(cx + r * 1.18f, cy + r * 0.58f), rgba(30, 50, 110), u.S(3));
    d->AddRectFilledMultiColor(u.P(cx - r * 1.18f, cy - r * 0.05f), u.P(cx + r * 1.18f, cy + r * 0.58f), rgba(214, 140, 60), rgba(214, 140, 60), rgba(170, 96, 34), rgba(170, 96, 34));
    d->AddLine(u.P(cx, cy - r * 0.05f), u.P(cx, cy + r * 0.58f), rgba(255, 240, 220), u.S(2));
    d->AddCircle(u.P(cx, cy + r * 0.26f), u.S(r * 0.22f), rgba(255, 240, 220), 24, u.S(2));
    d->AddRectFilled(u.P(cx - r * 0.25f, cy + r * 0.7f), u.P(cx + r * 0.25f, cy + r * 0.9f), rgba(25, 30, 45));
    d->AddRect(u.P(cx - r * 1.3f, cy - r * 0.85f), u.P(cx + r * 1.3f, cy + r * 0.7f), EDGE, u.S(6), 0, u.S(2));
}
void icon_pad(Ui& u, float cx, float cy, float r) {
    ImDrawList* d = dl(); const ImU32 body = rgba(40, 46, 66);
    d->AddRectFilled(u.P(cx - r * 1.1f, cy - r * 0.45f), u.P(cx + r * 1.1f, cy + r * 0.35f), body, u.S(r * 0.4f));
    d->AddCircleFilled(u.P(cx - r * 0.75f, cy + r * 0.3f), u.S(r * 0.42f), body, 32);
    d->AddCircleFilled(u.P(cx + r * 0.75f, cy + r * 0.3f), u.S(r * 0.42f), body, 32);
    d->AddRectFilled(u.P(cx - r * 0.82f, cy - r * 0.1f), u.P(cx - r * 0.42f, cy + r * 0.02f), rgba(150, 160, 180));
    d->AddRectFilled(u.P(cx - r * 0.68f, cy - r * 0.24f), u.P(cx - r * 0.56f, cy + r * 0.16f), rgba(150, 160, 180));
    const ImU32 cols[4] = {rgba(60, 200, 80), rgba(220, 50, 50), rgba(50, 120, 230), rgba(240, 200, 40)};
    const float bx[4] = {0.6f, 0.78f, 0.42f, 0.6f}, by[4] = {0.12f, -0.04f, -0.04f, -0.2f};
    for (int i = 0; i < 4; ++i) d->AddCircleFilled(u.P(cx + r * bx[i], cy + r * by[i]), u.S(r * 0.085f), cols[i], 16);
}
void icon_flame(Ui& u, float cx, float cy, float r) {
    ImDrawList* d = dl();
    auto flame = [&](float sc, ImU32 col, float dy) {
        d->PathClear();
        d->PathLineTo(u.P(cx, cy - r * 1.05f * sc + dy));
        d->PathBezierCubicCurveTo(u.P(cx + r * 0.25f * sc, cy - r * 0.5f * sc + dy), u.P(cx + r * 0.8f * sc, cy - r * 0.2f * sc + dy), u.P(cx + r * 0.62f * sc, cy + r * 0.45f * sc + dy));
        d->PathBezierCubicCurveTo(u.P(cx + r * 0.45f * sc, cy + r * 0.9f * sc + dy), u.P(cx - r * 0.45f * sc, cy + r * 0.9f * sc + dy), u.P(cx - r * 0.62f * sc, cy + r * 0.45f * sc + dy));
        d->PathBezierCubicCurveTo(u.P(cx - r * 0.8f * sc, cy - r * 0.1f * sc + dy), u.P(cx - r * 0.3f * sc, cy - r * 0.35f * sc + dy), u.P(cx, cy - r * 1.05f * sc + dy));
        d->PathFillConcave(col);
    };
    flame(1.0f, rgba(220, 40, 20), 0); flame(0.72f, rgba(255, 130, 20), r * 0.18f); flame(0.42f, rgba(255, 225, 90), r * 0.38f);
}
void icon_speaker(Ui& u, float cx, float cy, float r) {
    ImDrawList* d = dl(); const ImU32 c = rgba(200, 210, 230);
    d->AddRectFilled(u.P(cx - r * 0.9f, cy - r * 0.25f), u.P(cx - r * 0.5f, cy + r * 0.25f), c);
    d->AddTriangleFilled(u.P(cx - r * 0.5f, cy - r * 0.25f), u.P(cx - r * 0.05f, cy - r * 0.7f), u.P(cx - r * 0.05f, cy), c);
    d->AddTriangleFilled(u.P(cx - r * 0.5f, cy + r * 0.25f), u.P(cx - r * 0.05f, cy), u.P(cx - r * 0.05f, cy + r * 0.7f), c);
    d->AddRectFilled(u.P(cx - r * 0.5f, cy - r * 0.25f), u.P(cx - r * 0.05f, cy + r * 0.25f), c);
    for (int i = 1; i <= 3; ++i) d->PathArcTo(u.P(cx, cy), u.S(r * 0.3f * i), -0.9f, 0.9f, 24), d->PathStroke(rgba(70, 150, 255), 0, u.S(r * 0.09f));
}
void icon_cart(Ui& u, float cx, float cy, float r) {
    ImDrawList* d = dl();
    d->AddRectFilled(u.P(cx - r * 1.05f, cy - r * 0.75f), u.P(cx + r * 1.05f, cy + r * 0.8f), rgba(150, 156, 168), u.S(r * 0.15f));
    d->AddRectFilled(u.P(cx - r * 0.75f, cy - r * 0.55f), u.P(cx + r * 0.75f, cy + r * 0.45f), rgba(20, 24, 40), u.S(4));
    d->AddText(u.font, u.S(r * 0.42f), u.P(cx - r * 0.6f, cy - r * 0.35f), ORANGE, "N64");
    for (int i = 0; i < 6; ++i) d->AddRectFilled(u.P(cx - r * 0.7f + i * r * 0.26f, cy + r * 0.58f), u.P(cx - r * 0.58f + i * r * 0.26f, cy + r * 0.78f), rgba(110, 116, 128));
}
void icon_info(Ui& u, float cx, float cy, float r) {
    dl()->AddCircle(u.P(cx, cy), u.S(r * 0.8f), TEXT, 40, u.S(4));
    text(u, r * 1.1f, cx, cy - r * 0.62f, TEXT, "i", 0.5f);
}

void background(Ui& u, int w, int h) {
    ImDrawList* d = dl();
    d->AddRectFilledMultiColor({0, 0}, {(float)w, (float)h}, rgba(10, 16, 40), rgba(10, 16, 40), rgba(28, 18, 30), rgba(28, 18, 30));
    // arena lights
    for (int i = 0; i < 6; ++i) {
        const float x = 120 + i * 260, y = 50 + (i % 2) * 30;
        for (int k = 6; k >= 1; --k) d->AddCircleFilled(u.P(x, y), u.S(6.0f * k), rgba(150, 190, 255, 14), 24);
        d->AddCircleFilled(u.P(x, y), u.S(5), rgba(235, 245, 255), 16);
    }
    // stands bands
    for (int i = 0; i < 3; ++i) d->AddRectFilled(u.P(0, 150 + i * 70), u.P(VW, 190 + i * 70), rgba(30, 40, 80, 50));
    // court floor with lines
    d->AddRectFilledMultiColor(u.P(0, 700), u.P(VW, VH), rgba(120, 60, 24, 150), rgba(120, 60, 24, 150), rgba(70, 32, 12, 230), rgba(70, 32, 12, 230));
    d->AddLine(u.P(0, 700), u.P(VW, 700), rgba(240, 220, 200, 90), u.S(3));
    d->PathArcTo(u.P(VW / 2, 900), u.S(260), 3.14159f, 6.28318f, 64); d->PathStroke(rgba(240, 220, 200, 70), 0, u.S(3));
}

void title(Ui& u, bool subtitle) {
    // The project's own title (branding.hpp), centered: small line, large outlined line, rule, subtitle.
    text(u, 34, 800, 40, GOLD, HANGTIME_TITLE_SMALL, 0.5f, 5);
    outlined(u, 112, 800, 84, rgba(255, 230, 90), rgba(255, 110, 20), HANGTIME_TITLE_LARGE, 0.5f);
    dl()->AddRectFilledMultiColor(u.P(330, 230), u.P(1270, 236), RED, rgba(255, 140, 40), rgba(255, 140, 40), RED);
    if (subtitle) text(u, 26, 800, 248, rgba(205, 212, 228), HANGTIME_TITLE_SUB, 0.5f, 4);
}

struct Tile { float x, y, w, h; const char* label; void (*icon)(Ui&, float, float, float); };
const Tile TILES[] = {
    {110, 300, 440, 210, "PLAY GAME", icon_ball}, {580, 300, 440, 210, "GRAPHICS", icon_monitor}, {1050, 300, 440, 210, "CONTROLS", icon_pad},
    {110, 530, 440, 170, "CHEATS", icon_flame}, {580, 530, 440, 170, "AUDIO", icon_speaker}, {1050, 530, 440, 170, "GAME FILES", icon_cart},
    {580, 720, 440, 90, "ABOUT", icon_info},
};
constexpr int TILE_COUNT = 7, SKIP_ITEM = 7;

void draw_tile(Ui& u, const Tile& t, bool selected, float time) {
    ImDrawList* d = dl();
    if (selected) {
        const float pulse = 0.6f + 0.4f * std::sin(time * 4.0f);
        for (int k = 5; k >= 1; --k)
            d->AddRect(u.P(t.x - k * 2.0f, t.y - k * 2.0f), u.P(t.x + t.w + k * 2.0f, t.y + t.h + k * 2.0f), rgba(255, 190, 40, (int)(40 * pulse)), u.S(14), 0, u.S(4));
    }
    d->AddRectFilledMultiColor(u.P(t.x, t.y), u.P(t.x + t.w, t.y + t.h), selected ? rgba(60, 50, 40) : rgba(22, 36, 80),
                               selected ? rgba(60, 50, 40) : rgba(22, 36, 80), selected ? rgba(30, 24, 18) : rgba(12, 20, 50), selected ? rgba(30, 24, 18) : rgba(12, 20, 50));
    d->AddRect(u.P(t.x, t.y), u.P(t.x + t.w, t.y + t.h), selected ? GOLD : EDGE, u.S(12), 0, u.S(selected ? 4.0f : 2.0f));
    if (t.h > 120) {
        t.icon(u, t.x + t.w / 2, t.y + t.h * 0.42f, t.h * 0.26f);
        text(u, 38, t.x + t.w / 2, t.y + t.h - 58, selected ? GOLD : TEXT, t.label, 0.5f, 2);
    } else {
        t.icon(u, t.x + t.w / 2 - 90, t.y + t.h / 2, 30);
        text(u, 36, t.x + t.w / 2 - 40, t.y + t.h / 2 - 21, selected ? GOLD : TEXT, t.label, 0, 2);
    }
}

void hints(Ui& u, const char* a, const char* b) {
    auto chip = [&](float x, ImU32 col, const char* letter, const char* label) {
        dl()->AddCircle(u.P(x, 860), u.S(19), col, 32, u.S(3));
        text(u, 26, x, 845, col, letter, 0.5f);
        text(u, 26, x + 32, 845, TEXT, label);
    };
    chip(1250, rgba(70, 200, 90), "A", a);
    chip(1420, rgba(230, 60, 60), "B", b);
    text(u, 20, 30, 840, DIM, (std::string("v") + HANGTIME_VERSION).c_str());
    std::string footer = HANGTIME_PRODUCT_NAME;
    for (char& c : footer) c = (char)toupper((unsigned char)c);
    text(u, 20, 30, 864, DIM, footer.c_str());
}

// ---- navigation input (main page) ----
enum Nav { N_NONE, N_UP, N_DOWN, N_LEFT, N_RIGHT, N_OK, N_BACK };
Nav nav_from_event(const SDL_Event& e) {
    if (e.type == SDL_KEYDOWN) switch (e.key.keysym.scancode) {
        case SDL_SCANCODE_UP: return N_UP; case SDL_SCANCODE_DOWN: return N_DOWN;
        case SDL_SCANCODE_LEFT: return N_LEFT; case SDL_SCANCODE_RIGHT: return N_RIGHT;
        case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE: return N_OK;
        case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_BACKSPACE: return N_BACK; default: return N_NONE; }
    if (e.type == SDL_CONTROLLERBUTTONDOWN) switch (e.cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return N_UP; case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return N_DOWN;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return N_LEFT; case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return N_RIGHT;
        case SDL_CONTROLLER_BUTTON_A: case SDL_CONTROLLER_BUTTON_START: return N_OK;
        case SDL_CONTROLLER_BUTTON_B: return N_BACK; default: return N_NONE; }
    return N_NONE;
}
int main_move(int sel, Nav n) {
    // grid: 0 1 2 / 3 4 5 / 6 / 7(skip toggle)
    static const int up[] = {0, 1, 2, 0, 1, 2, 4, 6}, down[] = {3, 4, 5, 6, 6, 6, 7, 7}, left[] = {0, 0, 1, 3, 3, 4, 6, 7}, right[] = {1, 2, 2, 4, 5, 5, 6, 7};
    switch (n) { case N_UP: return up[sel]; case N_DOWN: return down[sel]; case N_LEFT: return left[sel]; case N_RIGHT: return right[sel]; default: return sel; }
}

void verify_rom(Ui& u, LauncherSettings& s) {
    std::vector<uint8_t> rom; std::string error;
    if (s.rom.empty()) { u.rom_ok = false; u.rom_status = "No ROM selected."; return; }
    u.rom_ok = launcher_load_rom(s.rom, rom, error);
    u.rom_status = u.rom_ok ? "Verified: NBA Hangtime (USA), SHA-256 " + std::string(HANGTIME_ROM_SHA256).substr(0, 16) + "..." : error;
}

// ---- sub pages (ImGui widgets inside the content panel) ----
template <typename F> void panel(Ui& u, F&& body) {
    dl()->AddRectFilled(u.P(110, 290), u.P(1490, 810), PANEL, u.S(14));
    dl()->AddRect(u.P(110, 290), u.P(1490, 810), EDGE, u.S(14), 0, u.S(2));
    ImGui::SetNextWindowPos(u.P(140, 310)); ImGui::SetNextWindowSize({u.S(1320), u.S(480)});
    ImGui::PushFont(u.font, u.S(28));
    ImGui::Begin("##page", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    body();
    ImGui::End();
    ImGui::PopFont();
}

bool combo(const char* label, int* v, const std::vector<std::string>& items) {
    bool changed = false;
    if (ImGui::BeginCombo(label, items[std::clamp(*v, 0, (int)items.size() - 1)].c_str())) {
        for (int i = 0; i < (int)items.size(); ++i) if (ImGui::Selectable(items[i].c_str(), i == *v)) { *v = i; changed = true; }
        ImGui::EndCombo();
    }
    return changed;
}

void page_graphics(Ui& u, LauncherSettings& s) {
    panel(u, [&] {
        ImGui::PushItemWidth(u.S(520));
        int aspect = s.widescreen ? 1 : 0;
        if (combo("Aspect ratio", &aspect, {"Original 4:3", "True 16:9 widescreen"})) s.widescreen = aspect == 1;
        ImGui::TextColored(ImColor(DIM), "  16:9 widens live play only; menus and stats screens stay 4:3.");
        combo("Display mode", &s.window_mode, {"Windowed", "Borderless fullscreen", "Exclusive fullscreen"});
        SDL_DisplayMode dm{}; SDL_GetDesktopDisplayMode(0, &dm);
        std::vector<int> res = {0, 720, 1080, 1440, 2160};
        std::vector<std::string> names = {"Automatic (follow window)"};
        for (int r : res) if (r) names.push_back(std::to_string(r) + "p" + (r > dm.h ? "  (above display, supersampled)" : ""));
        int ri = (int)(std::find(res.begin(), res.end(), s.resolution) - res.begin()); if (ri >= (int)res.size()) ri = 0;
        if (combo("Render resolution", &ri, names)) s.resolution = res[ri];
        int mi = s.msaa == 8 ? 3 : s.msaa == 4 ? 2 : s.msaa == 2 ? 1 : 0;
        if (combo("Anti-aliasing", &mi, {"Off", "MSAA 2x", "MSAA 4x", "MSAA 8x"})) s.msaa = mi ? 1 << mi : 0;
        // Render GPU (render_gpu.hpp), stored per system in hangtime.ini; Automatic changes nothing.
        static const std::vector<RenderGpu> gpus = render_gpu_list();
        std::vector<std::string> gnames = {"Automatic"};
        int gi = 0;
        for (size_t i = 0; i < gpus.size(); ++i) { gnames.push_back(gpus[i].name); if (!s.render_gpu.empty() && gpus[i].name == s.render_gpu) gi = (int)i + 1; }
        if (!s.render_gpu.empty() && gi == 0) { gnames.push_back(s.render_gpu + "  (not on this system: automatic)"); gi = (int)gnames.size() - 1; }
        if (combo("Render GPU", &gi, gnames)) s.render_gpu = gi == 0 ? "" : gi <= (int)gpus.size() ? gpus[gi - 1].name : s.render_gpu;
        ImGui::TextColored(ImColor(DIM), "  Automatic = the renderer's choice; a named GPU runs Vulkan on that GPU only.");
        ImGui::BeginDisabled();
        bool vs = true; ImGui::Checkbox("VSync (RT64 presents synced)", &vs);
        ImGui::EndDisabled();
        ImGui::TextColored(ImColor(DIM), "Original 60 Hz game timing.  In game: F10 = 16:9, F11 / Alt+Enter = fullscreen.");
        ImGui::PopItemWidth();
    });
}

void page_controls(Ui& u, LauncherSettings& s) {
    panel(u, [&] {
        if (!u.rebind_id.empty()) {
            ImGui::TextColored(ImColor(GOLD), "Press a %s for %s ...   (Esc / Back cancels)", u.rebind_device == 1 ? "key" : "gamepad control", u.rebind_id.c_str());
        } else ImGui::TextColored(ImColor(DIM), "Select a binding to change it. Gamepad bindings apply to every player's gamepad.");
        if (ImGui::BeginTable("binds", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY, {u.S(1300), u.S(330)})) {
            ImGui::TableSetupColumn("N64 control"); ImGui::TableSetupColumn("Game action"); ImGui::TableSetupColumn("Keyboard"); ImGui::TableSetupColumn("Gamepad");
            ImGui::TableHeadersRow();
            for (const auto& c : n64_controls()) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(c.label);
                ImGui::TableNextColumn(); ImGui::TextColored(ImColor(DIM), "%s", n64_control_action(c.id));
                ImGui::TableNextColumn();
                std::string k = u.bind.describe_keys(c.id) + "##k" + c.id;
                if (ImGui::Selectable(k.c_str(), u.rebind_id == c.id && u.rebind_device == 1)) { u.rebind_id = c.id; u.rebind_device = 1; }
                ImGui::TableNextColumn();
                std::string p = u.bind.describe_pad(c.id) + "##p" + c.id;
                if (ImGui::Selectable(p.c_str(), u.rebind_id == c.id && u.rebind_device == 2)) { u.rebind_id = c.id; u.rebind_device = 2; u.rebind_wait_release = true; }
            }
            ImGui::EndTable();
        }
        ImGui::PushItemWidth(u.S(400));
        ImGui::SliderInt("Stick dead zone (%)", &u.bind.deadzone, 0, 60);
        ImGui::PopItemWidth();
        ImGui::SameLine(); if (ImGui::Button("Reset to defaults")) u.bind = InputBindings::defaults();
        // Players 2-4 (main.cpp): gamepads take players 1-4 in connection order with the bindings
        // above; the keyboard plays as one chosen player. The game detects controllers when it starts.
        int kp = std::clamp(atoi(s.extra.count("keyboard_player") ? s.extra["keyboard_player"].c_str() : "1"), 1, 4) - 1;
        int four = s.extra.count("controllers") && s.extra["controllers"] == "4" ? 1 : 0;
        ImGui::PushItemWidth(u.S(400));
        if (combo("Keyboard plays as", &kp, {"Player 1", "Player 2", "Player 3", "Player 4"})) s.extra["keyboard_player"] = std::to_string(kp + 1);
        if (combo("Controllers", &four, {"Connected gamepads + keyboard", "Always four (plug in later)"})) s.extra["controllers"] = four ? "4" : "auto";
        int vs = s.extra.count("pad_order") && s.extra["pad_order"] == "1324" ? 1 : 0;   // port = player; 1-2 vs 3-4
        if (combo("Second gamepad", &vs, {"Player 2 (teammates)", "Player 3 (opponents, head-to-head)"})) s.extra["pad_order"] = vs ? "1324" : "1234";
        ImGui::PopItemWidth();
        std::string list;
        for (int i = 0, n = 0; i < SDL_NumJoysticks(); ++i)
            if (SDL_IsGameController(i)) { const char* nm = SDL_GameControllerNameForIndex(i); list += (n ? ",  " : "") + std::string("P") + std::to_string(++n) + " " + (nm ? nm : "gamepad"); }
        ImGui::TextColored(ImColor(DIM), "Gamepads: %s", list.empty() ? "none connected" : list.c_str());
        ImGui::TextColored(ImColor(DIM), "Connect every player's gamepad before PLAY: the game looks for controllers when it starts.");
        u.bind.save(s.extra);
    });
}

void page_audio(Ui& u, LauncherSettings& s) {
    panel(u, [&] {
        ImGui::PushItemWidth(u.S(520));
        ImGui::SliderInt("Master volume", &s.master_volume, 0, 100, "%d%%");
        ImGui::BeginDisabled();
        int dummy = 100;
        ImGui::SliderInt("Music volume", &dummy, 0, 100, "in game");
        ImGui::SliderInt("Sound effects volume", &dummy, 0, 100, "in game");
        ImGui::EndDisabled();
        ImGui::TextColored(ImColor(DIM), "  Music / effects: use the game's own GAME OPTIONS menu (not yet linked here).");
        std::vector<std::string> devs = {"System default"};
        int cur = 0;
        for (int i = 0; i < SDL_GetNumAudioDevices(0); ++i) {
            const char* n = SDL_GetAudioDeviceName(i, 0); if (!n) continue;
            devs.push_back(n); if (s.audio_device == n) cur = (int)devs.size() - 1;
        }
        if (combo("Output device", &cur, devs)) s.audio_device = cur ? devs[cur] : "";
        ImGui::PopItemWidth();
    });
}

void page_cheats(Ui& u, LauncherSettings& s) {
    panel(u, [&] {
        ImGui::TextColored(ImColor(DIM), "Applied by the game at each TONIGHT'S MATCHUP screen. None are on by default; matchup codes count for all players.");
        static const CheatInfo* focus = nullptr;
        ImGui::BeginChild("cheats", {u.S(1300), u.S(360)});
        const char* groups[] = {"GAMEPLAY", "PLAYER APPEARANCE", "SPECIAL MODES"};
        for (int pass = 0; pass < 2; ++pass) {
            ImGui::SeparatorText(pass == 0 ? "ORIGINAL N64 CHEATS" : "PC ENHANCEMENTS");
            for (int g = 0; g < 3; ++g) {
                bool header = false;
                for (const auto& c : cheat_registry()) {
                    if ((int)c.group != g || (c.origin == CheatOrigin::PC) != (pass == 1)) continue;
                    if (!header) { ImGui::TextColored(ImColor(GOLD), "%s", groups[g]); header = true; }
                    bool on = s.cheats.count(c.id) > 0;
                    ImGui::BeginDisabled(!c.available);
                    std::string label = std::string(c.name) + "##" + c.id;
                    if (ImGui::Checkbox(label.c_str(), &on)) {
                        if (on) s.cheats.insert(c.id); else s.cheats.erase(c.id);
                        // The game's big/huge head handlers are exclusive: keep the newest.
                        if (on && !strcmp(c.id, "big_head")) s.cheats.erase("huge_head");
                        if (on && !strcmp(c.id, "huge_head")) s.cheats.erase("big_head");
                    }
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) || ImGui::IsItemFocused()) focus = &c;
                    ImGui::SameLine(u.S(470));
                    // Unavailable cheats say why (wrapped inside the list).
                    ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x);
                    ImGui::TextColored(ImColor(c.available ? DIM : rgba(200, 120, 90)), "%s%s%s", c.description, c.available ? "" : "  -  unavailable: ", c.available ? "" : c.note);
                    ImGui::PopTextWrapPos();
                }
            }
        }
        ImGui::EndChild();
        if (focus) {
            ImGui::TextColored(ImColor(GOLD), "%s", focus->name);
            ImGui::SameLine();
            ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x);
            ImGui::TextColored(ImColor(DIM), "%s%s%s%s", focus->origin == CheatOrigin::PC ? "PC enhancement" : "Original: ", focus->how,
                               *focus->note ? "  -  " : "", focus->note);
            ImGui::PopTextWrapPos();
        }
    });
}

void page_files(Ui& u, LauncherSettings& s) {
    panel(u, [&] {
        ImGui::Text("ROM:");
        ImGui::SameLine(); ImGui::TextWrapped("%s", s.rom.empty() ? "(none)" : launcher_utf8(s.rom).c_str());
        ImGui::TextColored(ImColor(u.rom_ok ? rgba(90, 220, 110) : rgba(240, 110, 90)), "%s", u.rom_status.c_str());
        if (ImGui::Button("Select ROM ...")) {
            if (auto p = launcher_pick_rom()) { s.rom = *p; verify_rom(u, s); }
        }
        ImGui::SameLine(); if (ImGui::Button("Verify again")) verify_rom(u, s);
        ImGui::Separator();
        ImGui::TextColored(ImColor(DIM), "Accepted: NBA Hangtime (USA) in .z64, .n64 or .v64 byte order (SHA-256 checked).");
        ImGui::TextColored(ImColor(DIM), "Settings: %s", launcher_utf8(launcher_data_dir() / "hangtime.ini").c_str());
        ImGui::TextColored(ImColor(DIM), "Log: %s", launcher_utf8(launcher_data_dir() / "hangtime.log").c_str());
        ImGui::TextColored(ImColor(DIM), "Saves: none (the cartridge has no save memory; controller pak not emulated yet).");
    });
}

void page_about(Ui& u) {
    panel(u, [&] {
        ImGui::Text("%s  v%s", HANGTIME_PRODUCT_NAME, HANGTIME_VERSION);
        ImGui::TextColored(ImColor(DIM), "Build %s %s  -  %s", __DATE__, __TIME__,
#ifdef _WIN32
                           "Windows x64, Direct3D 12"
#else
                           "Linux x86-64, Vulkan"
#endif
        );
        ImGui::Separator();
        ImGui::TextWrapped("A native port made by statically recompiling the original Nintendo 64 program. "
                           "It needs your own NBA Hangtime (USA) ROM; no game data is included.");
        ImGui::Spacing();
        ImGui::TextColored(ImColor(GOLD), "Built with");
        ImGui::BulletText("N64Recomp (MIT) - static recompiler");
        ImGui::BulletText("N64ModernRuntime: ultramodern + librecomp (GPL-3.0)");
        ImGui::BulletText("RT64 (MIT) - renderer; plume, DXC, zstd, nativefiledialog-extended");
        ImGui::BulletText("SDL2 (zlib), Dear ImGui (MIT), Roboto font (Apache-2.0)");
        ImGui::Spacing();
        ImGui::TextWrapped("Unofficial project. NBA Hangtime and the names of the NBA and its teams are trademarks of their "
                           "owners, used only to identify the game this program needs. Not affiliated with or endorsed by "
                           "the NBA, Midway, Williams, Nintendo or any other rights holder.");
    });
}

void open_page(Ui& u, LauncherSettings& s, int which) {
    static const Page map[] = {MAIN, GRAPHICS, CONTROLS, CHEATS, AUDIO, FILES, ABOUT};
    u.page = map[which];
    if (u.page == FILES) verify_rom(u, s);
    ImGui::SetWindowFocus(nullptr);
}
}

LauncherResult run_launcher(LauncherSettings& s) {
    Ui u;
    SDL_DisplayMode dm{}; SDL_GetDesktopDisplayMode(0, &dm);
    int h = std::min(900, dm.h * 8 / 10), w = h * 16 / 9;
    u.window = SDL_CreateWindow(HANGTIME_PRODUCT_NAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!u.window) return LauncherResult::Play;   // no launcher possible: go straight to the game
    u.renderer = SDL_CreateRenderer(u.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!u.renderer) { SDL_DestroyWindow(u.window); return LauncherResult::Play; }
    for (int i = 0; i < SDL_NumJoysticks() && !u.pad; ++i) if (SDL_IsGameController(i)) u.pad = SDL_GameControllerOpen(i);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    const std::string font = launcher_utf8(launcher_exe_dir() / "assets" / "Roboto-Medium.ttf");
    u.font = io.Fonts->AddFontFromFileTTF(font.c_str(), 32.0f);
    if (!u.font) u.font = io.Fonts->AddFontDefault();
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.FrameRounding = 8; st.GrabRounding = 8; st.FramePadding = {14, 8}; st.ItemSpacing = {14, 14};
    st.Colors[ImGuiCol_FrameBg] = ImColor(rgba(26, 44, 96)); st.Colors[ImGuiCol_FrameBgHovered] = ImColor(rgba(44, 70, 140));
    st.Colors[ImGuiCol_FrameBgActive] = ImColor(rgba(70, 90, 160)); st.Colors[ImGuiCol_CheckMark] = ImColor(GOLD);
    st.Colors[ImGuiCol_SliderGrab] = ImColor(ORANGE); st.Colors[ImGuiCol_SliderGrabActive] = ImColor(GOLD);
    st.Colors[ImGuiCol_Button] = ImColor(rgba(40, 64, 130)); st.Colors[ImGuiCol_ButtonHovered] = ImColor(rgba(200, 110, 30));
    st.Colors[ImGuiCol_Header] = ImColor(rgba(60, 80, 150)); st.Colors[ImGuiCol_HeaderHovered] = ImColor(rgba(200, 110, 30));
    st.Colors[ImGuiCol_NavCursor] = ImColor(GOLD); st.Colors[ImGuiCol_PopupBg] = ImColor(rgba(16, 26, 60, 250));
    st.Colors[ImGuiCol_TableRowBgAlt] = ImColor(rgba(255, 255, 255, 10));
    ImGui_ImplSDL2_InitForSDLRenderer(u.window, u.renderer);
    ImGui_ImplSDLRenderer2_Init(u.renderer);

    u.bind = InputBindings::defaults(); u.bind.load(s.extra);
    verify_rom(u, s);
    LauncherResult result = LauncherResult::Quit;
    bool running = true;
    uint32_t stick_next = 0;
    // Test hook: HANGTIME_LAUNCHER_SCRIPT="down,right,ok,..." pushes one real SDL key event every
    // 500 ms (up/down/left/right/ok/back/space/tab), the same path as a physical key.
    std::vector<std::string> script;
    if (const char* sc = getenv("HANGTIME_LAUNCHER_SCRIPT")) {
        std::string all = sc, item;
        for (char ch : all + ",") { if (ch == ',') { if (!item.empty()) script.push_back(item); item.clear(); } else item += ch; }
    }
    size_t script_pos = 0; uint32_t script_next = SDL_GetTicks() + 1500;
    while (running) {
        if (script_pos < script.size() && SDL_GetTicks() >= script_next) {
            static const std::pair<const char*, SDL_Scancode> keys[] = {{"up", SDL_SCANCODE_UP}, {"down", SDL_SCANCODE_DOWN}, {"left", SDL_SCANCODE_LEFT},
                {"right", SDL_SCANCODE_RIGHT}, {"ok", SDL_SCANCODE_RETURN}, {"back", SDL_SCANCODE_ESCAPE}, {"space", SDL_SCANCODE_SPACE}, {"tab", SDL_SCANCODE_TAB}};
            for (auto& [n, scn] : keys) if (script[script_pos] == n) {
                for (int down = 1; down >= 0; --down) {
                    SDL_Event k{}; k.type = down ? SDL_KEYDOWN : SDL_KEYUP; k.key.state = down ? SDL_PRESSED : SDL_RELEASED;
                    k.key.keysym.scancode = scn; k.key.keysym.sym = SDL_GetKeyFromScancode(scn); k.key.windowID = SDL_GetWindowID(u.window);
                    SDL_PushEvent(&k);
                }
            }
            ++script_pos; script_next = SDL_GetTicks() + 500;
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_CONTROLLERDEVICEADDED && !u.pad) u.pad = SDL_GameControllerOpen(e.cdevice.which);
            if (e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERBUTTONUP) {   // gamepad -> ImGui navigation
                static const std::pair<int, ImGuiKey> m[] = {{SDL_CONTROLLER_BUTTON_A, ImGuiKey_GamepadFaceDown}, {SDL_CONTROLLER_BUTTON_B, ImGuiKey_GamepadFaceRight},
                    {SDL_CONTROLLER_BUTTON_X, ImGuiKey_GamepadFaceLeft}, {SDL_CONTROLLER_BUTTON_Y, ImGuiKey_GamepadFaceUp},
                    {SDL_CONTROLLER_BUTTON_DPAD_UP, ImGuiKey_GamepadDpadUp}, {SDL_CONTROLLER_BUTTON_DPAD_DOWN, ImGuiKey_GamepadDpadDown},
                    {SDL_CONTROLLER_BUTTON_DPAD_LEFT, ImGuiKey_GamepadDpadLeft}, {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, ImGuiKey_GamepadDpadRight},
                    {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, ImGuiKey_GamepadL1}, {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, ImGuiKey_GamepadR1}};
                for (auto& [b, k] : m) if (e.cbutton.button == b) io.AddKeyEvent(k, e.type == SDL_CONTROLLERBUTTONDOWN);
            }
            // rebinding capture (Controls page)
            if (u.page == CONTROLS && !u.rebind_id.empty()) {
                if (e.type == SDL_KEYDOWN && u.rebind_device == 1) {
                    if (e.key.keysym.scancode != SDL_SCANCODE_ESCAPE) u.bind.keys[u.rebind_id] = {e.key.keysym.scancode};
                    u.rebind_id.clear();
                    continue;
                }
                if (e.type == SDL_CONTROLLERBUTTONDOWN && u.rebind_device == 2 && e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) { u.rebind_id.clear(); continue; }
                if (u.rebind_device == 1) continue;
            }
            Nav n = nav_from_event(e);
            if (u.page == MAIN) {
                if (n == N_OK) {
                    if (u.sel == 0) { if (u.rom_ok) { result = LauncherResult::Play; running = false; } else { u.message = "Select your NBA Hangtime ROM first."; open_page(u, s, 5); } }
                    else if (u.sel == SKIP_ITEM) s.skip_launcher = !s.skip_launcher;
                    else open_page(u, s, u.sel);
                } else if (n == N_BACK) running = false;
                else if (n) u.sel = main_move(u.sel, n);
            } else if (n == N_BACK && u.rebind_id.empty() && !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
                u.page = MAIN; u.message.clear();
            }
        }
        // gamepad rebinding: first control pressed after all were released
        if (u.page == CONTROLS && u.rebind_device == 2 && !u.rebind_id.empty() && u.pad) {
            const std::string t = pad_pressed_token(u.pad);
            if (u.rebind_wait_release) { if (t.empty()) u.rebind_wait_release = false; }
            else if (!t.empty() && t != "back") { u.bind.pad[u.rebind_id] = {t}; u.rebind_id.clear(); }
        }
        // left stick moves the main-menu selection (with repeat)
        if (u.page == MAIN && u.pad && SDL_GetTicks() >= stick_next) {
            const int ax = SDL_GameControllerGetAxis(u.pad, SDL_CONTROLLER_AXIS_LEFTX), ay = SDL_GameControllerGetAxis(u.pad, SDL_CONTROLLER_AXIS_LEFTY);
            Nav n = ay < -20000 ? N_UP : ay > 20000 ? N_DOWN : ax < -20000 ? N_LEFT : ax > 20000 ? N_RIGHT : N_NONE;
            if (n) { u.sel = main_move(u.sel, n); stick_next = SDL_GetTicks() + 220; }
        }
        int ww, wh; SDL_GetRendererOutputSize(u.renderer, &ww, &wh);
        u.s = std::min(ww / VW, wh / VH); u.ox = (ww - VW * u.s) / 2; u.oy = (wh - VH * u.s) / 2;
        io.DisplayFramebufferScale = {1, 1};

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        io.DisplaySize = {(float)ww, (float)wh};
        ImGui::NewFrame();
        const float time = SDL_GetTicks() / 1000.0f;
        background(u, ww, wh);
        title(u, u.page == MAIN);
        if (u.page == MAIN) {
            // mouse: hover selects, click activates
            const ImVec2 m = io.MousePos;
            for (int i = 0; i < TILE_COUNT + 1; ++i) {
                const bool skip = i == SKIP_ITEM;
                const ImVec2 a = skip ? u.P(110, 828) : u.P(TILES[i].x, TILES[i].y), b = skip ? u.P(560, 872) : u.P(TILES[i].x + TILES[i].w, TILES[i].y + TILES[i].h);
                if (m.x >= a.x && m.x < b.x && m.y >= a.y && m.y < b.y && (io.MouseDelta.x || io.MouseDelta.y || ImGui::IsMouseClicked(0))) {
                    u.sel = i;
                    if (ImGui::IsMouseClicked(0)) {
                        if (i == 0) { if (u.rom_ok) { result = LauncherResult::Play; running = false; } else { u.message = "Select your NBA Hangtime ROM first."; open_page(u, s, 5); } }
                        else if (skip) s.skip_launcher = !s.skip_launcher;
                        else open_page(u, s, i);
                    }
                }
            }
            for (int i = 0; i < TILE_COUNT; ++i) draw_tile(u, TILES[i], u.sel == i, time);
            const bool ss = u.sel == SKIP_ITEM;
            dl()->AddRect(u.P(110, 832), u.P(140, 862), ss ? GOLD : EDGE, u.S(5), 0, u.S(3));
            if (s.skip_launcher) dl()->AddRectFilled(u.P(117, 839), u.P(133, 855), GOLD, u.S(3));
            text(u, 24, 152, 834, ss ? GOLD : TEXT, "Start the game directly next time  (F1 in game returns here)");
            if (!u.rom_ok) text(u, 22, 330, 724, rgba(240, 120, 90), "No verified ROM yet: open GAME FILES", 0.5f);
            hints(u, "SELECT", "QUIT");
        } else {
            text(u, 46, 110, 258 - 22, GOLD, PAGE_TITLE[u.page], 0, 3);
            if (!u.message.empty()) text(u, 24, 1490, 248, rgba(240, 120, 90), u.message.c_str(), 1.0f);
            switch (u.page) {
                case GRAPHICS: page_graphics(u, s); break; case CONTROLS: page_controls(u, s); break;
                case CHEATS: page_cheats(u, s); break; case AUDIO: page_audio(u, s); break;
                case FILES: page_files(u, s); break; case ABOUT: page_about(u); break; default: break;
            }
            hints(u, "SELECT", "BACK");
        }
        ImGui::Render();
        SDL_SetRenderDrawColor(u.renderer, 8, 12, 30, 255);
        SDL_RenderClear(u.renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), u.renderer);
        SDL_RenderPresent(u.renderer);
    }
    launcher_save_settings(s);
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    if (u.pad) SDL_GameControllerClose(u.pad);
    SDL_DestroyRenderer(u.renderer);
    SDL_DestroyWindow(u.window);
    return result;
}

// First-run game files build (module_builder.hpp) with a progress screen in the launcher's style.
// Returns true when the module was built; `error` explains a failure for the player.
bool run_module_setup(const std::filesystem::path& rom, std::string& error) {
    const ModuleBuildPaths paths = module_build_paths();
    ModuleBuildProgress progress;
    std::thread worker([&] { module_build(paths, rom, progress); });
    SDL_DisplayMode dm{}; SDL_GetDesktopDisplayMode(0, &dm);
    const int h = std::min(540, dm.h * 6 / 10), w = h * 16 / 9;
    SDL_Window* window = SDL_CreateWindow(HANGTIME_PRODUCT_NAME " - preparing game files", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
    if (renderer) {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        const std::string font = launcher_utf8(launcher_exe_dir() / "assets" / "Roboto-Medium.ttf");
        ImFont* f = io.Fonts->AddFontFromFileTTF(font.c_str(), 26.0f);
        if (!f) io.Fonts->AddFontDefault();
        ImGui::StyleColorsDark();
        ImGui::GetStyle().Colors[ImGuiCol_PlotHistogram] = ImColor(ORANGE);
        ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer2_Init(renderer);
        while (!progress.done) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) ImGui_ImplSDL2_ProcessEvent(&e);   // closing waits for the build to finish
            ImGui_ImplSDLRenderer2_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0}); ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("##setup", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground);
            ImGui::SetCursorPos({io.DisplaySize.x * 0.08f, io.DisplaySize.y * 0.2f});
            ImGui::TextColored(ImColor(GOLD), "Preparing the game from your ROM");
            ImGui::SetCursorPosX(io.DisplaySize.x * 0.08f);
            ImGui::TextColored(ImColor(DIM), "One time only. The game code is generated on this computer;");
            ImGui::SetCursorPosX(io.DisplaySize.x * 0.08f);
            ImGui::TextColored(ImColor(DIM), "nothing is downloaded and your ROM is not changed.");
            ImGui::SetCursorPos({io.DisplaySize.x * 0.08f, io.DisplaySize.y * 0.55f});
            ImGui::ProgressBar(progress.fraction, {io.DisplaySize.x * 0.84f, 0});
            ImGui::SetCursorPosX(io.DisplaySize.x * 0.08f);
            ImGui::TextUnformatted(progress.get_step().c_str());
            ImGui::End();
            ImGui::Render();
            SDL_SetRenderDrawColor(renderer, 8, 12, 30, 255);
            SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
            SDL_RenderPresent(renderer);
        }
        ImGui_ImplSDLRenderer2_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext();
    }
    worker.join();
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    if (!progress.ok) error = "The game files could not be prepared.\n\n" + progress.error;
    return progress.ok;
}
