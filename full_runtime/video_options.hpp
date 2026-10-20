#pragma once
// Presentation options handed from the launcher to the RT64 renderer context. Defaults keep the
// original presentation (4:3, original refresh, RT64 integer window scale). Anything else is an
// experimental mode under evaluation (FULL_GAME.md "Widescreen / frame rate").
struct HangtimeVideoOptions {
    int aspect = 0;            // 0 original 4:3, 1 RT64 Expand (fill the window), 2 manual ratio
    double aspect_ratio = 16.0 / 9.0;
    int refresh = 0;           // 0 original (VI rate), 1 display rate, 2 manual Hz (RT64 frame interpolation)
    int refresh_hz = 120;
    int msaa = 0;              // 0, 2, 4, 8
    // RT64 upscaling of 2D sprites (texture rectangles): -1 RT64 default (scaled sprites only),
    // 0 original resolution (like the N64: no filtered edge fringes on keyed sprites), 1 scaled only, 2 all.
    int upscale2d = -1;
    // RT64 texture filtering: -1 RT64 default (the N64's 3-point filter), 1 3-point, 0 standard bilinear.
    int three_point = -1;
    double resolution = 0;     // 0 = integer scale to the window, else a fixed multiplier
    bool widescreen = false;   // --widescreen: true 16:9 live play (widescreen.cpp), aspect_ratio = target
    bool present_blocking = false;   // --present-blocking: RT64's original back-pressure (A/B; rt64_context.cpp)
    bool vulkan = false;       // --graphics-api vulkan (Windows; Linux is always Vulkan)
    // Diagnostics (rt64_context.cpp, dl_guard.cpp): --dl-check hashes each display list's inputs at
    // submission and after RT64 read them (DLRACE lines); --dl-delay-ms N waits N ms before RT64
    // reads a list, standing in for a slower CPU (Steam Deck).
    bool dl_check = false;
    int dl_delay_ms = 0;
};
extern HangtimeVideoOptions hangtime_video;
#include <atomic>
extern std::atomic<bool> hangtime_ws_toggle_request;   // F10 (main thread) -> applied by the renderer (rt64_context.cpp)
