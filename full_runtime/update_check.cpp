// Updates (update_check.hpp). HTTPS through the operating system, no bundled TLS library:
// WinHTTP on Windows; on Linux the system's curl executable (present on SteamOS and common
// distributions; without it the check reports that it could not run). Archives are unpacked with
// the system's tar (Windows 10 1803+ ships bsdtar, which reads .zip).
// Test hook: HANGTIME_UPDATE_API=http://127.0.0.1:PORT replaces https://api.github.com, and
// downloads are then accepted only from that server (verify_update.py serves a fake release).
#include "update_check.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include "json/json.hpp"
#include "branding.hpp"
#include "launcher.hpp"
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {
// Never destroyed: a check still running when the program exits must not touch freed statics.
std::mutex& status_mutex = *new std::mutex;
UpdateStatus& status = *new UpdateStatus;
UpdateInstallStatus& install_status = *new UpdateInstallStatus;

#ifdef _WIN32
constexpr const char* EXE = ".exe";
constexpr const char* PACKAGE_PREFIX = "HangtimeRecomp-win64-";
constexpr const char* PACKAGE_SUFFIX = ".zip";
constexpr const char* MODULE = "hangtime_game.dll";
#else
constexpr const char* EXE = "";
constexpr const char* PACKAGE_PREFIX = "HangtimeRecomp-linux-x86_64-";
constexpr const char* PACKAGE_SUFFIX = ".tar.gz";
constexpr const char* MODULE = "hangtime_game.so";
#endif
constexpr const char* BACKUP_SUFFIX = ".old-update";

bool valid_repo(const std::string& r) {   // owner/name; also keeps the curl command line literal
    if (r.empty() || r.size() > 200 || r.find('/') == std::string::npos || r.find('/') != r.rfind('/')) return false;
    for (char c : r) if (!isalnum((unsigned char)c) && c != '-' && c != '_' && c != '.' && c != '/') return false;
    return r.front() != '/' && r.back() != '/' && r.find("..") == std::string::npos;
}
bool valid_url(const std::string& u) {    // characters a release URL uses; nothing a shell would interpret
    if (u.size() > 2000) return false;
    for (char c : u) if (!isalnum((unsigned char)c) && !strchr(":/._~%-+=?&", c)) return false;
    return true;
}

std::string test_api() { const char* t = getenv("HANGTIME_UPDATE_API"); return t && *t ? t : ""; }
std::string api_base() { const std::string t = test_api(); return t.empty() ? "https://api.github.com" : t; }

std::string user_agent() { return std::string(HANGTIME_PRODUCT_NAME "/") + HANGTIME_VERSION; }

// GET url, the body streamed to `sink` (false: stop). Returns the HTTP status (0: no connection,
// `error` says why). `progress` (optional) gets bytes received so far.
int http_get(const std::string& url, bool api, const std::function<bool(const char*, size_t)>& sink, std::string& error) {
    if (!valid_url(url)) { error = "unexpected address " + url; return 0; }
#ifdef _WIN32
    const std::wstring wurl(url.begin(), url.end());   // ASCII (valid_url)
    wchar_t host[256] = {}, path[2048] = {}, extra[2048] = {};
    URL_COMPONENTS uc{}; uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host; uc.dwHostNameLength = 256; uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) { error = "unexpected address " + url; return 0; }
    const std::string ua8 = user_agent();
    const std::wstring ua(ua8.begin(), ua8.end());
    int code = 0;
    HINTERNET session = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) session = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET connect = session ? WinHttpConnect(session, host, uc.nPort, 0) : nullptr;
    const std::wstring object = std::wstring(path) + extra;
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                     uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    bool stopped = false;
    if (request) {
        WinHttpSetTimeouts(request, 10000, 10000, 15000, 30000);
        const wchar_t* headers = api ? L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n" : WINHTTP_NO_ADDITIONAL_HEADERS;
        if (WinHttpSendRequest(request, headers, api ? (DWORD)-1L : 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr)) {
            DWORD sc = 0, len = sizeof(sc);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &sc, &len, WINHTTP_NO_HEADER_INDEX);
            code = (int)sc;
            std::vector<char> buf(1 << 16);
            for (DWORD got = 0;;) {
                if (!WinHttpReadData(request, buf.data(), (DWORD)buf.size(), &got)) { error = "the download was interrupted"; code = 0; break; }
                if (!got) break;
                if (!sink(buf.data(), got)) { stopped = true; break; }
            }
        }
    }
    if (!code && error.empty()) error = "no connection to " + std::string(host, host + wcslen(host)) + " (error " + std::to_string(GetLastError()) + ")";
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
    (void)stopped;
    return code;
#else
    // API answers are small: the status code is appended on its own line. Downloads stream the
    // body; -f turns an HTTP error into curl's exit status 22.
    const std::string cmd = std::string("curl -sL --connect-timeout 10 ") + (api ? "--max-time 15 -w '\\n%{http_code}' " : "-f ") +
                            "-A '" + user_agent() + "' " +
                            (api ? "-H 'Accept: application/vnd.github+json' -H 'X-GitHub-Api-Version: 2022-11-28' " : "") +
                            "'" + url + "' 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) { error = "could not run curl"; return 0; }
    std::string body;
    std::vector<char> buf(1 << 16);
    bool stopped = false;
    for (size_t n; (n = fread(buf.data(), 1, buf.size(), p)) > 0;) {
        if (api) { if (body.size() < (1u << 20)) body.append(buf.data(), n); }
        else if (!sink(buf.data(), n)) { stopped = true; break; }
    }
    const int rc = pclose(p);
    const int exit_code = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
    if (exit_code == 127) { error = "curl is not installed"; return 0; }
    if (api) {
        const size_t nl = body.rfind('\n');
        const int code = nl == std::string::npos ? 0 : atoi(body.c_str() + nl + 1);
        if (nl != std::string::npos) body.resize(nl);
        if (!code) { error = "no connection to github.com"; return 0; }
        sink(body.data(), body.size());
        return code;
    }
    if (stopped) return 200;
    if (exit_code == 22) { error = "the server refused the download"; return 404; }
    if (exit_code != 0) { error = "the download failed (curl " + std::to_string(exit_code) + ")"; return 0; }
    return 200;
#endif
}

int http_get_string(const std::string& url, bool api, std::string& body, std::string& error) {
    return http_get(url, api, [&](const char* d, size_t n) { if (body.size() < (8u << 20)) body.append(d, n); return true; }, error);
}

std::vector<long> version_parts(const std::string& v) {
    size_t i = 0;
    if (i < v.size() && (v[i] == 'v' || v[i] == 'V')) ++i;
    std::vector<long> parts;
    while (i < v.size() && isdigit((unsigned char)v[i])) {
        long n = 0;
        while (i < v.size() && isdigit((unsigned char)v[i])) n = n * 10 + (v[i++] - '0');
        parts.push_back(n);
        if (i < v.size() && v[i] == '.') ++i; else break;
    }
    return parts;
}

bool ends_with(const std::string& s, const std::string& t) { return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0; }

// Downloads are accepted only from the repository's own release files (GitHub then redirects to
// its storage host), or from the test server.
bool download_allowed(const std::string& repo, const std::string& url) {
    const std::string t = test_api();
    if (!t.empty()) return url.rfind(t + "/", 0) == 0;
    return url.rfind("https://github.com/" + repo + "/releases/download/", 0) == 0;
}

void set_install(UpdateInstallStatus::State s, float fraction = 0, const std::string& error = {}) {
    std::lock_guard<std::mutex> lock(status_mutex);
    install_status.state = s; install_status.fraction = fraction; install_status.error = error;
}

// Runs the system tar to unpack `archive` into `dir`.
bool extract(const fs::path& archive, const fs::path& dir, std::string& error) {
#ifdef _WIN32
    wchar_t sys[MAX_PATH]; GetSystemDirectoryW(sys, MAX_PATH);
    const fs::path tar = fs::path(sys) / "tar.exe";
    if (!fs::exists(tar)) { error = "this Windows has no tar.exe (Windows 10 1803 or newer is needed)"; return false; }
    std::wstring cmd = L"\"" + tar.wstring() + L"\" -xf \"" + archive.wstring() + L"\" -C \"" + dir.wstring() + L"\"";
    STARTUPINFOW si{}; si.cb = sizeof(si); PROCESS_INFORMATION pi{};
    if (!CreateProcessW(tar.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) { error = "could not run tar.exe"; return false; }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    if (code) { error = "the package could not be unpacked (tar " + std::to_string(code) + ")"; return false; }
    return true;
#else
    const std::string a = archive.string(), d = dir.string();
    const pid_t pid = fork();
    if (pid == 0) { execlp("tar", "tar", "-xzf", a.c_str(), "-C", d.c_str(), (char*)nullptr); _exit(127); }
    int rc = 0;
    if (pid < 0 || waitpid(pid, &rc, 0) < 0) { error = "could not run tar"; return false; }
    if (!WIFEXITED(rc) || WEXITSTATUS(rc)) { error = WIFEXITED(rc) && WEXITSTATUS(rc) == 127 ? "tar is not installed" : "the package could not be unpacked"; return false; }
    return true;
#endif
}

bool same_file(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (!fs::is_regular_file(a, ec) || !fs::is_regular_file(b, ec) || fs::file_size(a, ec) != fs::file_size(b, ec)) return false;
    std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
    std::vector<char> ba(1 << 16), bb(1 << 16);
    while (fa && fb) {
        fa.read(ba.data(), ba.size()); fb.read(bb.data(), bb.size());
        if (fa.gcount() != fb.gcount() || !std::equal(ba.begin(), ba.begin() + fa.gcount(), bb.begin())) return false;
    }
    return true;
}

// The first-start build kit (module_builder.hpp) of the new package differs from the installed one:
// the game module was generated or compiled by other tools or configuration.
bool kit_changed(const fs::path& root, const fs::path& exe_dir) {
    std::error_code ec;
    if (!fs::is_directory(root / "setup", ec)) return false;   // the new package has no kit: keep the module
    std::vector<fs::path> files;
    for (auto& e : fs::recursive_directory_iterator(root / "setup", ec)) if (e.is_regular_file()) files.push_back(fs::relative(e.path(), root));
    for (const char* t : {"N64Recomp", "RSPRecomp"}) files.push_back(fs::path("tools") / (std::string(t) + EXE));
    files.push_back(fs::path("tools") / "zig" / (std::string("zig") + EXE));
    for (const auto& f : files) if (!same_file(root / f, exe_dir / f)) return true;
    return false;
}

bool keep_installed(const fs::path& rel) {   // the player's files: never replaced by a package
    const std::string first = launcher_utf8(*rel.begin());
    return first == "hangtime.ini" || first == "saves" || first == "hangtime.log" || first == "game_build" || first == ".update"
        || first == "gamecontrollerdb_user.txt";
}
}  // namespace

std::string update_repo(const std::string& setting) { return setting.empty() ? HANGTIME_UPDATE_REPO : setting; }
std::string update_release_page(const std::string& repo) { return "https://github.com/" + repo + "/releases/latest"; }

bool update_version_newer(const std::string& latest, const std::string& current) {
    std::vector<long> a = version_parts(latest), b = version_parts(current);
    if (a.empty()) return false;   // not a version tag
    a.resize(std::max(a.size(), b.size())); b.resize(a.size());
    return a > b;
}

UpdateStatus update_check_now(const std::string& repo) {
    UpdateStatus r;
    if (!valid_repo(repo)) { r.state = UpdateStatus::Failed; r.error = "invalid repository name"; return r; }
    std::string body, error;
    const int code = http_get_string(api_base() + "/repos/" + repo + "/releases/latest", true, body, error);
    if (code == 404) { r.state = UpdateStatus::NoReleases; return r; }   // no published release yet (or the repository is not public)
    if (code != 200) {
        r.state = UpdateStatus::Failed;
        r.error = code ? "GitHub answered " + std::to_string(code) + (code == 403 || code == 429 ? " (rate limit; try later)" : "") : error;
        return r;
    }
    const nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
    if (!j.is_object() || !j.contains("tag_name") || !j["tag_name"].is_string()) { r.state = UpdateStatus::Failed; r.error = "unexpected answer from GitHub"; return r; }
    auto str = [](const nlohmann::json& o, const char* k) { return o.contains(k) && o[k].is_string() ? o[k].get<std::string>() : std::string(); };
    r.latest = str(j, "tag_name");
    r.page = str(j, "html_url");
    if (r.page.empty() || r.page.rfind("https://", 0) != 0) r.page = update_release_page(repo);
    r.notes = str(j, "body");
    if (j.contains("assets") && j["assets"].is_array())
        for (const auto& a : j["assets"]) {
            if (!a.is_object()) continue;
            const std::string name = str(a, "name"), url = str(a, "browser_download_url");
            if (name == "SHA256SUMS.txt") { r.sums_url = url; continue; }
            if (!r.asset.name.empty() || name.rfind(PACKAGE_PREFIX, 0) != 0 || !ends_with(name, PACKAGE_SUFFIX) || name.find("PERSONAL") != std::string::npos) continue;
            r.asset.name = name; r.asset.url = url;
            r.asset.size = a.contains("size") && a["size"].is_number_unsigned() ? a["size"].get<uint64_t>() : 0;
            const std::string digest = str(a, "digest");   // "sha256:<hex>"
            if (digest.rfind("sha256:", 0) == 0) r.asset.sha256 = digest.substr(7);
        }
    r.state = update_version_newer(r.latest, HANGTIME_VERSION) ? UpdateStatus::Available : UpdateStatus::UpToDate;
    return r;
}

void update_check_start(const std::string& repo) {
    {
        std::lock_guard<std::mutex> lock(status_mutex);
        if (status.state == UpdateStatus::Checking) return;
        status = {}; status.state = UpdateStatus::Checking;
    }
    std::thread([repo] {
        const UpdateStatus r = update_check_now(repo);
        fprintf(stderr, "UPDATE repo=%s state=%d latest=%s asset=%s %s\n", repo.c_str(), (int)r.state, r.latest.c_str(), r.asset.name.c_str(), r.error.c_str());
        std::lock_guard<std::mutex> lock(status_mutex);
        status = r;
    }).detach();   // a slow network never delays the launcher or the game; the result is just dropped at exit
}

UpdateStatus update_check_status() {
    std::lock_guard<std::mutex> lock(status_mutex);
    return status;
}

bool update_can_install(const UpdateStatus& release, std::string& why) {
    if (release.state != UpdateStatus::Available) { why = "no newer release"; return false; }
    if (release.asset.name.empty()) { why = "the release has no package for this system"; return false; }
    if (launcher_data_dir() != launcher_exe_dir()) { why = "the program folder is not writable"; return false; }
    return true;
}

UpdateInstallStatus update_install_now(const std::string& repo, const UpdateStatus& release) {
    auto fail = [](const std::string& e) { set_install(UpdateInstallStatus::Failed, 0, e); fprintf(stderr, "UPDATE install failed: %s\n", e.c_str()); return update_install_status(); };
    std::string why, error;
    if (!update_can_install(release, why)) return fail(why);
    const UpdateAsset& asset = release.asset;
    if (!download_allowed(repo, asset.url)) return fail("unexpected download address");
    const fs::path exe_dir = launcher_exe_dir(), work = exe_dir / ".update";
    std::error_code ec;
    fs::remove_all(work / "new", ec);
    fs::create_directories(work / "new", ec);
    const fs::path archive = work / ("download" + std::string(PACKAGE_SUFFIX));

    // 1. the checksum: GitHub's digest of the asset, else the release's SHA256SUMS.txt
    std::string expect = asset.sha256;
    if (expect.empty() && !release.sums_url.empty() && download_allowed(repo, release.sums_url)) {
        std::string sums;
        if (http_get_string(release.sums_url, false, sums, error) == 200) {
            size_t at = sums.find("  " + asset.name);
            if (at == std::string::npos) at = sums.find(" *" + asset.name);
            if (at != std::string::npos && at >= 64) expect = sums.substr(at - 64, 64);
        }
    }
    for (char& c : expect) c = (char)tolower((unsigned char)c);
    if (expect.size() != 64) return fail("the release publishes no SHA-256 checksum for " + asset.name);

    // 2. download
    fprintf(stderr, "UPDATE downloading %s (%llu bytes)\n", asset.url.c_str(), (unsigned long long)asset.size);
    set_install(UpdateInstallStatus::Downloading, 0);
    std::vector<uint8_t> data;
    data.reserve((size_t)asset.size);
    const int code = http_get(asset.url, false, [&](const char* d, size_t n) {
        data.insert(data.end(), d, d + n);
        if (asset.size) set_install(UpdateInstallStatus::Downloading, std::min(1.0f, (float)data.size() / (float)asset.size));
        return data.size() <= (size_t)2 << 30;
    }, error);
    if (code != 200) return fail(code ? "the download failed (HTTP " + std::to_string(code) + ")" : error);

    // 3. verify
    set_install(UpdateInstallStatus::Verifying, 1);
    const std::string got = launcher_sha256_hex(data);
    if (got != expect) return fail("the download is damaged (SHA-256 " + got.substr(0, 12) + "... instead of " + expect.substr(0, 12) + "...)");
    {
        std::ofstream f(archive, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(data.data()), (std::streamsize)data.size());
        if (!f) return fail("cannot write " + launcher_utf8(archive));
    }
    data.clear(); data.shrink_to_fit();

    // 4. unpack; the package is one folder holding the program
    set_install(UpdateInstallStatus::Extracting, 1);
    if (!extract(archive, work / "new", error)) return fail(error);
    fs::path root;
    const std::string exe_name = std::string("hangtime") + EXE;
    if (fs::exists(work / "new" / exe_name)) root = work / "new";
    else for (auto& e : fs::directory_iterator(work / "new", ec)) if (e.is_directory() && fs::exists(e.path() / exe_name)) { root = e.path(); break; }
    if (root.empty()) return fail("the package does not contain " + exe_name);

    // 5. replace the files; every replaced file is kept as *.old-update until the next start
    set_install(UpdateInstallStatus::Installing, 1);
    std::vector<std::pair<fs::path, fs::path>> moved;   // (destination, its backup or empty)
    std::vector<fs::path> retired;
    auto rollback = [&] {
        for (auto it = moved.rbegin(); it != moved.rend(); ++it) {
            std::error_code e2;
            fs::remove(it->first, e2);
            if (!it->second.empty()) fs::rename(it->second, it->first, e2);
        }
        for (const auto& r : retired) { std::error_code e2; fs::rename(fs::path(r.native() + fs::path(BACKUP_SUFFIX).native()), r, e2); }
    };
    if (kit_changed(root, exe_dir))   // the module must be generated again from the ROM by the new version
        for (const fs::path& m : {launcher_data_dir() / MODULE, exe_dir / MODULE}) {
            if (std::find(retired.begin(), retired.end(), m) != retired.end() || !fs::exists(m, ec)) continue;
            const fs::path backup = m.native() + fs::path(BACKUP_SUFFIX).native();
            fs::remove(backup, ec);
            fs::rename(m, backup, ec);
            if (ec) { rollback(); return fail("cannot replace " + launcher_utf8(m) + " (" + ec.message() + ")"); }
            retired.push_back(m);
            fprintf(stderr, "UPDATE the new version rebuilds the game module from the ROM (build kit changed)\n");
        }
    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_symlink() || it->is_regular_file()) files.push_back(fs::relative(it->path(), root));
    }
    for (const fs::path& rel : files) {
        if (keep_installed(rel)) continue;
        const fs::path src = root / rel, dest = exe_dir / rel;
        fs::create_directories(dest.parent_path(), ec);
        fs::path backup;
        if (fs::exists(fs::symlink_status(dest, ec))) {
            backup = dest.native() + fs::path(BACKUP_SUFFIX).native();
            fs::remove(backup, ec);
            fs::rename(dest, backup, ec);
            if (ec) { rollback(); return fail("cannot replace " + launcher_utf8(dest) + " (" + ec.message() + ")"); }
        }
        fs::rename(src, dest, ec);
        if (ec) { if (!backup.empty()) { std::error_code e2; fs::rename(backup, dest, e2); } rollback(); return fail("cannot install " + launcher_utf8(dest) + " (" + ec.message() + ")"); }
        moved.emplace_back(dest, backup);
    }
    {   // the backups to remove at the next start
        std::ofstream f(work / "cleanup.txt", std::ios::app);
        for (const auto& [dest, backup] : moved) if (!backup.empty()) f << launcher_utf8(backup) << "\n";
        for (const auto& r : retired) f << launcher_utf8(fs::path(r.native() + fs::path(BACKUP_SUFFIX).native())) << "\n";
    }
    fs::remove_all(work / "new", ec);
    fs::remove(archive, ec);
    fprintf(stderr, "UPDATE installed %s: %zu files\n", release.latest.c_str(), moved.size());
    set_install(UpdateInstallStatus::Done, 1);
    return update_install_status();
}

void update_install_start(const std::string& repo, const UpdateStatus& release) {
    {
        std::lock_guard<std::mutex> lock(status_mutex);
        const auto s = install_status.state;
        if (s != UpdateInstallStatus::Idle && s != UpdateInstallStatus::Failed) return;
        install_status = {}; install_status.state = UpdateInstallStatus::Downloading;
    }
    std::thread([repo, release] { update_install_now(repo, release); }).detach();
}

UpdateInstallStatus update_install_status() {
    std::lock_guard<std::mutex> lock(status_mutex);
    return install_status;
}

bool update_restart() {
#ifdef _WIN32
    SetEnvironmentVariableW(L"HANGTIME_LAUNCHER_SCRIPT", nullptr);   // a test script drives one launcher only
#else
    unsetenv("HANGTIME_LAUNCHER_SCRIPT");
#endif
    return launcher_restart({"--launcher"}, launcher_exe_dir() / (std::string("hangtime") + EXE));
}

void update_cleanup() {
    const fs::path work = launcher_exe_dir() / ".update", list = work / "cleanup.txt";
    std::error_code ec;
    if (!fs::exists(work, ec)) return;
    std::vector<std::string> left;
    {
        std::ifstream f(list);
        for (std::string line; std::getline(f, line);) {
            if (line.empty()) continue;
            const fs::path p = fs::u8path(line);
            if (!ends_with(line, BACKUP_SUFFIX)) continue;   // only the backups this program made
            fs::remove(p, ec);
            std::error_code e2;
            if (ec && fs::exists(p, e2)) left.push_back(line);   // still in use (the previous process is exiting)
        }
    }
    if (left.empty()) { fs::remove_all(work, ec); return; }
    std::ofstream f(list, std::ios::trunc);
    for (const auto& l : left) f << l << "\n";
}
