// RT64 renderer for the whole-program build (ultramodern::renderer::RendererContext).
// Wiring matches the validated in-process adapter (rt64_validation/live.cpp); here RT64 renders
// into the game window and reads VI state from ultramodern's per-retrace registers.
#include <array>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include "hle/rt64_application.h"
#include "ultramodern/renderer_context.hpp"
#include "ultramodern/ultramodern.hpp"
#include "librecomp/game.hpp"
#include "video_options.hpp"
#include "perf.hpp"
#include "dl_guard.hpp"
#include <thread>
#include "hle/rt64_workload_queue.h"
#include "hle/rt64_present_queue.h"

uint32_t hangtime_poll_count();  // main.cpp: game controller polls (osContStartReadData)
std::string hangtime_dl_dump_due();  // main.cpp: --dump-dl schedule
void hangtime_dump_dl(const uint8_t* rdram, uint32_t start, FILE* out);  // dl_dump.cpp
uint32_t hangtime_ws_rewrite(uint8_t* rdram, uint32_t dl);  // widescreen.cpp (identity unless --widescreen)
void hangtime_ws_trace_write(const char* path);            // widescreen.cpp (HANGTIME_WS_TRACE=1)
void hangtime_ws_stats(int* camx, int* clamp, int* need);  // widescreen.cpp
unsigned hangtime_ws_unculled(unsigned* sprites, unsigned* shifted);   // widescreen.cpp: player draws kept by the wide view
bool hangtime_ws_is_enabled();
void hangtime_ws_set_enabled(bool on);
namespace {
uint32_t mi_intr = 0;
void check_interrupts() { mi_intr = 0; }  // ultramodern signals SP/DP completion itself

class RT64Context final : public ultramodern::renderer::RendererContext {
    std::array<uint8_t, 0x1000> dmem{}, imem{};
    std::array<uint8_t, 0x40> header{};
    uint32_t dpc[8]{};
    std::unique_ptr<RT64::Application> app;
public:
    RT64Context(uint8_t* rdram, ultramodern::renderer::WindowHandle window) {
        auto rom = recomp::get_rom();
        for (size_t i = 0; i < header.size() && i < rom.size(); ++i) header[i] = rom[i];
        auto* vi = ultramodern::renderer::get_vi_regs();
        RT64::Application::Core core{};
#ifdef _WIN32
        core.window = window.window;
        const uint32_t window_thread = window.thread_id;
        const auto api = hangtime_video.vulkan ? RT64::UserConfiguration::GraphicsAPI::Vulkan : RT64::UserConfiguration::GraphicsAPI::D3D12;
        const auto chosen = hangtime_video.vulkan ? ultramodern::renderer::GraphicsApi::Vulkan : ultramodern::renderer::GraphicsApi::D3D12;
#else   // RT64 built with RT64_SDL_WINDOW_VULKAN: the SDL window itself (created with SDL_WINDOW_VULKAN)
        core.window = window;
        const uint32_t window_thread = 0;
        constexpr auto api = RT64::UserConfiguration::GraphicsAPI::Vulkan;
        constexpr auto chosen = ultramodern::renderer::GraphicsApi::Vulkan;
#endif
        core.HEADER = header.data(); core.RDRAM = rdram; core.DMEM = dmem.data(); core.IMEM = imem.data();
        core.MI_INTR_REG = &mi_intr; core.checkInterrupts = check_interrupts;
        core.DPC_START_REG = &dpc[0]; core.DPC_END_REG = &dpc[1]; core.DPC_CURRENT_REG = &dpc[2]; core.DPC_STATUS_REG = &dpc[3];
        core.DPC_CLOCK_REG = &dpc[4]; core.DPC_BUFBUSY_REG = &dpc[5]; core.DPC_PIPEBUSY_REG = &dpc[6]; core.DPC_TMEM_REG = &dpc[7];
        core.VI_STATUS_REG = &vi->VI_STATUS_REG; core.VI_ORIGIN_REG = &vi->VI_ORIGIN_REG; core.VI_WIDTH_REG = &vi->VI_WIDTH_REG;
        core.VI_INTR_REG = &vi->VI_INTR_REG; core.VI_V_CURRENT_LINE_REG = &vi->VI_V_CURRENT_LINE_REG; core.VI_TIMING_REG = &vi->VI_TIMING_REG;
        core.VI_V_SYNC_REG = &vi->VI_V_SYNC_REG; core.VI_H_SYNC_REG = &vi->VI_H_SYNC_REG; core.VI_LEAP_REG = &vi->VI_LEAP_REG;
        core.VI_H_START_REG = &vi->VI_H_START_REG; core.VI_V_START_REG = &vi->VI_V_START_REG; core.VI_V_BURST_REG = &vi->VI_V_BURST_REG;
        core.VI_X_SCALE_REG = &vi->VI_X_SCALE_REG; core.VI_Y_SCALE_REG = &vi->VI_Y_SCALE_REG;
        RT64::ApplicationConfiguration config; config.detectDataPath = false; config.useConfigurationFile = false;
        app = std::make_unique<RT64::Application>(core, config);
        app->userConfig.graphicsAPI = api;
        app->userConfig.resolution = RT64::UserConfiguration::Resolution::WindowIntegerScale;
        using UC = RT64::UserConfiguration;
        if (hangtime_video.resolution > 0) { app->userConfig.resolution = UC::Resolution::Manual; app->userConfig.resolutionMultiplier = hangtime_video.resolution; }
        if (hangtime_video.aspect == 1) app->userConfig.aspectRatio = UC::AspectRatio::Expand;
        if (hangtime_video.widescreen) {  // fixed target so widescreen.cpp's extension matches; extended origins reach the edges
            app->userConfig.aspectRatio = UC::AspectRatio::Manual; app->userConfig.aspectTarget = hangtime_video.aspect_ratio;
            app->userConfig.extAspectRatio = UC::AspectRatio::Expand;
        }
        if (hangtime_video.aspect == 2) { app->userConfig.aspectRatio = UC::AspectRatio::Manual; app->userConfig.aspectTarget = hangtime_video.aspect_ratio; }
        if (hangtime_video.refresh == 1) app->userConfig.refreshRate = UC::RefreshRate::Display;
        if (hangtime_video.refresh == 2) { app->userConfig.refreshRate = UC::RefreshRate::Manual; app->userConfig.refreshRateTarget = hangtime_video.refresh_hz; }
        if (hangtime_video.upscale2d == 0) app->userConfig.upscale2D = UC::Upscale2D::Original;
        if (hangtime_video.upscale2d == 1) app->userConfig.upscale2D = UC::Upscale2D::ScaledOnly;
        if (hangtime_video.upscale2d == 2) app->userConfig.upscale2D = UC::Upscale2D::All;
        if (hangtime_video.three_point >= 0) app->userConfig.threePointFiltering = hangtime_video.three_point != 0;
        if (hangtime_video.msaa == 2) app->userConfig.antialiasing = UC::Antialiasing::MSAA2X;
        if (hangtime_video.msaa == 4) app->userConfig.antialiasing = UC::Antialiasing::MSAA4X;
        if (hangtime_video.msaa == 8) app->userConfig.antialiasing = UC::Antialiasing::MSAA8X;
        fprintf(stderr, "VIDEO aspect=%d ratio=%.3f refresh=%d hz=%d msaa=%d resolution=%.2f upscale2d=%d three_point=%d\n", hangtime_video.aspect, hangtime_video.aspect_ratio,
                hangtime_video.refresh, hangtime_video.refresh_hz, hangtime_video.msaa, hangtime_video.resolution, hangtime_video.upscale2d, hangtime_video.three_point);
        const auto result = app->setup(window_thread);
        setup_result = result == RT64::Application::SetupResult::Success ? ultramodern::renderer::SetupResult::Success : ultramodern::renderer::SetupResult::GraphicsDeviceNotFound;
        chosen_api = chosen;
        fprintf(stderr, "RT64 setup %s api=%s\n", setup_result == ultramodern::renderer::SetupResult::Success ? "ok" : "FAILED",
                chosen == ultramodern::renderer::GraphicsApi::D3D12 ? "D3D12" : "Vulkan");
        if (setup_result == ultramodern::renderer::SetupResult::Success) {
            const auto& d = app->device->getDescription();
            const auto& caps = app->device->getCapabilities();
            // type=cpu is a software rasterizer (e.g. Mesa llvmpipe): never a valid performance measurement.
            static const char* types[] = {"unknown", "integrated", "discrete", "virtual", "cpu"};
            fprintf(stderr, "GPU device=\"%s\" type=%s vendor=0x%X driver=%llu vram_mb=%llu present_wait=%d display_timing=%d swap_textures=%u\n",
                    d.name.c_str(), (unsigned)d.type < 5 ? types[(int)d.type] : "?", (unsigned)d.vendor, (unsigned long long)d.driverVersion, (unsigned long long)(d.dedicatedVideoMemory >> 20),
                    (int)caps.presentWait, (int)caps.displayTiming, app->swapChain ? app->swapChain->getTextureCount() : 0);
        }
    }
    bool valid() override { return setup_result == ultramodern::renderer::SetupResult::Success; }
    bool update_config(const ultramodern::renderer::GraphicsConfig&, const ultramodern::renderer::GraphicsConfig&) override { return true; }
    void enable_instant_present() override {}
    unsigned display_lists = 0, screens = 0;
    int last_mode = -1;
    void send_dl(const OSTask* task) override {
        ++display_lists;
        perf_event('D', display_lists);
        perf_gfx_phase(1);
        const uint32_t game_dl = task->t.data_ptr & 0x3FFFFFF;
        const auto tw = std::chrono::steady_clock::now();
        const uint32_t dl = hangtime_ws_rewrite(app->core.RDRAM, game_dl);   // identity unless --widescreen
        if (dl != game_dl) perf_event('w', std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - tw).count());
        if (const std::string path = hangtime_dl_dump_due(); !path.empty()) {
            if (FILE* f = fopen(path.c_str(), "w")) {
                fprintf(f, "# display list %u (task data 0x%08X), VI %u\n", display_lists, (uint32_t)task->t.data_ptr, screens);
                hangtime_dump_dl(app->core.RDRAM, game_dl, f);
                fclose(f);
            }
            if (dl != game_dl) {                                              // the widescreen-rewritten list of the same frame
                if (FILE* f = fopen((path + ".ws.txt").c_str(), "w")) { hangtime_dump_dl(app->core.RDRAM, dl, f); fclose(f); }
                hangtime_ws_trace_write((path + ".calls.txt").c_str());
            }
            fprintf(stderr, "DLDUMP dl=%u %s\n", display_lists, path.c_str());
        }
        const auto t0 = std::chrono::steady_clock::now();
        app->state->rsp->reset();
        app->interpreter->loadUCodeGBI(task->t.ucode & 0x3FFFFFF, task->t.ucode_data & 0x3FFFFFF, true);
        if (hangtime_video.dl_check) hangtime_dl_snapshot(app->core.RDRAM, dl, dl_snapshot);
        if (hangtime_video.dl_delay_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(hangtime_video.dl_delay_ms));
        app->processDisplayLists(app->core.RDRAM, dl, 0, true);
        if (hangtime_video.dl_check) {
            const DlChange c = hangtime_dl_compare(app->core.RDRAM, dl_snapshot);
            if (c.total()) {
                ++dl_race_lists;
                if (dl_race_lists <= 20 || dl_race_lists % 100 == 0) {
                    char at[128]; int n = 0;
                    for (unsigned k = 0; k < c.total() && k < 4; ++k)
                        n += snprintf(at + n, sizeof(at) - n, " %c@%06X+%u", "cmvto"[(int)c.first[k].kind], c.first[k].addr, c.first[k].len);
                    fprintf(stderr, "DLRACE dl=%u lists=%u changed cmd=%u mtx=%u vtx=%u tex=%u other=%u bytes=%llu ranges=%zu at%s\n", display_lists, dl_race_lists,
                            c.ranges[0], c.ranges[1], c.ranges[2], c.ranges[3], c.ranges[4], (unsigned long long)c.bytes, dl_snapshot.ranges.size(), at);
                }
            }
        }
        dl_us += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
        perf_gfx_phase(0);
        perf_event('d', display_lists);
    }
    DlSnapshot dl_snapshot;      // --dl-check
    unsigned dl_race_lists = 0;  // display lists whose inputs changed while RT64 read them
    unsigned present_skips = 0;   // VIs whose present the presentation guard skipped (update_screen)
    long long dl_us = 0, present_us = 0;  // host time in display-list processing / presentation, per PROGRESS second
    void send_dummy_workload(uint32_t) override {}
    void update_screen() override {
        if (hangtime_ws_toggle_request.exchange(false)) {   // F10: switch presentation between frames
            const bool on = !hangtime_ws_is_enabled();
            using UC = RT64::UserConfiguration;
            app->userConfig.aspectRatio = on ? UC::AspectRatio::Manual : UC::AspectRatio::Original;
            app->userConfig.aspectTarget = hangtime_video.aspect_ratio;
            app->userConfig.extAspectRatio = on ? UC::AspectRatio::Expand : UC::AspectRatio::Original;
            app->updateUserConfig(true);
            hangtime_video.widescreen = on;
            hangtime_ws_set_enabled(on);
            fprintf(stderr, "WIDESCREEN %s\n", on ? "on" : "off");
        }
        const auto tp = std::chrono::steady_clock::now();
        auto* vi_regs = ultramodern::renderer::get_vi_regs();
        perf_event('S', vi_regs->VI_ORIGIN_REG, screens + 1);
        // Presentation guard: hand RT64 a new present only once its present thread has picked up
        // the previous one. Otherwise RT64's 4-entry present ring fills while a swap-chain Present
        // blocks, updateScreen spins on it, and workloads waiting on the queued presents fill the
        // workload ring so processDisplayLists blocks too: this gfx thread (VI screen updates and
        // DP completion) stalls and the game misses VIs. Skipping the present of a VI whose
        // predecessor is still queued shows the newest frame when the display path is free again,
        // the way RT64 already skips stale presents; game timing no longer depends on how fast the
        // host can put frames on screen (on a console, scan-out never blocks the RDP or CPU). When
        // presentation keeps up, nothing is ever skipped. --present-blocking restores the old path.
        bool present = true;
        if (!hangtime_video.present_blocking) {
            uint64_t picked_up;
            { std::scoped_lock lock(app->presentQueue->presentIdMutex); picked_up = app->presentQueue->presentId; }
            present = picked_up >= app->state->presentId;
        }
        if (present) {
            perf_gfx_phase(2);
            app->updateScreen();
            perf_gfx_phase(0);
        } else {
            ++present_skips;
            perf_event('k', vi_regs->VI_ORIGIN_REG, screens + 1);
        }
        perf_event('s', vi_regs->VI_ORIGIN_REG, screens + 1);
        present_us += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - tp).count();
        // Progress line once per second of VI: retraces presented and display lists rendered.
        // period/score are read-only observations of game globals (FULL_GAME.md): 0x800A6662
        // periods completed (zeroed by match init 0x80033C1C, incremented by the end-of-period
        // handler 0x800307B0: 4 = regulation over, 5-7 overtime), 0x800A6688/8A running team
        // scores (0x800307B0 derives the per-quarter scores from them).
        auto half = [this](uint32_t a) { return *reinterpret_cast<const int16_t*>(app->core.RDRAM + ((a - 0x80000000u) ^ 2)); };
        // MODE line whenever the game's mode word changes (0x800A71C4: set by match init 0x80033C1C,
        // the stats screen 0x800458E0 and ~15 other screen routines; per-value meanings not mapped).
        if (const int mode = half(0x800A71C4); mode != last_mode) {
            last_mode = mode;
            perf_event('M', (uint16_t)mode);
            static const auto t0 = std::chrono::steady_clock::now();  // ~launch (first presented VI)
            fprintf(stderr, "MODE poll=%u vi=%u t=%.1f mode=%d\n", hangtime_poll_count(), screens + 1,
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), mode);
        }
        if (++screens % 60 == 0) {
            auto* vi = ultramodern::renderer::get_vi_regs();
            // stats= per player FGM/FGA/assists from the game's stat records (0x800A6D78 + 0x20*p,
            // halfwords [0] 2PA [1] 2PM [2] 3PA [3] 3PM [12] assists, as drawn by 0x80046310).
            char stats[96]; int n = 0;
            for (uint32_t p = 0; p < 4; ++p) {
                const uint32_t r = 0x800A6D78 + p * 0x20;
                auto u = [&](int k) { return (unsigned)(uint16_t)half(r + 2 * k); };
                n += snprintf(stats + n, sizeof(stats) - n, "%s%u/%u/%u", p ? "," : "", u(1) + u(3), u(0) + u(2), u(12));
            }
            fprintf(stderr, "PROGRESS vi=%u display_lists=%u origin=%08X hstart=%08X period=%d score=%d-%d polls=%u stats=%s dl_ms=%.0f present_ms=%.0f swap_hz=%u fire=%X present_skips=%u dl_races=%u\n", screens, display_lists,
                    vi->VI_ORIGIN_REG, vi->VI_H_START_REG, half(0x800A6662), half(0x800A6688), half(0x800A668A), hangtime_poll_count(), stats, dl_us / 1000.0, present_us / 1000.0,
                    app->sharedQueueResources->swapChainRate, (unsigned)(uint16_t)half(0x800A6EB0), present_skips, dl_race_lists);
            dl_us = present_us = 0;
            if (perf_enabled()) {   // RT64's own profilers (ms averages over their last 120 samples), as microseconds
                auto us = [](const RT64::ProfilingTimer& t) { return (uint64_t)(t.average() * 1000.0); };
                perf_event('R', us(app->workloadQueue->rendererGPUProfiler), us(app->workloadQueue->workloadProfiler));
                perf_event('r', us(app->presentQueue->presentProfiler), us(app->state->dlCpuProfiler));
                // Ring write positions advance once per swap-chain present / per rendered workload:
                // their per-second deltas are the real present and workload rates.
                perf_event('h', app->presentQueue->presentProfiler.index(), app->workloadQueue->workloadProfiler.index());
            }
            perf_flush();   // also writes the --profile-gfx table
            if (hangtime_video.widescreen) {   // view clamp of the last second (widescreen.cpp)
                int camx, clamp, need; hangtime_ws_stats(&camx, &clamp, &need);
                unsigned us = 0, ush = 0; const unsigned uc = hangtime_ws_unculled(&us, &ush);
                fprintf(stderr, "WSVIEW vi=%u camx=%d clamp=%d need=%d unculled=%u sprites=%u shifted=%u\n", screens, camx, clamp, need, uc, us, ush);
            }
        }
    }
    void shutdown() override { if (app) app->end(); }
    uint32_t get_display_framerate() const override { return 60; }
    float get_resolution_scale() const override { return 1.0f; }
};
}

std::unique_ptr<ultramodern::renderer::RendererContext> hangtime_create_rt64_context(uint8_t* rdram, ultramodern::renderer::WindowHandle window, bool) {
    return std::make_unique<RT64Context>(rdram, window);
}
