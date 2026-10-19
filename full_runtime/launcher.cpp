// Player-facing launcher pieces: settings file, ROM discovery/verification/picker, log file.
// Portable (Windows / Linux): own SHA-256, per-OS executable and data paths, nativefiledialog-
// extended (RT64's copy) for the picker.
#include "launcher.hpp"
#include "branding.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "nfd.h"

namespace fs = std::filesystem;

const char* const HANGTIME_ROM_SHA256 = "6e1304d5bf8922a652f4063f9d26120ac8c3e9af2a5f8a4f4bc733cf003edf83";  // NBA Hangtime (USA)
const char* const HANGTIME_VERSION = HANGTIME_PROJECT_VERSION;   // project(VERSION) in CMakeLists.txt

std::string launcher_utf8(const fs::path& p) { const auto u = p.u8string(); return std::string(u.begin(), u.end()); }

fs::path launcher_exe_dir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(std::wstring(buf, n)).parent_path();
#else
    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::current_path() : exe.parent_path();
#endif
}

// Portable when the executable's folder is writable (settings and log beside it), otherwise the
// per-user folder: %LOCALAPPDATA%\NBAHangtimeRecomp, or $XDG_CONFIG_HOME (~/.config)/nbahangtime.
fs::path launcher_data_dir() {
    static const fs::path dir = [] {
        const fs::path exe = launcher_exe_dir();
        const fs::path probe = exe / "hangtime.ini.tmp";
        if (FILE* f = fopen(launcher_utf8(probe).c_str(), "ab")) { fclose(f); std::error_code ec; fs::remove(probe, ec); return exe; }
        fs::path out = exe;
#ifdef _WIN32
        PWSTR local = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) out = fs::path(local) / "NBAHangtimeRecomp";
        CoTaskMemFree(local);
#else
        if (const char* x = getenv("XDG_CONFIG_HOME"); x && *x) out = fs::path(x) / "nbahangtime";
        else if (const char* h = getenv("HOME")) out = fs::path(h) / ".config" / "nbahangtime";
#endif
        std::error_code ec; fs::create_directories(out, ec);
        return out;
    }();
    return dir;
}

LauncherSettings launcher_load_settings() {
    LauncherSettings s;
    std::ifstream f(launcher_data_dir() / "hangtime.ini");
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto eq = line.find('=');
        if (eq == std::string::npos || line[0] == '#' || line[0] == ';') continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        const int n = atoi(v.c_str());
        if (k == "rom") s.rom = fs::u8path(v);
        else if (k == "fullscreen") { if (v == "1" && s.window_mode == 0) s.window_mode = 1; }   // older files
        else if (k == "window_mode") s.window_mode = std::clamp(n, 0, 2);
        else if (k == "widescreen") s.widescreen = v == "1";
        else if (k == "resolution") s.resolution = n;
        else if (k == "msaa") s.msaa = n;
        else if (k == "skip_launcher") s.skip_launcher = v == "1";
        else if (k == "master_volume") s.master_volume = std::clamp(n, 0, 100);
        else if (k == "audio_device") s.audio_device = v;
        else if (k == "render_gpu") s.render_gpu = v;
        else if (k.rfind("cheat.", 0) == 0) { if (v == "1") s.cheats.insert(k.substr(6)); }
        else s.extra[k] = v;
    }
    return s;
}

void launcher_save_settings(const LauncherSettings& s) {
    std::ofstream f(launcher_data_dir() / "hangtime.ini", std::ios::trunc);
    f << "# " HANGTIME_PRODUCT_NAME " settings (written by version " << HANGTIME_VERSION << ")\n";
    f << "rom=" << launcher_utf8(s.rom) << "\n";
    f << "window_mode=" << s.window_mode << "\n";
    f << "widescreen=" << (s.widescreen ? 1 : 0) << "\n";
    f << "resolution=" << s.resolution << "\n";
    f << "msaa=" << s.msaa << "\n";
    f << "skip_launcher=" << (s.skip_launcher ? 1 : 0) << "\n";
    f << "master_volume=" << s.master_volume << "\n";
    f << "audio_device=" << s.audio_device << "\n";
    f << "render_gpu=" << s.render_gpu << "\n";
    for (const auto& c : s.cheats) f << "cheat." << c << "=1\n";
    for (const auto& [k, v] : s.extra) f << k << "=" << v << "\n";
}

// ---- SHA-256 (FIPS 180-4), portable ----
std::string launcher_sha256_hex(const std::vector<uint8_t>& data) {
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
        0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
        0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
        0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    auto block = [&](const uint8_t* p) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) w[i] = (uint32_t)p[4 * i] << 24 | p[4 * i + 1] << 16 | p[4 * i + 2] << 8 | p[4 * i + 3];
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
            const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    };
    const size_t n = data.size(), full = n / 64;
    for (size_t i = 0; i < full; ++i) block(data.data() + 64 * i);
    uint8_t tail[128]{};
    const size_t rem = n - full * 64;
    memcpy(tail, data.data() + full * 64, rem);
    tail[rem] = 0x80;
    const size_t tlen = rem < 56 ? 64 : 128;
    const uint64_t bits = (uint64_t)n * 8;
    for (int i = 0; i < 8; ++i) tail[tlen - 1 - i] = (uint8_t)(bits >> (8 * i));
    block(tail); if (tlen == 128) block(tail + 64);
    char hex[65];
    for (int i = 0; i < 8; ++i) snprintf(hex + 8 * i, 9, "%08x", h[i]);
    return hex;
}

// Reads a ROM in any common dump byte order (.z64 big-endian, .v64 16-bit swapped, .n64 32-bit
// little-endian), converts it to z64 in memory and requires the exact NBA Hangtime (USA) image.
bool launcher_load_rom(const fs::path& path, std::vector<uint8_t>& rom, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { error = "cannot open " + launcher_utf8(path); return false; }
    rom.assign(std::istreambuf_iterator<char>(f), {});
    if (rom.size() != 12 * 1024 * 1024) { error = launcher_utf8(path.filename()) + " is not a 12 MiB N64 ROM"; return false; }
    const uint32_t magic = (uint32_t)rom[0] << 24 | rom[1] << 16 | rom[2] << 8 | rom[3];
    if (magic == 0x37804012) for (size_t i = 0; i + 1 < rom.size(); i += 2) std::swap(rom[i], rom[i + 1]);           // v64
    else if (magic == 0x40123780) for (size_t i = 0; i + 3 < rom.size(); i += 4) { std::swap(rom[i], rom[i + 3]); std::swap(rom[i + 1], rom[i + 2]); }  // n64
    else if (magic != 0x80371240) { error = launcher_utf8(path.filename()) + " is not an N64 ROM image"; return false; }
    const std::string hash = launcher_sha256_hex(rom);
    if (hash != HANGTIME_ROM_SHA256) {
        error = launcher_utf8(path.filename()) + " is not NBA Hangtime (USA) (SHA-256 " + hash + ")";
        return false;
    }
    return true;
}

std::optional<fs::path> launcher_pick_rom() {
    if (NFD_Init() != NFD_OKAY) return std::nullopt;
    nfdu8char_t* out = nullptr;
    const nfdu8filteritem_t filters[] = {{"N64 ROM", "z64,n64,v64"}};
    const nfdresult_t r = NFD_OpenDialogU8(&out, filters, 1, nullptr);
    std::optional<fs::path> picked;
    if (r == NFD_OKAY && out) { picked = fs::u8path(out); NFD_FreePathU8(out); }
    NFD_Quit();
    return picked;
}

// Windows GUI-subsystem executable: when nobody redirected stderr (double-click), write the log
// to hangtime.log in the data folder; --console attaches to the launching terminal instead.
// Linux: stderr stays a terminal, file or pipe the caller gave it; otherwise the same hangtime.log.
void launcher_setup_log(bool console) {
#ifdef _WIN32
    if (console && AttachConsole(ATTACH_PARENT_PROCESS)) { freopen("CONOUT$", "w", stderr); freopen("CONOUT$", "w", stdout); return; }
    const HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    // Redirected by the caller to a file or pipe (tests): keep it. A character device here is NUL
    // (shortcuts, launchers), where the log would be lost, so it goes to hangtime.log instead.
    if (h && h != INVALID_HANDLE_VALUE && (GetFileType(h) == FILE_TYPE_DISK || GetFileType(h) == FILE_TYPE_PIPE)) return;
    const fs::path log = launcher_data_dir() / "hangtime.log";
    _wfreopen(log.c_str(), L"w", stderr);
#else
    (void)console;
    struct stat st{};
    if (isatty(STDERR_FILENO)) return;
    if (fstat(STDERR_FILENO, &st) == 0 && (S_ISREG(st.st_mode) || S_ISFIFO(st.st_mode))) return;  // redirected by the caller
    if (!freopen(launcher_utf8(launcher_data_dir() / "hangtime.log").c_str(), "w", stderr)) return;
#endif
    setvbuf(stderr, nullptr, _IONBF, 0);  // a killed process must not lose the log
}

bool launcher_restart(const std::vector<std::string>& args) {
#ifdef _WIN32
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";
    for (const auto& a : args) { cmd += L" \""; cmd += fs::u8path(a).wstring(); cmd += L"\""; }
    STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return true;
#else
    const std::string exe = launcher_utf8(fs::read_symlink("/proc/self/exe"));
    if (fork() == 0) {
        std::vector<char*> argv{const_cast<char*>(exe.c_str())};
        for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execv(exe.c_str(), argv.data());
        _exit(127);
    }
    return true;
#endif
}
