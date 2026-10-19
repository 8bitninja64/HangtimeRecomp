// Update check (update_check.hpp). HTTPS through the operating system, no bundled TLS library:
// WinHTTP on Windows; on Linux the system's curl executable (present on SteamOS and common
// distributions; without it the check reports that it could not run).
#include "update_check.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>
#include "branding.hpp"
#include "launcher.hpp"
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

namespace {
// Never destroyed: a check still running when the program exits must not touch freed statics.
std::mutex& status_mutex = *new std::mutex;
UpdateStatus& status = *new UpdateStatus;

bool valid_repo(const std::string& r) {   // owner/name; also keeps the curl command line literal
    if (r.empty() || r.size() > 200 || r.find('/') == std::string::npos || r.find('/') != r.rfind('/')) return false;
    for (char c : r) if (!isalnum((unsigned char)c) && c != '-' && c != '_' && c != '.' && c != '/') return false;
    return r.front() != '/' && r.back() != '/' && r.find("..") == std::string::npos;
}

std::string user_agent() { return std::string(HANGTIME_PRODUCT_NAME "/") + HANGTIME_VERSION; }

// GET https://api.github.com/<path>; returns the HTTP status (0: no connection) and the body.
int https_get(const std::string& path, std::string& body, std::string& error) {
#ifdef _WIN32
    auto wide = [](const std::string& s) { return std::wstring(s.begin(), s.end()); };   // ASCII only (valid_repo)
    int code = 0;
    HINTERNET session = WinHttpOpen(wide(user_agent()).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) session = WinHttpOpen(wide(user_agent()).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET connect = session ? WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0) : nullptr;
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", wide("/" + path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    if (request) {
        WinHttpSetTimeouts(request, 8000, 8000, 8000, 8000);
        const wchar_t* headers = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
        if (WinHttpSendRequest(request, headers, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr)) {
            DWORD sc = 0, len = sizeof(sc);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &sc, &len, WINHTTP_NO_HEADER_INDEX);
            code = (int)sc;
            for (DWORD avail = 0; WinHttpQueryDataAvailable(request, &avail) && avail && body.size() < (1u << 20);) {
                std::vector<char> buf(avail); DWORD got = 0;
                if (!WinHttpReadData(request, buf.data(), avail, &got) || !got) break;
                body.append(buf.data(), got);
            }
        }
    }
    if (!code) error = "no connection to github.com (error " + std::to_string(GetLastError()) + ")";
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
    return code;
#else
    // -w appends the status code on its own line; stderr is discarded (the error is reported below).
    const std::string cmd = "curl -sL --max-time 10 -A '" + user_agent() + "' -H 'Accept: application/vnd.github+json' "
                            "-H 'X-GitHub-Api-Version: 2022-11-28' -w '\\n%{http_code}' 'https://api.github.com/" + path + "' 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) { error = "could not run curl"; return 0; }
    char buf[4096];
    for (size_t n; (n = fread(buf, 1, sizeof(buf), p)) > 0 && body.size() < (1u << 20);) body.append(buf, n);
    const int rc = pclose(p);
    const size_t nl = body.rfind('\n');
    const int code = nl == std::string::npos ? 0 : atoi(body.c_str() + nl + 1);
    if (nl != std::string::npos) body.resize(nl);
    if (!code) error = rc == 127 * 256 || rc == 127 ? "curl is not installed" : "no connection to github.com";
    return code;
#endif
}

// The string value of "key" in a JSON object (first occurrence, simple escapes).
std::string json_string(const std::string& json, const std::string& key) {
    size_t i = json.find("\"" + key + "\"");
    if (i == std::string::npos) return {};
    i = json.find(':', i);
    if (i == std::string::npos) return {};
    while (++i < json.size() && isspace((unsigned char)json[i])) {}
    if (i >= json.size() || json[i] != '"') return {};
    std::string out;
    for (++i; i < json.size() && json[i] != '"'; ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) ++i;
        out += json[i];
    }
    return out;
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
    const int code = https_get("repos/" + repo + "/releases/latest", body, error);
    if (code == 200) {
        r.latest = json_string(body, "tag_name");
        if (r.latest.empty()) { r.state = UpdateStatus::Failed; r.error = "unexpected answer from GitHub"; }
        else r.state = update_version_newer(r.latest, HANGTIME_VERSION) ? UpdateStatus::Available : UpdateStatus::UpToDate;
    } else if (code == 404) {
        r.state = UpdateStatus::NoReleases;   // no published release yet (or the repository is not public)
    } else {
        r.state = UpdateStatus::Failed;
        r.error = code ? "GitHub answered " + std::to_string(code) + (code == 403 || code == 429 ? " (rate limit; try later)" : "") : error;
    }
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
        fprintf(stderr, "UPDATE repo=%s state=%d latest=%s %s\n", repo.c_str(), (int)r.state, r.latest.c_str(), r.error.c_str());
        std::lock_guard<std::mutex> lock(status_mutex);
        status = r;
    }).detach();   // a slow network never delays the launcher or the game; the result is just dropped at exit
}

UpdateStatus update_check_status() {
    std::lock_guard<std::mutex> lock(status_mutex);
    return status;
}
