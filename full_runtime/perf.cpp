// --perf event log and --profile-gfx sampler (perf.hpp). Events are appended under a mutex to a
// buffered file and flushed about once per second (perf_flush from the renderer's progress tick),
// so a killed process loses at most the last second.
#include "perf.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static FILE* perf_file = nullptr;
static std::mutex perf_mutex;
static std::atomic<bool> perf_on{false};
static std::chrono::steady_clock::time_point perf_t0;

void perf_open(const char* path) {
    perf_file = fopen(path, "w");
    if (!perf_file) { fprintf(stderr, "PERF cannot open %s\n", path); return; }
    setvbuf(perf_file, nullptr, _IOFBF, 1 << 20);
    perf_t0 = std::chrono::steady_clock::now();
    fprintf(perf_file, "kind,t_us,a,b\n");
    perf_on = true;
}
bool perf_enabled() { return perf_on.load(std::memory_order_relaxed); }
void perf_event(char kind, uint64_t a, uint64_t b) {
    if (!perf_on.load(std::memory_order_relaxed)) return;
    const auto t = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - perf_t0).count();
    std::lock_guard<std::mutex> lock(perf_mutex);
    fprintf(perf_file, "%c,%lld,%llu,%llu\n", kind, (long long)t, (unsigned long long)a, (unsigned long long)b);
}
void perf_flush() {
    if (perf_on.load(std::memory_order_relaxed)) { std::lock_guard<std::mutex> lock(perf_mutex); fflush(perf_file); }
    perf_profile_flush();
}

// ---- gfx-thread sampler ----
static std::atomic<int> gfx_phase{0};
static std::atomic<int64_t> gfx_phase_start_us{0};
static std::atomic<bool> profile_on{false};
static int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
void perf_gfx_phase(int phase);

#ifdef _WIN32
#include <windows.h>
static std::atomic<DWORD> gfx_thread_id{0};
static std::string profile_path;
static std::mutex profile_mutex;
static std::map<std::string, uint32_t> profile_counts;   // "phase,bucket,mod+rva;..." -> samples
static bool profile_dirty = false;

void perf_gfx_phase(int phase) {
    if (!profile_on.load(std::memory_order_relaxed)) return;
    if (!gfx_thread_id.load(std::memory_order_relaxed)) gfx_thread_id = GetCurrentThreadId();
    gfx_phase_start_us.store(now_us(), std::memory_order_relaxed);
    gfx_phase.store(phase, std::memory_order_release);
}

// Captures one stack of a thread (no allocation while it is suspended: it may hold the heap
// lock), then records it under "phase,bucket".
static void sample_thread(HANDLE t, int phase, int bucket, std::map<DWORD64, std::string>& module_names, const std::atomic<int>* live_phase) {
    DWORD64 bases[48]; DWORD64 rips[48];
    if (SuspendThread(t) == (DWORD)-1) return;
    if (live_phase) phase = live_phase->load(std::memory_order_acquire);   // the phase the thread is really in
    CONTEXT ctx{}; ctx.ContextFlags = CONTEXT_FULL;
    int n = 0;
    if (phase && GetThreadContext(t, &ctx)) {
        for (; n < 48 && ctx.Rip; ++n) {
            DWORD64 image = 0;
            auto entry = RtlLookupFunctionEntry(ctx.Rip, &image, nullptr);
            rips[n] = ctx.Rip; bases[n] = image;
            if (entry) { PVOID h = nullptr; DWORD64 est = 0; RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, ctx.Rip, entry, &ctx, &h, &est, nullptr); }
            else { ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp); ctx.Rsp += 8; }
        }
    }
    ResumeThread(t);
    if (!phase || !n) return;
    std::string key = std::to_string(phase) + "," + std::to_string(bucket) + ",";
    for (int i = 0; i < n; ++i) {
        auto it = module_names.find(bases[i]);
        if (it == module_names.end()) {
            char name[MAX_PATH] = "?";
            if (bases[i]) GetModuleFileNameA((HMODULE)bases[i], name, MAX_PATH);
            const char* b = strrchr(name, '\\') ? strrchr(name, '\\') + 1 : name;
            it = module_names.emplace(bases[i], b).first;
        }
        char frame[96]; snprintf(frame, sizeof(frame), "%s%s+0x%llX", i ? ";" : "", it->second.c_str(), (unsigned long long)(rips[i] - bases[i]));
        key += frame;
    }
    std::lock_guard<std::mutex> lock(profile_mutex);
    ++profile_counts[key]; profile_dirty = true;
}

// RT64's own threads, found by the names RT64 gives them (SetThreadDescription): sampled always,
// phase 3 "RT64 Present", 4 "RT64 Workload", 5 "RT64 Texture" (bucket 0).
#include <tlhelp32.h>
static void find_rt64_threads(HANDLE out[3]) {
    static const wchar_t* names[3] = {L"RT64 Present", L"RT64 Workload", L"RT64 Texture"};
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te{sizeof(te)};
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != GetCurrentProcessId()) continue;
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!h) continue;
        PWSTR desc = nullptr; bool kept = false;
        if (SUCCEEDED(GetThreadDescription(h, &desc)) && desc)
            for (int i = 0; i < 3; ++i) if (!out[i] && wcscmp(desc, names[i]) == 0) { out[i] = h; kept = true; break; }
        if (desc) LocalFree(desc);
        if (!kept) CloseHandle(h);
    }
    CloseHandle(snap);
}

static void sampler() {
    HANDLE t = nullptr, rt64[3] = {};
    std::map<DWORD64, std::string> module_names;
    for (unsigned iter = 0;; ++iter) {
        Sleep(1);
        if ((iter % 1000) == 0 && (!rt64[0] || !rt64[1] || !rt64[2])) find_rt64_threads(rt64);
        for (int i = 0; i < 3; ++i) if (rt64[i]) sample_thread(rt64[i], 3 + i, 0, module_names, nullptr);
        const DWORD id = gfx_thread_id.load();
        if (!id) continue;
        if (!t) t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, id);
        if (!t || !gfx_phase.load(std::memory_order_acquire)) continue;
        const int64_t elapsed = now_us() - gfx_phase_start_us.load(std::memory_order_relaxed);
        const int bucket = elapsed < 4000 ? 0 : elapsed < 8000 ? 1 : 2;   // ms into the call: <4, 4-8, >=8
        sample_thread(t, 1, bucket, module_names, &gfx_phase);
    }
}
void perf_profile_open(const char* path) {
    profile_path = path;
    profile_on = true;
    std::thread(sampler).detach();
}
void perf_profile_flush() {
    if (!profile_on.load()) return;
    std::lock_guard<std::mutex> lock(profile_mutex);
    if (!profile_dirty) return;
    profile_dirty = false;
    if (FILE* f = fopen(profile_path.c_str(), "w")) {
        fprintf(f, "count,phase,bucket,stack\n");
        for (auto& [k, c] : profile_counts) {
            const auto c1 = k.find(','), c2 = k.find(',', c1 + 1);
            fprintf(f, "%u,%s,%s,%s\n", c, k.substr(0, c1).c_str(), k.substr(c1 + 1, c2 - c1 - 1).c_str(), k.substr(c2 + 1).c_str());
        }
        fclose(f);
    }
}
#else
void perf_gfx_phase(int phase) {
    if (!profile_on.load(std::memory_order_relaxed)) return;
    gfx_phase_start_us.store(now_us(), std::memory_order_relaxed);
    gfx_phase.store(phase, std::memory_order_release);
}
void perf_profile_open(const char*) { fprintf(stderr, "--profile-gfx is Windows-only (ignored)\n"); }
void perf_profile_flush() {}
#endif
