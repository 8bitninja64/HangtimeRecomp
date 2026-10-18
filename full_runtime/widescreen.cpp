// Optional true-16:9 presentation of live play (--widescreen). Off by default: then every hook
// returns at once and the display list reaches RT64 unchanged (original 4:3 path).
//
// How the original draws a gameplay frame (FULL_GAME.md "Widescreen"): all 2D, F3D texture
// rectangles into a 320x240 framebuffer.
//  * Floor: 0x80061594 draws 131 copy-mode scanlines (0x800618C4), each a 320-texel window into a
//    pre-rendered 1410x131 perspective court image (row k at base + k*0x582, window offset
//    camX + 545, camX = word 0x8015D470). The image holds the whole court.
//  * Everything else goes through the sprite dispatcher 0x8005B770, which culls against x 0..319
//    / y 0..239 and calls one of 16 builders that clip to the same bounds (hard-coded 0x13F/0xEF).
//    The whole arena backdrop is submitted every frame and culled by the dispatcher. World and
//    screen (HUD) sprites are told apart by the object draw routine and its sprite state flag
//    0x400 (hangtime_ws_object).
// Widescreen, without changing the game's state or rules:
//  * Sprites: at dispatcher entry the sprite record's x (+0x12) is shifted by +S or -S (S =
//    extension + clamp margin), chosen so the game's own cull/clip window (shifted 0..319) keeps
//    the most of the sprite inside the wide view; at dispatcher exit the exact original x is
//    restored. The rewriter re-emits the call's rectangles as RT64 extended texture rectangles at
//    their true x (signed coordinates). Sprites fully inside 0..319 are not shifted.
//  * Floor: each scanline's window is widened from the same court image row.
//  * View limit at the arena ends: the camera travels further than the 16:9 view has crowd art
//    for (the original limit only keeps the 4:3 view inside it). World draws (floor, backdrop,
//    players, ball) are offset by d = 0.80*|camX| - 226 (when positive, <= CLAMP_MAX) so the view
//    stops at the art's edge; the crowd art scrolls 0.80 px per camera unit (fit over 2544 traced
//    frames, checked against the measured coverage in view_clamp). HUD keeps its positions; the
//    game's camera and logic are untouched.
//  * Scissor and full-screen fills: full width in live-play frames.
//  * Every other frame (menus, team select, matchup, breaks, stats): rect aspect ADJUST, so the
//    original 320x240 picture is shown unstretched at 4:3, pillarboxed in black.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <vector>
#include "hangtime_full.h"

std::atomic<bool> hangtime_ws_enabled{false};   // read by the game and renderer threads; F10 toggles it
// Per-PROGRESS-second statistics (rt64_context.cpp): last camera x, largest applied / needed clamp.
int ws_stat_camx = 0, ws_stat_clamp = 0, ws_stat_need = 0;
void hangtime_ws_stats(int* camx, int* clamp, int* need) { *camx = ws_stat_camx; *clamp = ws_stat_clamp; *need = ws_stat_need; ws_stat_clamp = ws_stat_need = 0; }
double hangtime_ws_extend = 0;   // extra 4:3-space pixels on each side of the view (16:9: 53.33)
static int ws_shift = 0;         // sprite shift: extension + clamp margin
static constexpr int CLAMP_MAX = 64;   // largest view clamp offset (pixels)

namespace {
constexpr uint32_t DL_POINTER = 0x800C4338;       // game's display-list write pointer
constexpr uint32_t CAMERA_X = 0x8015D470;         // floor camera x (window offset = camX + 545)
constexpr int FLOOR_ROW = 0x582, FLOOR_OFFSET = 545, FLOOR_WINDOW = 320;
uint8_t* ram = nullptr;
uint32_t& word(uint32_t a) { return *reinterpret_cast<uint32_t*>(ram + (a & 0x1FFFFFFC)); }
int16_t& half(uint32_t a) { return *reinterpret_cast<int16_t*>(ram + ((a & 0x1FFFFFFE) ^ 2)); }

// One dispatcher call: its call site, the display-list range it produced, the sprite's true
// screen extent (before any shift) and the shift applied.
struct CallInfo { uint32_t caller, dl_begin, dl_end; int left, right, top, bottom; uint16_t flags; int shift; uint32_t object; bool screen; };
struct Frame { uint32_t dl_start = 0; bool floor = false; int32_t camx = 0; std::vector<CallInfo> calls; int clamp = 0; };
std::mutex frames_mutex;
std::deque<Frame> frames;     // frames being built / waiting for the renderer
Frame last_frame;             // last live frame rewritten (HANGTIME_WS_TRACE output)
const bool ws_trace = getenv("HANGTIME_WS_TRACE") != nullptr;
// Dispatcher call in progress (game thread; the dispatcher is not re-entrant).
uint32_t call_record = 0; int16_t call_x = 0; bool call_open = false;
CallInfo call_info{};
uint32_t caller = 0;          // set before each call site (hangtime_ws_caller)
uint32_t cur_object = 0; bool cur_screen = false;   // object draw routine in progress (hangtime_ws_object)
bool cur_unculled = false;   // this player draw passed the original screen cull only because of the wide view
std::atomic<unsigned> ws_unculled_sprites{0}, ws_unculled_shifted{0};   // their dispatcher calls / of those shifted into the wide window

int choose_shift(int x, int left, int right, uint16_t flags) {
    if (!(flags & 0x2000) && left >= 0 && right <= 319) return 0;        // fully inside: unchanged
    if (right < -ws_shift || left > 319 + ws_shift) return 0;            // outside the wide view
    if (flags & 0x2000) {                                                // scaled: by anchor
        if (x >= 60 && x <= 259) return 0;
        return x < 160 ? ws_shift : -ws_shift;
    }
    // The shift whose clip window (shifted 0..319) keeps most of the sprite in the widest view
    // (extension + clamp); ties keep the original (0).
    auto kept = [&](int sh) {
        const int lo = std::max({left, -sh, -ws_shift}), hi = std::min({right, 319 - sh, 319 + ws_shift});
        return hi >= lo ? hi - lo + 1 : 0;
    };
    const int k0 = kept(0), kl = kept(ws_shift), kr = kept(-ws_shift);
    return (kl > k0 && kl >= kr) ? ws_shift : (kr > k0 ? -ws_shift : 0);
}
}

extern "C" void hangtime_ws_caller(uint32_t call_site) { caller = call_site; }
// Object draw routines draw(object, view): 0x8005FFAC sprite objects, 0x800606D4 scaled objects
// (players), 0x80061BEC ball, 0x8006243C text. Screen-space (HUD) objects are text objects and
// sprite objects whose sprite state (object+0x2C) has flag 0x400: 0x8005FFAC then takes x as is,
// otherwise it subtracts the view camera (view+0x14) -- see FULL_GAME.md "Widescreen".
extern "C" void hangtime_ws_object(uint8_t* rdram, uint64_t object, uint32_t draw) {
    if (!hangtime_ws_enabled) return;
    ram = rdram;
    cur_object = (uint32_t)object;
    cur_unculled = false;
    cur_screen = draw == 0x8006243C || (draw == 0x8005FFAC && (half(word(cur_object + 0x2C)) & 0x400));
}

extern "C" void hangtime_ws_frame_begin(uint8_t* rdram, recomp_context*) {   // 0x8005643C entry
    if (!hangtime_ws_enabled) return;
    ram = rdram;
    std::lock_guard<std::mutex> lock(frames_mutex);
    frames.push_back(Frame{word(DL_POINTER)});
    while (frames.size() > 8) frames.pop_front();
}

extern "C" void hangtime_ws_floor(uint8_t* rdram, recomp_context*) {          // 0x80061594 entry
    if (!hangtime_ws_enabled) return;
    ram = rdram;
    std::lock_guard<std::mutex> lock(frames_mutex);
    if (!frames.empty()) { frames.back().floor = true; frames.back().camx = (int32_t)word(CAMERA_X); }
}

extern "C" void hangtime_ws_sprite_begin(uint8_t* rdram, recomp_context* ctx) {  // 0x8005B770 entry
    call_open = false;
    if (!hangtime_ws_enabled) return;
    ram = rdram;
    {
        std::lock_guard<std::mutex> lock(frames_mutex);
        if (frames.empty() || !frames.back().floor) return;   // only live-play frames
    }
    const uint32_t s = (uint32_t)ctx->r4;
    const int x = half(s + 0x12), y = half(s + 0x14), offx = half(s + 0x16), offy = half(s + 0x18);
    const int w = (uint16_t)half(s + 0x1A), h = (uint16_t)half(s + 0x1C);
    const uint16_t flags = (uint16_t)half(s + 0x1E);
    int left, right;
    if (flags & 0x2000) { left = right = x; }                            // scaled: extent unknown here
    else if (flags & 0x8000) { right = x - offx; left = right - w + 1; } // mirrored (dispatcher test)
    else { left = x + offx; right = left + w - 1; }
    const int shift = choose_shift(x, left, right, flags);
    call_record = s; call_x = (int16_t)x; call_open = true;
    call_info = {caller, word(DL_POINTER), 0, left, right, y + offy, y + offy + h - 1, flags, shift, cur_object, cur_screen};
    if (cur_unculled) { ++ws_unculled_sprites; if (shift) ++ws_unculled_shifted; }
    if (shift) half(s + 0x12) = (int16_t)(x + shift);
}

// Player draw routine 0x800606D4 culls the whole player against the original screen before any
// sprite reaches the dispatcher: 0x80060AC8 loads the player's right edge (t9) and 0x80060AD0
// skips the draw if it is < 0; 0x80060AD8 loads its left edge (t7) and 0x80060AE0/E4 skip it if
// >= 320. In 16:9 that band is visible, so a player walking into it vanished. In live-play frames
// the edges are presented to these two tests as just inside when they are within the wide view
// (+-ws_shift); the registers get their exact original values back right after the tests (fall
// through at 0x80060AEC, or the function exit 0x80061478 taken by the skip branches). t9/t7 are
// read only by these tests; the game's memory and decisions elsewhere are unchanged.
static int32_t pc_t9 = 0, pc_t7 = 0;
static bool pc_t9_set = false, pc_t7_set = false;
std::atomic<unsigned> ws_player_unculled{0};   // players drawn only because of the wide view (diagnostic)
// Per WSVIEW second (rt64_context.cpp): player draws kept by the wide view, their dispatcher calls,
// and how many of those calls were shifted so the dispatcher's own clip keeps them.
unsigned hangtime_ws_unculled(unsigned* sprites, unsigned* shifted) {
    *sprites = ws_unculled_sprites.exchange(0); *shifted = ws_unculled_shifted.exchange(0);
    return ws_player_unculled.exchange(0);
}
extern "C" void hangtime_ws_player_cull(uint8_t* rdram, recomp_context* ctx, int step) {
    if (step == 2) {                                   // after the tests: exact original registers
        if (pc_t9_set) ctx->r25 = (gpr)(int64_t)pc_t9;
        if (pc_t7_set) ctx->r15 = (gpr)(int64_t)pc_t7;
        pc_t9_set = pc_t7_set = false;
        return;
    }
    if (!hangtime_ws_enabled) return;
    ram = rdram;
    {
        std::lock_guard<std::mutex> lock(frames_mutex);
        if (frames.empty() || !frames.back().floor) return;   // only live-play frames
    }
    if (step == 0) {                                   // before 0x80060AD0: bltz t9 (right edge < 0)
        const int32_t right = (int32_t)ctx->r25;
        if (right < 0 && right >= -ws_shift) { pc_t9 = right; pc_t9_set = true; ctx->r25 = 0; ++ws_player_unculled; cur_unculled = true; }
    } else {                                           // before 0x80060AE0: slti t7, 320 (left edge >= 320)
        const int32_t left = (int32_t)ctx->r15;
        if (left >= 320 && left < 320 + ws_shift) { pc_t7 = left; pc_t7_set = true; ctx->r15 = 319; ++ws_player_unculled; cur_unculled = true; }
    }
}

extern "C" void hangtime_ws_sprite_end(uint8_t* rdram, recomp_context*) {      // 0x8005C140 (jr ra)
    if (!call_open) return;
    ram = rdram;
    if (call_info.shift) half(call_record + 0x12) = call_x;              // exact original value
    call_info.dl_end = word(DL_POINTER);
    std::lock_guard<std::mutex> lock(frames_mutex);
    if (!frames.empty()) frames.back().calls.push_back(call_info);
    call_open = false;
}

void hangtime_ws_trace_write(const char* path) {
    if (FILE* f = fopen(path, "w")) {
        fprintf(f, "# dispatcher calls of the last rewritten live frame: call site, dl range, true x extent, y extent, flags, shift\n");
        fprintf(f, "# camx=%d floor=%d clamp=%d\n", last_frame.camx, last_frame.floor, last_frame.clamp);
        for (const auto& c : last_frame.calls)
            fprintf(f, "ra=%08X dl=%06X..%06X x %4d..%4d y %4d..%4d flags=%04X shift=%+d obj=%08X%s\n", c.caller, c.dl_begin & 0x7FFFFF, c.dl_end & 0x7FFFFF,
                    c.left, c.right, c.top, c.bottom, c.flags, c.shift, c.object, c.screen ? " screen" : "");
        fclose(f);
    }
}

// ---- display-list rewriter (renderer thread) ----
namespace {
constexpr uint32_t OUT_BASE = 0x00C00000, OUT_SIZE = 0x00100000;   // above the game's 8 MiB, inside RT64's 16 MiB DMA mask
constexpr uint32_t EXT = 0x64;                                     // RT64 extended opcode
constexpr uint32_t ORIGIN_NONE = 0x800, ORIGIN_LEFT = 0, ORIGIN_RIGHT = 0x400;
struct Out {
    uint32_t pos = OUT_BASE;
    bool overflow = false;
    void put(uint32_t a, uint32_t b) {
        if (pos + 8 > OUT_BASE + OUT_SIZE - 16) { overflow = true; return; }
        word(pos) = a; word(pos + 4) = b; pos += 8;
    }
};
uint32_t phys(uint32_t a, const uint32_t* seg) { return (seg[(a >> 24) & 0xF] + (a & 0xFFFFFF)) & 0x7FFFFF; }
void ex_texrect(Out& o, uint32_t tile, bool flip, int ulx, int uly, int lrx, int lry, uint32_t st, uint32_t d) {
    o.put(EXT << 24 | 0x000002, (tile & 7) | ORIGIN_NONE << 3 | ORIGIN_NONE << 15 | (flip ? 1u << 7 : 0));
    o.put((uint32_t)(uint16_t)ulx << 16 | (uint16_t)uly, (uint32_t)(uint16_t)lrx << 16 | (uint16_t)lry);
    o.put(st, d);
}
void ex_fill_full(Out& o, uint32_t lry) {                          // full-width fill (both extended origins)
    o.put(EXT << 24 | 0x000003, ORIGIN_LEFT | ORIGIN_RIGHT << 12);
    o.put(0, lry & 0xFFF);                                         // 0 from the left edge .. 0 from the right edge
}

// View clamp d (pixels, content moved right by d) so the wide view [-ext-d, 319+ext-d] stays inside
// the backdrop art of this frame: per scanline of the backdrop band, how far the game's own opaque
// world pieces (not HUD, not scaled sprites) continue beyond the original 4:3 edges. Rows whose
// original edge is not covered by these pieces (covered by something else, or a hole the original
// shows too) say nothing about the art's end and are skipped.
int view_clamp(const Frame& f, double ext, int* raw) {
    double cov_left = -1e9, cov_right = 1e9;
    bool any_left = false, any_right = false;
    std::vector<std::pair<int, int>> iv;
    for (int y = 0; y <= 108; y += 2) {
        iv.clear();
        for (const auto& c : f.calls)
            if (!c.screen && !(c.flags & 0x2000) && c.top <= y && c.bottom >= y) iv.push_back({c.left, c.right});
        std::sort(iv.begin(), iv.end());
        for (size_t i = 0; i < iv.size();) {                       // merge into runs (art seams up to 6 px)
            int ra = iv[i].first, rb = iv[i].second;
            for (++i; i < iv.size() && iv[i].first <= rb + 6; ++i) rb = std::max(rb, iv[i].second);
            if (ra <= 3 && rb >= 3) { any_left = true; cov_left = std::max(cov_left, (double)ra); }      // run through the old left edge
            if (ra <= 316 && rb >= 316) { any_right = true; cov_right = std::min(cov_right, (double)rb); } // run through the old right edge
        }
    }
    if (!any_left) cov_left = -1e9;
    if (!any_right) cov_right = 1e9;
    const double need_right = (319.0 + ext) - cov_right, need_left = cov_left + ext;
    double d = 0;
    if (need_right > 0 && need_left > 0) d = 0;                   // both ends short: leave the view alone
    else if (need_right > 0) d = std::ceil(need_right);
    else if (need_left > 0) d = -std::ceil(need_left);
    *raw = (int)d;
    return (int)std::clamp(d, (double)-CLAMP_MAX, (double)CLAMP_MAX);
}
}

uint32_t hangtime_ws_rewrite(uint8_t* rdram, uint32_t dl) {
    if (!hangtime_ws_enabled) return dl;
    ram = rdram;
    Frame f;
    {
        std::lock_guard<std::mutex> lock(frames_mutex);
        // The task's list starts at (or a few commands before) the frame's first display defaults.
        auto it = std::find_if(frames.begin(), frames.end(), [&](const Frame& fr) {
            const uint32_t s = fr.dl_start & 0x7FFFFF; return s >= dl && s < dl + 0x400; });
        if (it != frames.end()) { f = std::move(*it); frames.erase(frames.begin(), it + 1); }
    }
    const bool live = f.floor;
    int raw_clamp = 0;
    // The applied clamp is a fixed function of the camera (a presentation-side camera limit): the
    // crowd art's ends move 0.80 px per camera unit (parallax), so the 16:9 view needs
    // d = 0.80*|camx| - 234 at the ends (fit over 2544 frames; residual -5..+7 from animated crowd
    // pieces), applied with an 8-px margin. Measured coverage (view_clamp) is kept as a diagnostic.
    if (live) view_clamp(f, hangtime_ws_extend, &raw_clamp);
    const double over = 0.80 * std::abs(f.camx) - 226.0;
    const int d = live && over > 0 ? (f.camx > 0 ? 1 : -1) * std::min(CLAMP_MAX, (int)std::ceil(over)) : 0;
    f.clamp = d;
    if (live) { ws_stat_camx = f.camx; if (std::abs(d) > std::abs(ws_stat_clamp)) ws_stat_clamp = d; if (std::abs(raw_clamp) > std::abs(ws_stat_need)) ws_stat_need = raw_clamp; }
    if (ws_trace && live) last_frame = f;
    static const bool log_clamp = getenv("HANGTIME_WS_LOGCLAMP") != nullptr;   // per-frame view clamp (diagnostic)
    if (log_clamp && live) fprintf(stderr, "WSCLAMP camx=%d d=%d need=%d\n", f.camx, d, raw_clamp);
    static int bad_dumps = 0;                          // trace: frames whose clamp need looks implausible
    if (ws_trace && live && std::abs(raw_clamp) > 80 && bad_dumps < 3) {
        ++bad_dumps;
        char path[64]; snprintf(path, sizeof(path), "ws_bad_clamp_%d.txt", bad_dumps);
        hangtime_ws_trace_write(path);
    }
    std::sort(f.calls.begin(), f.calls.end(), [](const CallInfo& a, const CallInfo& b) { return a.dl_begin < b.dl_begin; });
    auto call_at = [&](uint32_t a) -> const CallInfo* {
        auto it = std::upper_bound(f.calls.begin(), f.calls.end(), a, [](uint32_t v, const CallInfo& c) { return v < (c.dl_begin & 0x7FFFFF); });
        if (it == f.calls.begin()) return nullptr;
        --it;
        return a < (it->dl_end & 0x7FFFFF) ? &*it : nullptr;
    };
    const int ext_floor = (int)std::ceil(hangtime_ws_extend) + 1;
    Out o;
    o.put(0x00525464, 1u << 28 | EXT);                 // gEXEnable
    o.put(EXT << 24 | 0x000033, 2);                    // gEXSetRectAspect(G_EX_ASPECT_ADJUST)
    uint32_t seg[16]{}, fill_color = 0, cycle = 0;
    std::vector<uint32_t> stack;
    uint32_t pc = dl & 0x7FFFFF;
    for (int guard = 0; guard < 200000 && !o.overflow; ++guard) {
        const uint32_t a = word(pc), b = word(pc + 4), op = a >> 24;
        if (op == 0x06) {                                              // G_DL: inline
            if (((a >> 16) & 0xFF) == 0) stack.push_back(pc + 8);
            pc = phys(b, seg);
            continue;
        }
        if (op == 0xB8) {                                              // G_ENDDL
            if (stack.empty()) break;
            pc = stack.back(); stack.pop_back();
            continue;
        }
        if (op == 0xBC && (a & 0xFF) == 0x06) seg[((a >> 8) & 0xFFFF) >> 2 & 0xF] = b & 0xFFFFFF;
        if (op == 0xF7) fill_color = b;
        if (op == 0xBA && ((a >> 8) & 0xFF) <= 20 && ((a >> 8) & 0xFF) + (a & 0xFF) >= 22) cycle = (b >> 20) & 3;  // G_MDSFT_CYCLETYPE
        if (op == 0xF6 && ((a >> 14) & 0x3FF) >= 319 && ((b >> 14) & 0x3FF) == 0 && ((b >> 2) & 0x3FF) == 0) {   // full-screen fill
            if (live) ex_fill_full(o, a);                              // live play: the same fill across 16:9
            else if (cycle == 3) {                                     // other screens' clear: black bars + the original
                o.put(0xF7000000, 0); ex_fill_full(o, a); o.put(0xF7000000, fill_color); o.put(a, b);
            }
            else o.put(a, b);                                          // translucent overlay outside live play: 4:3 only
            pc += 8;
            continue;
        }
        if (live && op == 0xED) {                                      // scissor: whole 16:9 width
            o.put(EXT << 24 | 0x000005, (b >> 24 & 3) | ORIGIN_LEFT << 2 | ORIGIN_RIGHT << 14);
            o.put(0 << 16 | (a & 0xFFF), (b & 0xFFF));                // RIGHT-origin lrx is relative to the right edge
            pc += 8;
            continue;
        }
        if (live && op == 0xFD && (a >> 16) == 0xFD48 && (word(pc + 8) >> 16) == 0xF548 && (word(pc + 0x18) >> 24) == 0xF4 &&
            (word(pc + 0x30) >> 24) == 0xF2 && (word(pc + 0x38) >> 24) == 0xE4 && (word(pc + 0x40) >> 24) == 0xB4 &&
            word(pc + 0x4C) == 0x10001000 && (((word(pc + 0x38) >> 12) & 0xFFF) - ((word(pc + 0x3C) >> 12) & 0xFFF)) == 0x13F * 4) {
            // Floor scanline (0x800618C4): widen the window within its 1410-texel court row (moved by d).
            const uint32_t align = (a & 0xFFF) - 0x13F;                // texels before the window in the aligned load
            const uint32_t start = (b & 0x7FFFFF) + align;             // first visible texel
            const int offset = f.camx + FLOOR_OFFSET;                  // window offset inside the row
            if (offset < 0 || offset > FLOOR_ROW - FLOOR_WINDOW) { o.put(a, b); pc += 8; continue; }   // unexpected: keep original
            const int left = std::max(0, offset - ext_floor - d), right = std::min(FLOOR_ROW, offset + FLOOR_WINDOW + ext_floor - d);
            if (right <= left) { o.put(a, b); pc += 8; continue; }
            const uint32_t nstart = start + (uint32_t)(left - offset), nw = (uint32_t)(right - left);
            const uint32_t u = nstart & 7, addr = (b & 0xFF800000) | (nstart & ~7u), line = (u + nw + 7) >> 3;
            const uint32_t tr0 = word(pc + 0x38), tr1 = word(pc + 0x3C);
            const int x0 = (int)((tr1 >> 12) & 0xFFF) + (left - offset + d) * 4, uly = tr1 & 0xFFF, lry = tr0 & 0xFFF;
            o.put(0xFD480000 | ((u + nw - 1) & 0xFFF), addr);
            o.put(0xF5480000 | (line & 0x1FF) << 9, 0x07000000);
            o.put(0xE6000000, 0);
            o.put(0xF4000000 | u << 14, 0x07000000 | (u + nw - 1) << 14);
            o.put(0xE7000000, 0);
            o.put(0xF5480000 | (line & 0x1FF) << 9, 0);
            o.put(0xF2000000 | u << 14, (u + nw - 1) << 14);
            ex_texrect(o, 0, false, x0, uly, x0 + (int)(nw - 1) * 4, lry, (u << 5) << 16, 0x10001000);
            pc += 0x50;
            continue;
        }
        if (live && (op == 0xE4 || op == 0xE5) && (word(pc + 8) >> 24) == 0xB4 && (word(pc + 16) >> 24) == 0xB3) {
            if (const CallInfo* c = call_at(pc)) {
                // Back to the sprite's true x, plus the view clamp for world sprites (not the HUD).
                const int move = -c->shift + (c->screen ? 0 : d);
                if (move) {
                    const int lrx = (a >> 12) & 0xFFF, lry = a & 0xFFF, ulx = (b >> 12) & 0xFFF, uly = b & 0xFFF;
                    ex_texrect(o, (b >> 24) & 7, op == 0xE5, ulx + move * 4, uly, lrx + move * 4, lry, word(pc + 12), word(pc + 20));
                    pc += 24;
                    continue;
                }
            }
        }
        o.put(a, b);
        pc += 8;
    }
    o.put(0xB8000000, 0);
    if (o.overflow) { fprintf(stderr, "WIDESCREEN: rewritten display list overflow; original frame drawn\n"); return dl; }
    return OUT_BASE;
}

// Ratio of the wide view (also when it starts disabled: F10 enables it later).
void hangtime_ws_configure(double aspect) {
    hangtime_ws_extend = std::max(0.0, (240.0 * aspect - 320.0) / 2.0);
    ws_shift = (int)std::ceil(hangtime_ws_extend) + CLAMP_MAX;
}

void hangtime_ws_set_enabled(bool on) { hangtime_ws_enabled = on; }
bool hangtime_ws_is_enabled() { return hangtime_ws_enabled; }
