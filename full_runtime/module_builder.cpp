// First-run game module build (module_builder.hpp).
#include "module_builder.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>
#include "launcher.hpp"
#include "partition.hpp"
#include "branding.hpp"
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

#ifdef _WIN32
static const char* const EXE = ".exe";
static const char* const MODULE_NAME = "hangtime_game.dll";
static const char* const TARGET = "x86_64-windows-gnu";
#else
static const char* const EXE = "";
static const char* const MODULE_NAME = "hangtime_game.so";
static const char* const TARGET = "x86_64-linux-gnu";
#endif

ModuleBuildPaths module_build_paths() {
    const fs::path exe = launcher_exe_dir(), data = launcher_data_dir();
    ModuleBuildPaths p;
    p.setup = exe / "setup";
    p.tools = exe / "tools";
    p.zig = exe / "tools" / "zig" / (std::string("zig") + EXE);
    p.import_lib = exe / "hangtime.lib";
    p.work = data / "game_build";
    p.output = data / MODULE_NAME;
    return p;
}

bool module_build_available(const ModuleBuildPaths& p) {
    for (const fs::path& f : {p.setup / "audio_rsp.toml", p.setup / "sdk" / "src" / "game_module_api.cpp",
                              p.tools / (std::string("N64Recomp") + EXE), p.tools / (std::string("RSPRecomp") + EXE), p.zig})
        if (!fs::exists(f)) return false;
#ifdef _WIN32
    if (!fs::exists(p.import_lib)) return false;
#endif
    return true;
}

// Runs a program with its output appended to `log`; returns the exit code (-1 if it did not start).
static int run(const std::vector<std::string>& args, const fs::path& cwd, const fs::path& log) {
#ifdef _WIN32
    std::wstring cmd;
    for (const auto& a : args) {   // standard Windows argument quoting
        const std::wstring w = fs::u8path(a).wstring();
        if (!cmd.empty()) cmd += L' ';
        cmd += L'"';
        size_t backslashes = 0;
        for (wchar_t c : w) {
            if (c == L'\\') { ++backslashes; continue; }
            if (c == L'"') cmd.append(backslashes * 2 + 1, L'\\');
            else cmd.append(backslashes, L'\\');
            backslashes = 0;
            cmd += c;
        }
        cmd.append(backslashes * 2, L'\\');
        cmd += L'"';
    }
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE out = CreateFileW(log.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = si.hStdError = out; si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    const BOOL started = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, cwd.c_str(), &si, &pi);
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    if (!started) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return (int)code;
#else
    const pid_t pid = fork();
    if (pid == 0) {
        if (chdir(cwd.c_str()) != 0) _exit(126);
        const int fd = open(log.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); close(fd); }
        std::vector<char*> argv;
        for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execv(argv[0], argv.data());
        _exit(127);
    }
    if (pid < 0) return -1;
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

static std::string tail(const fs::path& f, size_t bytes = 1500) {
    std::ifstream in(f, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return s.size() > bytes ? s.substr(s.size() - bytes) : s;
}

// Copies a configuration template, replacing the values of the given keys.
static bool write_config(const fs::path& in, const fs::path& out, const std::vector<std::pair<std::string, std::string>>& values) {
    std::ifstream src(in);
    std::ofstream dst(out, std::ios::trunc);
    if (!src || !dst) return false;
    std::string line;
    while (std::getline(src, line)) {
        std::string key = line.substr(0, line.find('='));
        key.erase(std::remove(key.begin(), key.end(), ' '), key.end());
        for (const auto& [k, v] : values) if (key == k && line.find('=') != std::string::npos) { line = k + " = '" + v + "'"; break; }
        dst << line << '\n';
    }
    return true;
}

// Everything in the work folder except the build log is derived from the ROM (the ROM copy,
// generated sources, objects, the linker's .pdb/.lib side files) and is removed when done.
static void clean_work(const fs::path& work) {
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(work, ec))
        if (e.path().filename() != "build.log") fs::remove_all(e.path(), ec);
}

void module_build(const ModuleBuildPaths& p, const fs::path& rom_path, ModuleBuildProgress& g) {
    auto fail = [&](const std::string& why) {
        clean_work(p.work);
        g.error = why + "\n\nDetails: " + launcher_utf8(g.log);
        g.ok = false; g.done = true;
    };
    std::error_code ec;
    fs::remove_all(p.work, ec);
    fs::create_directories(p.work / "generated", ec);
    fs::create_directories(p.work / "obj", ec);
    g.log = p.work / "build.log";
    { std::ofstream(g.log, std::ios::trunc) << HANGTIME_PRODUCT_NAME " game files build\n"; }

    // 1. The player's ROM as a big-endian image (launcher_load_rom checks the hash and converts
    //    .v64/.n64), only for the recompilers; removed when done.
    g.set_step("Reading your ROM"); g.fraction = 0.02f;
    std::vector<uint8_t> rom; std::string err;
    if (!launcher_load_rom(rom_path, rom, err)) return fail("The ROM could not be read: " + err);
    { std::ofstream f(p.work / "rom.z64", std::ios::binary); f.write(reinterpret_cast<const char*>(rom.data()), (std::streamsize)rom.size()); if (!f) return fail("Could not write to " + launcher_utf8(p.work)); }

    // 2. Recompile the game's CPU code and audio microcode (N64Recomp, RSPRecomp).
    g.set_step("Recompiling the game code"); g.fraction = 0.05f;
    // The function partition and hooks are computed here from the ROM (partition.cpp), so the
    // package carries no function tables of the game.
    HangtimePartition part;
    if (!hangtime_partition(rom, part, err)) return fail("The ROM could not be analysed: " + err);
    {
        std::ofstream(p.work / "full.symbols.toml", std::ios::binary) << part.symbols_toml;
        std::ofstream(p.work / "full.toml", std::ios::binary)
            << "[input]\nsymbols_file_path='" << launcher_utf8(fs::absolute(p.work / "full.symbols.toml")) << "'\nrom_file_path='rom.z64'\n"
               "output_func_path='generated'\nrecomp_include='#include \"hangtime_full.h\"'\n\n" << part.hooks_toml;
    }
    if (!write_config(p.setup / "audio_rsp.toml", p.work / "audio_rsp.toml", {{"rom_file_path", "rom.z64"}, {"output_file_path", "audio_rsp_generated.cpp"}}))
        return fail("Could not write the recompiler configuration.");
    if (run({launcher_utf8(p.tools / (std::string("N64Recomp") + EXE)), launcher_utf8(fs::absolute(p.work / "full.toml"))}, p.work, g.log) != 0) return fail("The game code recompiler failed.");
    g.fraction = 0.25f;
    // Absolute config paths: RSPRecomp aborts on a bare relative one (empty base directory).
    if (run({launcher_utf8(p.tools / (std::string("RSPRecomp") + EXE)), launcher_utf8(fs::absolute(p.work / "audio_rsp.toml"))}, p.work, g.log) != 0) return fail("The audio recompiler failed.");
    fs::remove(p.work / "rom.z64", ec);

    // 3. Compile with the bundled Zig toolchain (its cache sits beside the work folder and is reused).
    g.set_step("Compiling the game"); g.fraction = 0.30f;
#ifdef _WIN32
    SetEnvironmentVariableW(L"ZIG_GLOBAL_CACHE_DIR", (p.work.parent_path() / "zig-cache").c_str());
    SetEnvironmentVariableW(L"ZIG_LOCAL_CACHE_DIR", (p.work.parent_path() / "zig-cache").c_str());
#else
    setenv("ZIG_GLOBAL_CACHE_DIR", (p.work.parent_path() / "zig-cache").c_str(), 1);
    setenv("ZIG_LOCAL_CACHE_DIR", (p.work.parent_path() / "zig-cache").c_str(), 1);
#endif
    std::vector<fs::path> sources;
    for (const auto& e : fs::directory_iterator(p.work / "generated", ec))
        if (e.path().extension() == ".c" && e.path().filename().string().rfind("funcs_", 0) == 0) sources.push_back(e.path());
    std::sort(sources.begin(), sources.end());
    sources.push_back(p.work / "audio_rsp_generated.cpp");
    sources.push_back(p.setup / "sdk" / "src" / "rsp.cpp");
    sources.push_back(p.setup / "sdk" / "src" / "game_module_api.cpp");
    const std::vector<std::string> flags = {"-target", TARGET, "-O2", "-fwrapv", "-fno-strict-aliasing", "-msse4.1", "-w", "-fPIC",
        "-I" + launcher_utf8(p.setup / "sdk" / "include"), "-I" + launcher_utf8(p.setup / "sdk" / "include" / "librecomp"), "-I" + launcher_utf8(p.work / "generated")};
    std::atomic<size_t> next{0}, finished{0};
    std::atomic<bool> failed{false};
    std::mutex fail_mutex; std::string fail_log;
    auto worker = [&] {
        for (size_t i; !failed && (i = next++) < sources.size();) {
            const fs::path& src = sources[i];
            const bool cpp = src.extension() == ".cpp";
            std::vector<std::string> args = {launcher_utf8(p.zig), cpp ? "c++" : "cc"};
            args.insert(args.end(), flags.begin(), flags.end());
            args.push_back(cpp ? "-std=c++20" : "-std=c11");
            args.insert(args.end(), {"-c", launcher_utf8(src), "-o", launcher_utf8(p.work / "obj" / (src.filename().string() + ".o"))});
            const fs::path log = p.work / "obj" / (src.filename().string() + ".log");
            if (run(args, p.work, log) != 0) { failed = true; std::lock_guard<std::mutex> l(fail_mutex); fail_log = src.filename().string() + ":\n" + tail(log); }
            g.fraction = 0.30f + 0.55f * (float)++finished / (float)sources.size();
        }
    };
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < std::max(1u, std::thread::hardware_concurrency()); ++t) pool.emplace_back(worker);
    for (auto& t : pool) t.join();
    if (failed) { { std::ofstream(g.log, std::ios::app) << fail_log; } return fail("Compiling the game failed."); }

    // 4. Link the module (Windows: against the runtime's import library) and install it.
    g.set_step("Linking"); g.fraction = 0.88f;
    std::vector<std::string> link = {launcher_utf8(p.zig), "c++", "-target", TARGET, "-shared", "-o", launcher_utf8(p.work / MODULE_NAME)};
    for (const auto& src : sources) link.push_back(launcher_utf8(p.work / "obj" / (src.filename().string() + ".o")));
#ifdef _WIN32
    link.push_back(launcher_utf8(p.import_lib));
#endif
    if (run(link, p.work, g.log) != 0) return fail("Linking the game failed.");
    fs::rename(p.work / MODULE_NAME, p.output, ec);
    if (ec) { fs::copy_file(p.work / MODULE_NAME, p.output, fs::copy_options::overwrite_existing, ec); if (ec) return fail("Could not install the game files: " + ec.message()); }
    clean_work(p.work);
    g.set_step("Done"); g.fraction = 1.0f;
    g.ok = true; g.done = true;
}
