// NBA Hangtime native PC launcher: the whole recompiled game on N64ModernRuntime + RT64.
// Startup follows librecomp's recomp::start (IPL3 1 MiB image load, IPL3 low-memory variables,
// game thread on ultramodern) without its mod system or ROM-library UI.
#define SDL_MAIN_HANDLED
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#else
#include <execinfo.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#endif
#include "SDL.h"
#ifdef _WIN32
#include "SDL_syswm.h"   // HWND for RT64 D3D12 (on Linux it would pull X11 macros such as None)
#endif
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/renderer_context.hpp"
#include "librecomp/game.hpp"
#include "librecomp/rsp.hpp"
#include "librecomp/addresses.hpp"
#include "recomp.h"
#include "lightweightsemaphore.h"
#include "launcher.hpp"
#include "launcher_ui.hpp"
#include "input_map.hpp"
#include "cheats.hpp"
#include "video_options.hpp"
#include "perf.hpp"
#include "render_gpu.hpp"

#include "game_module.hpp"   // the recompiled game lives in a separately built module
#include "module_builder.hpp"

void hangtime_register_functions();
std::unique_ptr<ultramodern::renderer::RendererContext> hangtime_create_rt64_context(uint8_t* rdram, ultramodern::renderer::WindowHandle window, bool developer_mode);

static SDL_Window* window = nullptr;
static bool controller_connected = true;
HangtimeVideoOptions hangtime_video;
void hangtime_ws_configure(double aspect);  // widescreen.cpp
void hangtime_ws_set_enabled(bool on);
std::atomic<bool> hangtime_ws_toggle_request{false};
static std::atomic<bool> quit_requested{false};

// ---- pieces librecomp's recomp.cpp normally provides (same semantics, no mod system) ----
std::atomic_bool exited = false;
moodycamel::LightweightSemaphore graphics_shutdown_ready;
std::filesystem::path config_path = std::filesystem::current_path() / "hangtime_data";
static std::atomic<bool> game_started{false};
bool ultramodern::is_game_started() { return game_started.load(); }
void ultramodern::quit() { exited.store(true); quit_requested = true; }
std::u8string recomp::current_game_id() { return u8"nba_hangtime_us"; }
// Game threads start at their original entry through the generated function lookup.
void run_thread_function(uint8_t* rdram, uint64_t addr, uint64_t sp, uint64_t arg) {
    recomp_context ctx{};
    ctx.r29 = sp; ctx.r4 = arg; ctx.mips3_float_mode = 0; ctx.f_odd = &ctx.f0.u32h;
    ctx.status_reg = 0xFF01;  // libultra threads start with all interrupts unmasked and IE set
    get_function((int32_t)addr)(rdram, &ctx);
}

// ---- librecomp game hooks this launcher owns (no saves: the cart has no EEPROM/SRAM/flash) ----
recomp::SaveType recomp::get_save_type() { return recomp::SaveType::None; }
bool recomp::eeprom_allowed() { return false; }
bool recomp::sram_allowed() { return false; }
bool recomp::flashram_allowed() { return false; }
extern "C" void osGetMemSize_recomp(uint8_t*, recomp_context* ctx) { ctx->r2 = 8 * 1024 * 1024; }

// ---- input: keyboard + first SDL game controller as controller 1 ----
// N64 button bits. The game's default action table (0x80035FE0) is Shoot = C-Down,
// Pass = C-Right, Turbo = A; B has no action (FULL_GAME.md). Host defaults put those actions
// where a player expects them; the game's own CONTROLLER menu can still remap them.
enum : uint16_t { N_A = 0x8000, N_B = 0x4000, N_Z = 0x2000, N_START = 0x1000, N_DU = 0x0800, N_DD = 0x0400, N_DL = 0x0200,
    N_DR = 0x0100, N_L = 0x0020, N_R = 0x0010, N_CU = 0x0008, N_CD = 0x0004, N_CL = 0x0002, N_CR = 0x0001 };
// Players 1-4 = N64 controllers 1-4: SDL game controllers fill the four slots in connection order
// (hot-plug fills the first free slot); the keyboard drives one player (keyboard_player, default
// player 1). The game reads which controllers are present only at boot (osContInit): a slot counts
// as connected when a pad or the keyboard is assigned to it then, or always with controllers=4.
static std::atomic<SDL_GameController*> pads[4] = {};
static SDL_JoystickID pad_ids[4] = {-1, -1, -1, -1};
static int keyboard_player = 0;    // 0-based; settings keyboard_player=N / --keyboard-player N (1-based)
static bool always_four = false;   // settings controllers=4 / --controllers 4
// The controller port is the player number; players 1-2 are one team, 3-4 the other. pad_order is
// the order in which gamepads fill the ports: 1234 (second pad = teammate) or 1324 (second pad =
// opponent, head-to-head). Settings pad_order=, --pad-order.
static int pad_order[4] = {0, 1, 2, 3};
static void set_pad_order(const std::string& digits) {
    if (digits.size() != 4) return;
    int o[4], seen = 0;
    for (int i = 0; i < 4; ++i) { o[i] = digits[i] - '1'; if (o[i] < 0 || o[i] > 3 || (seen & (1 << o[i]))) return; seen |= 1 << o[i]; }
    std::copy(o, o + 4, pad_order);
}
static int pad_slot_of(SDL_JoystickID id) { for (int s = 0; s < 4; ++s) if (pad_ids[s] == id) return s; return -1; }
static void pad_assign(int device_index) {   // main thread
    if (!SDL_IsGameController(device_index) || pad_slot_of(SDL_JoystickGetDeviceInstanceID(device_index)) >= 0) return;
    for (int s : pad_order) {
        if (pads[s].load()) continue;
        SDL_GameController* c = SDL_GameControllerOpen(device_index);
        if (!c) return;
        pad_ids[s] = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c));
        pads[s] = c;
        char* m = SDL_GameControllerMapping(c);
        fprintf(stderr, "GAMEPAD player=%d %s mapping=%s\n", s + 1, SDL_GameControllerName(c), m ? m : "none");
        SDL_free(m);
        return;
    }
}
static void pad_release(SDL_JoystickID id) {   // main thread: unplugged
    const int s = pad_slot_of(id);
    if (s < 0) return;
    SDL_GameController* old = pads[s].exchange(nullptr);
    pad_ids[s] = -1;
    fprintf(stderr, "GAMEPAD player=%d removed\n", s + 1);
    SDL_GameControllerClose(old);
}
// Scripted presses for automated tests, delivered through this same controller callback like a
// held key: --press SECONDS:BUTTON (wall clock, 200 ms), --press-frame VI:BUTTON (12 VI) and
// --press-poll POLL:BUTTON (12 controller polls). Polls are the game's own input frames
// (osContStartReadData), so poll timing follows the game logic even when its frame rate varies.
// SL/SR/SU/SD hold the analog stick fully in that direction.
// Any of these (and --key-*/--pad-*) also accepts a state-gated time "mMODE[#K]+POLLS": POLLS game
// polls into the K-th (default first) entry of the game's mode word (0x800A71C4, read-only) into
// MODE. Screen durations vary between runs in both VI and polls (boot, loading), so tests gate
// on game state.
enum class Clock { Wall, Vi, Poll, Mode };
struct ScriptedPress { double at; uint32_t mask; Clock clock; int mode; int entry; };
static std::vector<ScriptedPress> scripted;
static const auto launch_time = std::chrono::steady_clock::now();
static std::atomic<uint32_t> vi_count{0}, poll_count{0}, mode_entry_poll{0};
static std::atomic<int> current_mode{-1};
static std::atomic<uint32_t> mode_entries[64];  // entries into each mode value so far
static void on_vi() { perf_event('V', ++vi_count); }
uint32_t hangtime_poll_count() { return poll_count.load(); }
static uint8_t* rdram_base = nullptr;
static uint16_t guest_half(uint32_t addr) { return *reinterpret_cast<const uint16_t*>(rdram_base + ((addr - 0x80000000u) ^ 2)); }
static void on_poll() {  // osContStartReadData: one game input frame
    const uint32_t poll = ++poll_count;
    perf_event('P', poll);
    cheats_frame(rdram_base);
    const int mode = (int16_t)guest_half(0x800A71C4);
    if (mode != current_mode.load()) {
        if (mode >= 0 && mode < 64) ++mode_entries[mode];
        mode_entry_poll = poll; current_mode = mode;
    }
}
static bool parse_time(const std::string& t, Clock fallback, double& at, Clock& clock, int& mode, int& entry) {
    if (t.empty()) return false;
    entry = 1;
    if (t[0] == 'm') {
        const auto plus = t.find('+'), hash = t.find('#');
        mode = atoi(t.substr(1).c_str());
        if (hash != std::string::npos) entry = atoi(t.substr(hash + 1).c_str());
        at = plus == std::string::npos ? 0 : atof(t.substr(plus + 1).c_str());
        clock = Clock::Mode;
        return mode >= 0 && mode < 64 && entry >= 1;
    }
    at = atof(t.c_str()); clock = fallback; mode = -1;
    return true;
}
static constexpr uint32_t STICK_L = 1u << 16, STICK_R = 1u << 17, STICK_U = 1u << 18, STICK_D = 1u << 19;
static uint32_t button_mask(const std::string& n) {
    static const std::pair<const char*, uint32_t> map[] = {{"A",N_A},{"B",N_B},{"Z",N_Z},{"START",N_START},
        {"DU",N_DU},{"DD",N_DD},{"DL",N_DL},{"DR",N_DR},{"L",N_L},{"R",N_R},{"CU",N_CU},{"CD",N_CD},{"CL",N_CL},{"CR",N_CR},
        {"SL",STICK_L},{"SR",STICK_R},{"SU",STICK_U},{"SD",STICK_D}};
    for (auto& [k, v] : map) if (n == k) return v;
    return 0;
}
// Repeats: "FROM..TO/STEP:WHAT" (FROM may carry a gated prefix, e.g. m3+600..9000/90:CD) expands
// to one press every STEP from FROM through TO.
static std::vector<std::string> expand_repeats(const std::string& spec) {
    const auto colon = spec.find(':'), dots = spec.find("..");
    if (dots == std::string::npos || colon == std::string::npos || dots > colon) return {spec};
    const std::string left = spec.substr(0, dots), right = spec.substr(dots + 2, colon - dots - 2), what = spec.substr(colon);
    const auto num = left.find_last_not_of("0123456789.") + 1, slash = right.find('/');
    const double from = atof(left.substr(num).c_str()), to = atof(right.substr(0, slash).c_str());
    const double step = slash == std::string::npos ? 0 : atof(right.substr(slash + 1).c_str());
    std::vector<std::string> out;
    for (double v = from; step > 0 && v <= to && out.size() < 100000; v += step) out.push_back(left.substr(0, num) + std::to_string(v) + what);
    return out.empty() ? std::vector<std::string>{spec} : out;
}
static double clock_now(Clock c, int mode, int entry) {
    if (c == Clock::Vi) return vi_count.load();
    if (c == Clock::Poll) return poll_count.load();
    if (c == Clock::Mode) return current_mode.load() == mode && mode_entries[mode].load() == (uint32_t)entry ? (double)poll_count.load() - mode_entry_poll.load() : -1e9;
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - launch_time).count();
}
static uint32_t scripted_buttons() {
    uint32_t b = 0;
    for (auto& p : scripted) {
        const double now = clock_now(p.clock, p.mode, p.entry), hold = p.clock == Clock::Wall ? 0.2 : 12;
        if (now >= p.at && now < p.at + hold) b |= p.mask;
    }
    return b;
}
// --input-trace: one line whenever the host input or the game's own decoded input changes.
// game= is the game's internal button word for player 1 (0x8015CE68, built by 0x8005E804) and
// sw= its arcade switch word (0x800A65B0, 0x80024888: 1/2/4/8 = up/down/left/right, 0x10 Shoot,
// 0x20 Pass, 0x40 Turbo, 0x80 Start). Read-only observations, one poll behind the host values.
static bool input_trace = false;
static bool use_keyboard = true;  // --no-keyboard: automated runs ignore the physical keyboard
static void trace_input(uint16_t buttons, float x, float y) {
    static uint64_t last = ~0ull;
    const uint16_t game = guest_half(0x8015CE68), sw = guest_half(0x800A65B0);
    const uint16_t sw2 = guest_half(0x800A65B2), sw3 = guest_half(0x800A65B4), sw4 = guest_half(0x800A65B6);   // players 2-4 (0x80024888: 0x800A65B0 + 2*player)
    const int sx = (int)(x * 100), sy = (int)(y * 100);
    const uint64_t key = (uint64_t)buttons | (uint64_t)(uint8_t)sx << 16 | (uint64_t)(uint8_t)sy << 24 | (uint64_t)game << 32 | (uint64_t)sw << 48;
    static uint64_t last_others = ~0ull;
    const uint64_t others = (uint64_t)sw2 | (uint64_t)sw3 << 16 | (uint64_t)sw4 << 32;
    if (key == last && others == last_others) return;
    last = key; last_others = others;
    fprintf(stderr, "INPUT poll=%u vi=%u host=%04X stick=%d,%d game=%04X sw=%04X sw2=%04X sw3=%04X sw4=%04X\n", poll_count.load(), vi_count.load(), buttons, sx, sy, game, sw, sw2, sw3, sw4);
}
// Host bindings (input_map.cpp; defaults = the verified mapping, overridable in the launcher).
static InputBindings bindings = InputBindings::defaults();
static bool get_input(int controller, uint16_t* buttons, float* x, float* y) {
    *buttons = 0; *x = *y = 0.0f;
    if (controller < 0 || controller >= 4 || (controller == 0 && !controller_connected)) return false;
    SDL_GameController* p = pads[controller].load();
    const bool kb = use_keyboard && keyboard_player == controller;
    if (controller != 0 && !p && !kb && !always_four) return false;   // nothing plugged into this port
    input_poll(bindings, p, kb, buttons, x, y);
    const uint8_t* k = SDL_GetKeyboardState(nullptr);
    if (kb && k[SDL_SCANCODE_RETURN] && (k[SDL_SCANCODE_LALT] || k[SDL_SCANCODE_RALT])) *buttons &= ~N_START;  // Alt+Enter = fullscreen
    if (controller != 0) return true;                                  // scripted presses and the trace are player 1's
    const uint32_t s = scripted_buttons();
    *buttons |= (uint16_t)s;
    if (s & STICK_L) *x = -1.0f; if (s & STICK_R) *x = 1.0f;
    if (s & STICK_U) *y = 1.0f; if (s & STICK_D) *y = -1.0f;
    if (input_trace) trace_input(*buttons, *x, *y);
    return true;
}
static ultramodern::input::connected_device_info_t device_info(int controller) {
    const bool present = controller == 0 ? controller_connected
        : controller < 4 && (pads[controller].load() || (use_keyboard && keyboard_player == controller) || always_four);
    if (present) return {ultramodern::input::Device::Controller, ultramodern::input::Pak::None};
    return {ultramodern::input::Device::None, ultramodern::input::Pak::None};
}

// ---- audio: SDL queue, 16-bit stereo; ultramodern hands samples in N64 word order ----
static SDL_AudioDeviceID audio_device = 0;
static std::string audio_device_name;   // launcher Audio page; "" = default
static int master_volume = 100;         // percent, applied after the recording point (--wav keeps the game's stream)
static uint32_t audio_rate = 0;
// --wav PATH: record exactly the stereo stream handed to SDL (header sizes kept current, so the
// file stays valid when the process is killed).
static FILE* wav_file = nullptr;
static uint32_t wav_bytes = 0;
static void wav_write(const int16_t* data, size_t count) {
    if (!wav_file || !audio_rate) return;
    if (!wav_bytes) {
        const uint32_t rate = audio_rate, byte_rate = rate * 4, fmt_size = 16;
        const uint16_t pcm = 1, channels = 2, align = 4, bits = 16;
        fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, wav_file); fwrite(&fmt_size, 4, 1, wav_file);
        fwrite(&pcm, 2, 1, wav_file); fwrite(&channels, 2, 1, wav_file); fwrite(&rate, 4, 1, wav_file);
        fwrite(&byte_rate, 4, 1, wav_file); fwrite(&align, 2, 1, wav_file); fwrite(&bits, 2, 1, wav_file);
        fwrite("data\0\0\0\0", 1, 8, wav_file);
    }
    fwrite(data, sizeof(int16_t), count, wav_file);
    wav_bytes += (uint32_t)(count * sizeof(int16_t));
    const uint32_t riff = 36 + wav_bytes;
    fseek(wav_file, 4, SEEK_SET); fwrite(&riff, 4, 1, wav_file);
    fseek(wav_file, 40, SEEK_SET); fwrite(&wav_bytes, 4, 1, wav_file);
    fseek(wav_file, 0, SEEK_END); fflush(wav_file);
}
static void queue_samples(int16_t* samples, size_t count) {
    static bool logged = false;
    const unsigned long long offset = (unsigned long long)((uint8_t*)samples - rdram_base);
    if (!logged) { logged = true; fprintf(stderr, "AUDIO first buffer offset=0x%llX samples=%zu\n", offset, count); fflush(stderr); }
    if (offset + count * sizeof(int16_t) > 8 * 1024 * 1024) {
        static unsigned bad = 0;
        if (bad++ < 8) { fprintf(stderr, "AUDIO BAD buffer offset=0x%llX samples=%zu (outside 8 MiB RDRAM; skipped)\n", offset, count); fflush(stderr); }
        return;
    }
    std::vector<int16_t> swapped(count);
    for (size_t i = 0; i + 1 < count; i += 2) { swapped[i] = samples[i + 1]; swapped[i + 1] = samples[i]; }
    wav_write(swapped.data(), count);
    if (!audio_device) return;
    if (master_volume < 100) for (auto& v : swapped) v = (int16_t)(v * master_volume / 100);
    SDL_QueueAudio(audio_device, swapped.data(), (uint32_t)(count * sizeof(int16_t)));
}
static size_t frames_remaining() { return audio_device ? SDL_GetQueuedAudioSize(audio_device) / (2 * sizeof(int16_t)) : 0; }
static void set_frequency(uint32_t rate) {
    if (rate == audio_rate) return;
    if (audio_device) SDL_CloseAudioDevice(audio_device);
    SDL_AudioSpec want{}; want.freq = (int)rate; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 512;
    audio_device = SDL_OpenAudioDevice(audio_device_name.empty() ? nullptr : audio_device_name.c_str(), 0, &want, nullptr, 0);
    if (!audio_device && !audio_device_name.empty()) audio_device = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);   // device gone: default
    if (audio_device) SDL_PauseAudioDevice(audio_device, 0);
    audio_rate = rate;
    fprintf(stderr, "AUDIO frequency=%u device=%u\n", rate, audio_device);
}

// ---- AI with console semantics (same model as the verified harness AudioFifo) ----
// Two DMA slots; osAiSetNextBuffer fails (-1) when both are busy; osAiGetLength reports the
// remainder of the playing buffer only. Consumption follows real SDL playback.
#include <deque>
#include <mutex>
static std::mutex ai_mutex;
static std::deque<uint32_t> ai_slots;  // remaining bytes per queued DMA, front = playing
static void ai_update() {
    uint32_t remaining_total = audio_device ? SDL_GetQueuedAudioSize(audio_device) : 0, sum = 0;
    for (uint32_t s : ai_slots) sum += s;
    uint32_t played = sum > remaining_total ? sum - remaining_total : 0;
    while (played && !ai_slots.empty()) {
        if (played < ai_slots.front()) { ai_slots.front() -= played; break; }
        played -= ai_slots.front(); ai_slots.pop_front();
    }
}
static constexpr uint32_t VI_NTSC_CLOCK = 48681812;
extern "C" void osAiSetFrequency_recomp(uint8_t*, recomp_context* ctx) {
    const uint32_t requested = (uint32_t)ctx->r4;
    const uint32_t dac_rate = (uint32_t)((float)VI_NTSC_CLOCK / requested + 0.5f);  // libultra rounding
    const uint32_t actual = VI_NTSC_CLOCK / dac_rate;
    ultramodern::set_audio_frequency(actual);
    ctx->r2 = actual;
}
extern "C" void osAiSetNextBuffer_recomp(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t bytes = (uint32_t)ctx->r5;
    std::lock_guard<std::mutex> lock(ai_mutex);
    ai_update();
    if (ai_slots.size() >= 2) { ctx->r2 = -1; return; }  // both DMA slots busy
    if (bytes == 0 || (bytes & 7) || bytes > 0x3FFF8) {
        fprintf(stderr, "FAIL: invalid AI DMA length %u from 0x%08X\n", bytes, (uint32_t)ctx->r4); fflush(stderr); std::abort();
    }
    ultramodern::queue_audio_buffer(rdram, (int32_t)ctx->r4, bytes);
    ai_slots.push_back(bytes);
    ctx->r2 = 0;
}
extern "C" void osAiGetLength_recomp(uint8_t*, recomp_context* ctx) {
    std::lock_guard<std::mutex> lock(ai_mutex);
    ai_update();
    ctx->r2 = ai_slots.empty() ? 0 : (ai_slots.front() & ~7u);
}
extern "C" void osAiGetStatus_recomp(uint8_t*, recomp_context* ctx) {
    std::lock_guard<std::mutex> lock(ai_mutex);
    ai_update();
    ctx->r2 = ai_slots.size() >= 2 ? 0x80000000 : 0;  // AI_STATUS_FIFO_FULL
}

// ---- RSP: the game's original audio microcode, recompiled by RSPRecomp ----
static RspUcodeFunc* get_rsp_microcode(const OSTask* task) {
    if (task->t.type == 2 && (uint32_t)task->t.ucode == 0x80079580 && (uint32_t)task->t.ucode_data == 0x800A35C0) return hangtime_game()->audio_microcode;
    return nullptr;
}

#ifdef _WIN32
static void message_box(const char* msg) { fprintf(stderr, "ERROR: %s\n", msg); MessageBoxA(nullptr, msg, "NBA Hangtime", MB_OK | MB_ICONERROR); }
static void warning_box(const std::string& msg) { MessageBoxA(nullptr, msg.c_str(), "NBA Hangtime", MB_OK | MB_ICONWARNING); }
#else
static void message_box(const char* msg) { fprintf(stderr, "ERROR: %s\n", msg); SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "NBA Hangtime", msg, nullptr); }
static void warning_box(const std::string& msg) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "NBA Hangtime", msg.c_str(), nullptr); }
#endif

#ifdef _WIN32
// Diagnostic: module-relative call stacks of every thread (resolve with full_runtime/resolve_stacks.py).
#include <tlhelp32.h>
static void sample_all_threads() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te{sizeof(te)};
    const DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
        HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!t || SuspendThread(t) == (DWORD)-1) { if (t) CloseHandle(t); continue; }
        CONTEXT ctx{}; ctx.ContextFlags = CONTEXT_FULL; GetThreadContext(t, &ctx);
        fprintf(stderr, "THREAD %lu\n", te.th32ThreadID);
        for (int i = 0; i < 64 && ctx.Rip; ++i) {
            DWORD64 image = 0; char name[MAX_PATH] = "?";
            auto entry = RtlLookupFunctionEntry(ctx.Rip, &image, nullptr);
            if (image) GetModuleFileNameA((HMODULE)image, name, MAX_PATH);
            const char* base = strrchr(name, '\\') ? strrchr(name, '\\') + 1 : name;
            fprintf(stderr, "  STACK %02d %s+0x%llX\n", i, base, (unsigned long long)(ctx.Rip - image));
            if (entry) { PVOID h = nullptr; DWORD64 est = 0; RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, ctx.Rip, entry, &ctx, &h, &est, nullptr); }
            else { ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp); ctx.Rsp += 8; }
        }
        ResumeThread(t); CloseHandle(t);
    }
    CloseHandle(snap);
    fflush(stderr);
}

// Crash report: exception code, address and module-relative stack of the faulting thread.
static void print_stack(CONTEXT ctx) {
    for (int i = 0; i < 64 && ctx.Rip; ++i) {
        DWORD64 image = 0; char name[MAX_PATH] = "?";
        auto entry = RtlLookupFunctionEntry(ctx.Rip, &image, nullptr);
        if (image) GetModuleFileNameA((HMODULE)image, name, MAX_PATH);
        const char* base = strrchr(name, '\\') ? strrchr(name, '\\') + 1 : name;
        fprintf(stderr, "  STACK %02d %s+0x%llX\n", i, base, (unsigned long long)(ctx.Rip - image));
        if (entry) { PVOID h = nullptr; DWORD64 est = 0; RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, ctx.Rip, entry, &ctx, &h, &est, nullptr); }
        else { ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp); ctx.Rsp += 8; }
    }
}
static LONG WINAPI crash_filter(EXCEPTION_POINTERS* e) {
    fprintf(stderr, "CRASH code=0x%08lX address=%p thread=%lu", e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress, GetCurrentThreadId());
    if (e->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
        fprintf(stderr, " %s 0x%llX", e->ExceptionRecord->ExceptionInformation[0] ? "write" : "read", (unsigned long long)e->ExceptionRecord->ExceptionInformation[1]);
    fprintf(stderr, "\n");
    print_stack(*e->ContextRecord);
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}
static void install_crash_handler() { SetUnhandledExceptionFilter(crash_filter); }
#else
// Crash report (POSIX): signal, fault address and the faulting thread's stack as
// module(+offset) frames from glibc backtrace (written straight to fd 2, no allocation), then
// the default action. FAIL lines abort, so their stack is reported too.
static void crash_signal(int sig, siginfo_t* info, void*) {
    char line[128];
    const int n = snprintf(line, sizeof(line), "CRASH signal=%d address=%p\n", sig, info ? info->si_addr : nullptr);
    if (n > 0) { ssize_t w = write(STDERR_FILENO, line, (size_t)n); (void)w; }
    void* frames[64];
    backtrace_symbols_fd(frames, backtrace(frames, 64), STDERR_FILENO);
    signal(sig, SIG_DFL);
    raise(sig);
}
static void install_crash_handler() {
    void* warm[1]; backtrace(warm, 1);   // load libgcc's unwinder now, not inside the handler
    struct sigaction sa{};
    sa.sa_sigaction = crash_signal; sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) sigaction(sig, &sa, nullptr);
}
#endif

// ROM: command line, else the last verified ROM (hangtime.ini), else a ROM beside the executable,
// else the development checkout's ..\rom, else a file picker. Every candidate must be the exact
// NBA Hangtime (USA) image (SHA-256); .v64/.n64 byte orders are converted in memory.
static bool find_rom(const std::filesystem::path& arg, LauncherSettings& settings, bool interactive, std::vector<uint8_t>& rom) {
    std::string error, errors;
    auto attempt = [&](const std::filesystem::path& p) {
        if (p.empty()) return false;
        if (launcher_load_rom(p, rom, error)) { settings.rom = std::filesystem::absolute(p); return true; }
        if (std::filesystem::exists(p)) errors += error + "\n";
        return false;
    };
    if (!arg.empty()) {
        if (attempt(arg)) return true;
        message_box(errors.empty() ? ("ROM not found: " + launcher_utf8(arg)).c_str() : errors.c_str());
        return false;
    }
    const auto exe = launcher_exe_dir();
    for (const auto& p : {settings.rom, exe / "NBA Hangtime (USA).z64", exe / "NBA Hangtime (USA).n64", exe / "NBA Hangtime (USA).v64",
                          std::filesystem::path("..") / "rom" / "NBA Hangtime (USA).z64"})
        if (attempt(p)) return true;
    if (!interactive) { message_box(("No NBA Hangtime (USA) ROM found.\n" + errors).c_str()); return false; }
    for (;;) {
        const auto picked = launcher_pick_rom();
        if (!picked) return false;
        error.clear();
        if (launcher_load_rom(*picked, rom, error)) { settings.rom = *picked; return true; }
        warning_box(error + "\n\nChoose your NBA Hangtime (USA) ROM (.z64, .n64 or .v64).");
    }
}

// ---- test drivers for the real host input paths (main thread) ----
// --key-press VI:KEY / --key-poll POLL:KEY post WM_KEYDOWN/WM_KEYUP to the game window, so the key
// goes through SDL's own window procedure and keyboard state exactly like a physical key.
// --virtual-pad attaches an SDL virtual game controller; --pad-press VI:CONTROL / --pad-poll
// POLL:CONTROL hold one of its buttons or axes (SDL GameController mapping, then get_input).
// --virtual-pad [N] attaches N (default 1) virtual pads, which become players 1..N in order;
// --pad-press TIME:CONTROL@K drives virtual pad K (default 1).
struct HostPress { double at; Clock clock; int mode; int entry; int kind; int code; bool down; int pad; };  // kind 0 key, 1 pad button, 2 pad axis +, 3 pad axis -
static std::vector<HostPress> host_presses;
static std::vector<SDL_Joystick*> virtual_pads;
static bool parse_host_press(const std::string& spec, Clock clock, bool pad_control) {
    const auto colon = spec.find(':');
    if (colon == std::string::npos) return false;
    std::string n = spec.substr(colon + 1);
    HostPress p{0, clock, -1, 1, 0, -1, false, 0};
    if (const auto at = n.find('@'); pad_control && at != std::string::npos) { p.pad = atoi(n.c_str() + at + 1) - 1; n = n.substr(0, at); }
    if (p.pad < 0 || p.pad > 3) return false;
    if (!parse_time(spec.substr(0, colon), clock, p.at, p.clock, p.mode, p.entry)) return false;
    if (!pad_control) {
#ifdef _WIN32
        static const std::pair<const char*, int> keys[] = {{"RETURN",VK_RETURN},{"LEFT",VK_LEFT},{"RIGHT",VK_RIGHT},{"UP",VK_UP},{"DOWN",VK_DOWN},
            {"LSHIFT",VK_LSHIFT},{"SPACE",VK_SPACE},{"F10",VK_F10},{"F11",VK_F11}};
        for (auto& [k, v] : keys) if (n == k) p.code = v;
        if (n.size() == 1 && isalnum((unsigned char)n[0])) p.code = toupper((unsigned char)n[0]);
#else
        return false;   // --key-press posts Win32 window messages; no X11/Wayland driver yet
#endif
    } else {
        static const char* buttons[] = {"A","B","X","Y","BACK","GUIDE","START","LS","RS","LB","RB","DU","DD","DL","DR"};
        static const char* axes[] = {"LX","LY","RX","RY","LT","RT"};
        for (int i = 0; i < 15; ++i) if (n == buttons[i]) { p.kind = 1; p.code = i; }
        for (int i = 0; i < 6; ++i)
            if (n.rfind(axes[i], 0) == 0 && (n.size() == 2 || n.size() == 3)) { p.kind = n.size() == 3 && n[2] == '-' ? 3 : 2; p.code = i; }
    }
    if (p.code < 0) return false;
    host_presses.push_back(p);
    return true;
}
// --dump-ram TIME:PATH (diagnostic): the 8 MiB RDRAM as the runtime holds it (32-bit words in host
// order) at a VI count or gated time. Game content: write under captures/ (local only).
struct RamDump { double at; Clock clock; int mode; int entry; std::string path; bool done; };
static std::vector<RamDump> ram_dumps;
// --dump-dl TIME:PATH (diagnostic): the next display list after TIME, decoded to text by
// dl_dump.cpp on the renderer thread (rt64_context.cpp send_dl).
static std::vector<RamDump> dl_dumps;
static std::mutex dl_dump_mutex;
std::string hangtime_dl_dump_due() {
    std::lock_guard<std::mutex> lock(dl_dump_mutex);
    for (auto& d : dl_dumps)
        if (!d.done && clock_now(d.clock, d.mode, d.entry) >= d.at) { d.done = true; return d.path; }
    return {};
}
static void drive_ram_dumps() {
    for (auto& d : ram_dumps) {
        if (d.done || clock_now(d.clock, d.mode, d.entry) < d.at) continue;
        d.done = true;
        if (FILE* f = fopen(d.path.c_str(), "wb")) { fwrite(rdram_base, 1, 8 * 1024 * 1024, f); fclose(f); }
        fprintf(stderr, "RAMDUMP poll=%u vi=%u %s\n", poll_count.load(), vi_count.load(), d.path.c_str());
    }
}
#ifdef _WIN32
using HostWindow = HWND;
#else
using HostWindow = SDL_Window*;
#endif
static void drive_host_presses([[maybe_unused]] HostWindow hwnd) {
    for (auto& p : host_presses) {
        const double now = clock_now(p.clock, p.mode, p.entry);
        const bool want = now >= p.at && now < p.at + 12;
        if (want == p.down) continue;
        p.down = want;
        if (p.kind == 0) {
#ifdef _WIN32
            const bool ext = p.code == VK_LEFT || p.code == VK_RIGHT || p.code == VK_UP || p.code == VK_DOWN;
            LPARAM l = 1 | (LPARAM)MapVirtualKeyA(p.code, MAPVK_VK_TO_VSC) << 16 | (LPARAM)(ext ? 1 : 0) << 24;
            if (!want) l |= (LPARAM)3 << 30;
            PostMessageA(hwnd, want ? WM_KEYDOWN : WM_KEYUP, p.code, l);
#endif
        } else if (p.pad < (int)virtual_pads.size()) {
            SDL_Joystick* vp = virtual_pads[p.pad];
            if (p.kind == 1) SDL_JoystickSetVirtualButton(vp, p.code, want ? SDL_PRESSED : SDL_RELEASED);
            else SDL_JoystickSetVirtualAxis(vp, p.code, !want ? 0 : p.kind == 3 ? -32768 : 32767);
        }
    }
}

int main(int argc, char** argv) {
    install_crash_handler();
    std::filesystem::path rom_path;
    int sample_after_s = 0;
    int want_virtual_pads = 0;
    int keyboard_player_arg = 0, controllers_arg = 0;   // --keyboard-player N, --controllers 4 (tests; else settings)
    std::string pad_order_arg;                           // --pad-order 1324
    bool background = false, focus = false, console = false, fullscreen_flag = false, windowed_flag = false;
    bool force_launcher = false, no_launcher = false;
    std::set<std::string> test_cheats;
    std::string render_gpu_arg;   // --render-gpu NAME (tests); else the player setting
    for (int i = 1; i < argc; ++i)
        if (!strcmp(argv[i], "--version")) {   // CI smoke test: the runtime starts (no ROM, no window)
            printf("hangtime runtime %s (game module API %u)\n", HANGTIME_VERSION, (unsigned)HANGTIME_GAME_API_VERSION);
            fflush(stdout);
            return 0;
        }
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--no-controller")) controller_connected = false;
        else if (!strcmp(argv[i], "--sample") && i + 1 < argc) sample_after_s = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav_file = fopen(argv[++i], "wb");
        else if (!strcmp(argv[i], "--input-trace")) input_trace = true;
        else if (!strcmp(argv[i], "--perf") && i + 1 < argc) perf_open(argv[++i]);   // frame-timing event log (perf_report.py)
        else if (!strcmp(argv[i], "--profile-gfx") && i + 1 < argc) perf_profile_open(argv[++i]);   // gfx-thread stack samples (profile_report.py)
        else if (!strcmp(argv[i], "--virtual-pad")) {   // optional count (players 1..N)
            want_virtual_pads = 1;
            if (i + 1 < argc && isdigit((unsigned char)argv[i + 1][0])) want_virtual_pads = std::clamp(atoi(argv[++i]), 1, 4);
        }
        else if (!strcmp(argv[i], "--keyboard-player") && i + 1 < argc) keyboard_player_arg = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--controllers") && i + 1 < argc) controllers_arg = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pad-order") && i + 1 < argc) pad_order_arg = argv[++i];
        else if (!strcmp(argv[i], "--no-keyboard")) use_keyboard = false;
        else if (!strcmp(argv[i], "--cheat") && i + 1 < argc) test_cheats.insert(argv[++i]);   // tests: enable a cheat (settings ignored)
        else if (!strcmp(argv[i], "--launcher")) force_launcher = true;     // show the launcher even if skipped in settings
        else if (!strcmp(argv[i], "--no-launcher")) no_launcher = true;
        else if (!strcmp(argv[i], "--console")) console = true;
        // Experimental presentation modes (default = original 4:3 at the VI rate).
        else if (!strcmp(argv[i], "--aspect") && i + 1 < argc) {
            const std::string a = argv[++i];
            if (a == "expand") hangtime_video.aspect = 1;
            else if (a != "original") {
                const auto colon = a.find(':');
                hangtime_video.aspect = 2;
                hangtime_video.aspect_ratio = colon == std::string::npos ? atof(a.c_str()) : atof(a.c_str()) / atof(a.c_str() + colon + 1);
            }
        }
        else if (!strcmp(argv[i], "--refresh") && i + 1 < argc) {
            const std::string r = argv[++i];
            if (r == "display") hangtime_video.refresh = 1;
            else if (r != "original") { hangtime_video.refresh = 2; hangtime_video.refresh_hz = atoi(r.c_str()); }
        }
        else if (!strcmp(argv[i], "--widescreen")) {   // true widescreen live play; optional ratio (default 16:9)
            hangtime_video.widescreen = true; hangtime_video.aspect_ratio = 16.0 / 9.0;
            if (i + 1 < argc && isdigit((unsigned char)argv[i + 1][0])) {
                const std::string r = argv[++i]; const auto colon = r.find(':');
                hangtime_video.aspect_ratio = colon == std::string::npos ? atof(r.c_str()) : atof(r.c_str()) / atof(r.c_str() + colon + 1);
            }
        }
        else if (!strcmp(argv[i], "--msaa") && i + 1 < argc) hangtime_video.msaa = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--render-gpu") && i + 1 < argc) render_gpu_arg = argv[++i];
        else if (!strcmp(argv[i], "--present-blocking")) hangtime_video.present_blocking = true;
        else if (!strcmp(argv[i], "--graphics-api") && i + 1 < argc) hangtime_video.vulkan = !strcmp(argv[++i], "vulkan");
        else if (!strcmp(argv[i], "--resolution") && i + 1 < argc) hangtime_video.resolution = atof(argv[++i]);
        else if (!strcmp(argv[i], "--fullscreen")) fullscreen_flag = true;
        else if (!strcmp(argv[i], "--windowed")) windowed_flag = true;
        else if ((!strcmp(argv[i], "--dump-ram") || !strcmp(argv[i], "--dump-dl")) && i + 1 < argc) {
            const bool dl = !strcmp(argv[i], "--dump-dl");
            const std::string spec = argv[++i]; const auto colon = spec.find(':', spec[0] == 'm' ? 1 : 0);
            RamDump d{0, Clock::Vi, -1, 1, colon == std::string::npos ? "" : spec.substr(colon + 1), false};
            if (d.path.empty() || !parse_time(spec.substr(0, colon), Clock::Vi, d.at, d.clock, d.mode, d.entry)) { message_box("--dump-ram/--dump-dl TIME:PATH"); return 1; }
            (dl ? dl_dumps : ram_dumps).push_back(d);
        }
        else if (!strcmp(argv[i], "--background")) background = true;  // tests: open without taking keyboard focus
        else if (!strcmp(argv[i], "--focus")) focus = true;   // with --background: test settings, but the window takes focus (performance runs)
        else if ((!strcmp(argv[i], "--press") || !strcmp(argv[i], "--press-frame") || !strcmp(argv[i], "--press-poll")) && i + 1 < argc) {
            const Clock clock = !strcmp(argv[i], "--press") ? Clock::Wall : !strcmp(argv[i], "--press-frame") ? Clock::Vi : Clock::Poll;
            for (const std::string& spec : expand_repeats(argv[++i])) {
                const auto colon = spec.find(':');
                const uint32_t mask = colon == std::string::npos ? 0 : button_mask(spec.substr(colon + 1));
                if (!mask) { message_box("--press SECONDS:BUTTON / --press-frame VI:BUTTON / --press-poll POLL:BUTTON (A,B,Z,START,DU,DD,DL,DR,L,R,CU,CD,CL,CR,SL,SR,SU,SD)"); return 1; }
                ScriptedPress p{0, mask, clock, -1, 1};
                if (!parse_time(spec.substr(0, colon), clock, p.at, p.clock, p.mode, p.entry)) { message_box("bad press time"); return 1; }
                scripted.push_back(p);
            }
        }
        else if ((!strcmp(argv[i], "--key-press") || !strcmp(argv[i], "--key-poll") || !strcmp(argv[i], "--pad-press") || !strcmp(argv[i], "--pad-poll")) && i + 1 < argc) {
            const bool poll = strstr(argv[i], "poll") != nullptr, pad_control = strstr(argv[i], "pad") != nullptr;
            for (const std::string& spec : expand_repeats(argv[++i]))
                if (!parse_host_press(spec, poll ? Clock::Poll : Clock::Vi, pad_control)) {
                    message_box("--key-press VI:KEY (Windows only; letter, digit, RETURN, LEFT, RIGHT, UP, DOWN, LSHIFT, SPACE) / --pad-press VI:CONTROL[@PAD] (A,B,X,Y,START,BACK,LB,RB,DU,DD,DL,DR, LX+ LX- LY+ LY- RX+ RX- RY+ RY- LT RT; PAD 1-4 with --virtual-pad N)");
                    return 1;
                }
        }
        else rom_path = argv[i];
    }
    launcher_setup_log(console);
#ifdef _WIN32
    // Opt out of Windows power throttling (EcoQoS) and coarse timers: an unfocused window was
    // otherwise throttled to ~53 presented frames/s (game logic slower than real time).
    PROCESS_POWER_THROTTLING_STATE throttling{PROCESS_POWER_THROTTLING_CURRENT_VERSION,
        PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION, 0};
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &throttling, sizeof(throttling));
    timeBeginPeriod(1);
    if (sample_after_s > 0) std::thread([sample_after_s] { Sleep(sample_after_s * 1000); sample_all_threads(); }).detach();
#else
    if (sample_after_s > 0) fprintf(stderr, "--sample is Windows-only (ignored)\n");
#endif
    config_path = launcher_data_dir() / "hangtime_data";
    // Test runs (--background) neither read nor write the player's settings and skip the launcher.
    LauncherSettings settings = background ? LauncherSettings{} : launcher_load_settings();
    SDL_SetMainReady();
    if (background) {  // unfocused test window: SDL would otherwise drop joystick state changes
        if (!focus) SDL_SetHint(SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN, "1");
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    }
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) { message_box(SDL_GetError()); return 1; }
    if (!background && !no_launcher && (force_launcher || !settings.skip_launcher)) {
        if (!rom_path.empty()) settings.rom = std::filesystem::absolute(rom_path);   // e.g. a ROM dropped on the exe
        if (run_launcher(settings) == LauncherResult::Quit) { SDL_Quit(); return 0; }
        rom_path.clear();
    }
    std::vector<uint8_t> rom;
    if (!find_rom(rom_path, settings, !background, rom)) return 1;
    fprintf(stderr, "ROM %s (SHA-256 verified)\n", launcher_utf8(settings.rom).c_str());
    {   // The recompiled game (game_module.hpp): never shipped; on first run it is built here from
        // the player's ROM (module_builder.hpp) when the package's setup kit is present.
        std::string error;
        if (!hangtime_game_load(error)) {
            const ModuleBuildPaths paths = module_build_paths();
            if (!module_build_available(paths)) { message_box(error.c_str()); return 1; }
            fprintf(stderr, "GAME MODULE building from the ROM into %s\n", launcher_utf8(paths.output).c_str());
            const auto t0 = std::chrono::steady_clock::now();
            bool built;
            if (background) {   // tests: no window, progress in the log
                ModuleBuildProgress progress;
                std::thread worker([&] { module_build(paths, settings.rom, progress); });
                std::string last;
                while (!progress.done) {
                    if (const std::string step = progress.get_step(); step != last) { last = step; fprintf(stderr, "GAME MODULE %3.0f%% %s\n", progress.fraction * 100.0, step.c_str()); }
                    SDL_Delay(100);
                }
                worker.join();
                built = progress.ok;
                if (!built) error = progress.error;
            } else built = run_module_setup(settings.rom, error);
            fprintf(stderr, "GAME MODULE build %s in %.1f s\n", built ? "ok" : "FAILED", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
            if (!built || !hangtime_game_load(error)) { message_box(error.c_str()); return 1; }
        }
    }
    if (!background) {   // player settings (launcher pages); test runs keep the verified defaults
        bindings.load(settings.extra);
        master_volume = settings.master_volume;
        audio_device_name = settings.audio_device;
        if (hangtime_video.resolution == 0 && settings.resolution > 0) hangtime_video.resolution = settings.resolution / 240.0;
        if (hangtime_video.msaa == 0) hangtime_video.msaa = settings.msaa;
        cheats_configure(settings.cheats);
        if (auto it = settings.extra.find("keyboard_player"); it != settings.extra.end()) keyboard_player = std::clamp(atoi(it->second.c_str()), 1, 4) - 1;
        if (auto it = settings.extra.find("controllers"); it != settings.extra.end()) always_four = it->second == "4";
        if (auto it = settings.extra.find("pad_order"); it != settings.extra.end()) set_pad_order(it->second);
    }
    if (!pad_order_arg.empty()) set_pad_order(pad_order_arg);
    if (keyboard_player_arg) keyboard_player = std::clamp(keyboard_player_arg, 1, 4) - 1;
    if (controllers_arg) always_four = controllers_arg == 4;
    if (!test_cheats.empty()) cheats_configure(test_cheats);
    {   // per-system render GPU (render_gpu.hpp), before the renderer loads the Vulkan loader
        std::string gpu_log;
        if (render_gpu_apply(!render_gpu_arg.empty() ? render_gpu_arg : background ? std::string() : settings.render_gpu, gpu_log)) hangtime_video.vulkan = true;
        fprintf(stderr, "RENDER_GPU %s\n", gpu_log.c_str());
    }
    int window_mode = background ? 0 : settings.window_mode;
    if (fullscreen_flag && window_mode == 0) window_mode = 1;
    if (windowed_flag) window_mode = 0;
    bool fullscreen = window_mode != 0;
    if (settings.widescreen && !background) hangtime_video.widescreen = true;
    if (hangtime_video.widescreen) settings.widescreen = true;
    hangtime_ws_configure(hangtime_video.aspect_ratio);   // ratio of the wide view; on/off below and with F10
    hangtime_ws_set_enabled(hangtime_video.widescreen);
    if (hangtime_video.widescreen) fprintf(stderr, "WIDESCREEN ratio=%.4f\n", hangtime_video.aspect_ratio);
    if (!background) launcher_save_settings(settings);
    recomp::set_rom_contents(std::move(rom));

    // Real pixels on scaled displays (no blurry DWM upscale); the initial 4:3 window follows the
    // display scale and stays within the usable desktop.
    float ddpi = 96.0f;
    if (SDL_GetDisplayDPI(0, &ddpi, nullptr, nullptr) != 0 || ddpi <= 0) ddpi = 96.0f;
    SDL_Rect usable{0, 0, 1920, 1080};
    SDL_GetDisplayUsableBounds(0, &usable);
    const double win_ratio = (hangtime_video.aspect == 2 || hangtime_video.widescreen) ? hangtime_video.aspect_ratio : hangtime_video.aspect == 1 ? 16.0 / 9.0 : 4.0 / 3.0;
    int win_h = std::min(hangtime_video.resolution > 0 ? (int)(hangtime_video.resolution * 240) : (int)(720 * ddpi / 96.0f), usable.h * 9 / 10), win_w = (int)(win_h * win_ratio);
    if (win_w > usable.w * 95 / 100) { win_w = usable.w * 95 / 100; win_h = (int)(win_w / win_ratio); }
#ifdef _WIN32
    const uint32_t window_api = 0;                   // RT64 D3D12 renders into the HWND
#else
    const uint32_t window_api = SDL_WINDOW_VULKAN;   // RT64 Vulkan surface through SDL (RT64_SDL_WINDOW_VULKAN)
#endif
    window = SDL_CreateWindow("NBA Hangtime", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w, win_h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | window_api);
    if (window) {
        SDL_DisplayMode dm{}; SDL_GetDesktopDisplayMode(0, &dm);
        fprintf(stderr, "WINDOW %dx%d display %dx%d dpi=%.0f\n", win_w, win_h, dm.w, dm.h, ddpi);
    }
    if (!window) { message_box(SDL_GetError()); return 1; }
    auto apply_fullscreen = [&] {   // borderless = desktop-sized fullscreen window; exclusive = display mode switch
        SDL_SetWindowFullscreen(window, !fullscreen ? 0 : window_mode == 2 ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_FULLSCREEN_DESKTOP);
        SDL_ShowCursor(fullscreen ? SDL_DISABLE : SDL_ENABLE);
    };
    if (fullscreen) apply_fullscreen();
    if (focus) SDL_RaiseWindow(window);
    for (int v = 0; v < want_virtual_pads; ++v) {   // tests: virtual pads take players 1..N first
        const int index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
        SDL_Joystick* vp = index >= 0 ? SDL_JoystickOpen(index) : nullptr;
        fprintf(stderr, "VIRTUAL PAD %d index=%d %s\n", v + 1, index, vp ? "attached" : SDL_GetError());
        if (vp) { virtual_pads.push_back(vp); pad_assign(index); }
    }
    for (int i = 0; i < SDL_NumJoysticks(); ++i) pad_assign(i);   // then physical pads in SDL order
#ifdef _WIN32
    SDL_SysWMinfo wm; SDL_VERSION(&wm.version); SDL_GetWindowWMInfo(window, &wm);
    const HostWindow host_window = wm.info.win.window;
    ultramodern::renderer::WindowHandle window_handle{host_window, GetCurrentThreadId()};
#else
    const HostWindow host_window = window;
    ultramodern::renderer::WindowHandle window_handle = window;
#endif

    hangtime_game()->rsp_init();
    hangtime_game()->rsp_set_callbacks({get_rsp_microcode});
    static const ultramodern::rsp::callbacks_t rsp_callbacks{.init = hangtime_game()->rsp_init, .run_task = hangtime_game()->rsp_run_task};
    static const ultramodern::renderer::callbacks_t renderer_callbacks{.create_render_context = hangtime_create_rt64_context};
    static const ultramodern::audio_callbacks_t audio_callbacks{.queue_samples = queue_samples, .get_frames_remaining = frames_remaining, .set_frequency = set_frequency};
    static const ultramodern::input::callbacks_t input_callbacks{.poll_input = on_poll, .get_input = get_input, .set_rumble = [](int, bool) {}, .get_connected_device_info = device_info};
    static const ultramodern::gfx_callbacks_t gfx_callbacks{};
    static const ultramodern::events::callbacks_t events_callbacks{.vi_callback = on_vi};
    static const ultramodern::error_handling::callbacks_t error_callbacks{.message_box = message_box};
    static const ultramodern::threads::callbacks_t threads_callbacks{};
    ultramodern::set_callbacks(rsp_callbacks, renderer_callbacks, audio_callbacks, input_callbacks, gfx_callbacks, events_callbacks, error_callbacks, threads_callbacks);

    // RDRAM: reserve librecomp's allocation size, commit the accessible region.
#ifdef _WIN32
    uint8_t* rdram = (uint8_t*)VirtualAlloc(nullptr, recomp::allocation_size, MEM_RESERVE, PAGE_NOACCESS);
    if (!rdram || !VirtualAlloc(rdram, recomp::mem_size, MEM_COMMIT, PAGE_READWRITE)) { message_box("Failed to allocate RDRAM"); return 1; }
#else   // same as librecomp's POSIX path: reserve inaccessible, then open the accessible region
    void* reserve = mmap(nullptr, recomp::allocation_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    uint8_t* rdram = reserve == MAP_FAILED ? nullptr : (uint8_t*)reserve;
    if (!rdram || mprotect(rdram, recomp::mem_size, PROT_READ | PROT_WRITE) != 0) { message_box("Failed to allocate RDRAM"); return 1; }
#endif
    rdram_base = rdram;
    hangtime_register_functions();

    std::thread game_thread([rdram, window_handle] {
        ultramodern::set_native_thread_name("Game Start Thread");
        ultramodern::preinit(rdram, window_handle);
        recomp_context ctx{};
        ctx.f_odd = &ctx.f0.u32h; ctx.mips3_float_mode = false;
        ctx.status_reg = 0xFF01;
        // IPL3: 1 MiB from ROM 0x1000 to the boot entry, then its low-memory variables.
        recomp::do_rom_read(rdram, (gpr)(int32_t)0x80000400, 0x10001000, 0x100000);
        MEM_W(0x80000300 - 0x80000000, (gpr)(int32_t)0x80000000) = 1;            // osTvType: NTSC
        MEM_W(0x80000308 - 0x80000000, (gpr)(int32_t)0x80000000) = (int32_t)0xB0000000;  // osRomBase
        MEM_W(0x8000030C - 0x80000000, (gpr)(int32_t)0x80000000) = 0;            // osResetType: cold
        MEM_W(0x80000318 - 0x80000000, (gpr)(int32_t)0x80000000) = 8 * 1024 * 1024;  // osMemSize
        ultramodern::init_saving(rdram);
        game_started = true;
        try { hangtime_game()->boot_entry(rdram, &ctx); } catch (ultramodern::thread_terminated&) {}
    });

    while (!quit_requested) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit_requested = true;
            if (e.type == SDL_KEYDOWN && !e.key.repeat && !background && (e.key.keysym.sym == SDLK_F11 ||
                    (e.key.keysym.sym == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT)))) {  // F11 / Alt+Enter
                fullscreen = !fullscreen;
                if (fullscreen && window_mode == 0) window_mode = 1;
                apply_fullscreen();
                settings.window_mode = fullscreen ? window_mode : 0; launcher_save_settings(settings);
            }
            if (e.type == SDL_KEYDOWN && !e.key.repeat && !background && e.key.keysym.sym == SDLK_F1) {   // F1: back to the launcher
                launcher_save_settings(settings);
                if (launcher_restart({"--launcher"})) quit_requested = true;
            }
            if (e.type == SDL_KEYDOWN && !e.key.repeat && !background && e.key.keysym.sym == SDLK_F10) {   // F10: 16:9 widescreen on/off
                settings.widescreen = !settings.widescreen; launcher_save_settings(settings);
                hangtime_ws_toggle_request = true;
                if (!fullscreen) {   // windowed: the window follows the presentation's ratio
                    int w = 0, h = 0; SDL_GetWindowSize(window, &w, &h);
                    SDL_SetWindowSize(window, (int)(h * (settings.widescreen ? hangtime_video.aspect_ratio : 4.0 / 3.0)), h);
                }
            }
            if (e.type == SDL_CONTROLLERDEVICEADDED) pad_assign(e.cdevice.which);      // first free player slot
            if (e.type == SDL_CONTROLLERDEVICEREMOVED) pad_release(e.cdevice.which);   // unplugged: free its slot
        }
        drive_host_presses(host_window);
        drive_ram_dumps();
        SDL_Delay(1);
    }
    ultramodern::quit();
    game_thread.detach();
    // Game threads block forever by design; exit now.
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(), 0);
#else
    fflush(nullptr);
    _exit(0);
#endif
}
