// Render GPU choice (render_gpu.hpp).
#include "render_gpu.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

static std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }

std::vector<RenderGpu> render_gpu_list() {
    std::vector<RenderGpu> out;
#ifdef _WIN32
    // Display adapters (device class GUID_DEVCLASS_DISPLAY) that register a Vulkan driver.
    const char* cls = "SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}";
    HKEY root;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, cls, 0, KEY_READ, &root) != ERROR_SUCCESS) return out;
    char sub[256];
    for (DWORD i = 0; ; ++i) {
        DWORD len = sizeof(sub);
        if (RegEnumKeyExA(root, i, sub, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        char desc[256] = {}, drv[1024] = {};
        DWORD dl = sizeof(desc), vl = sizeof(drv);
        if (RegGetValueA(root, sub, "DriverDesc", RRF_RT_REG_SZ, nullptr, desc, &dl) != ERROR_SUCCESS) continue;
        if (RegGetValueA(root, sub, "VulkanDriverName", RRF_RT_REG_SZ | RRF_RT_REG_MULTI_SZ, nullptr, drv, &vl) != ERROR_SUCCESS || !drv[0]) continue;
        out.push_back({desc, fs::path(drv).filename().string()});
    }
    RegCloseKey(root);
#else
    // Vulkan driver manifests (the loader's standard search folders); the name is the file stem.
    std::vector<fs::path> dirs = {"/usr/share/vulkan/icd.d", "/etc/vulkan/icd.d", "/usr/local/share/vulkan/icd.d"};
    if (const char* h = getenv("HOME")) dirs.push_back(fs::path(h) / ".local/share/vulkan/icd.d");
    std::error_code ec;
    for (const auto& d : dirs)
        for (const auto& e : fs::directory_iterator(d, ec))
            if (e.path().extension() == ".json") out.push_back({e.path().stem().string(), e.path().filename().string()});
#endif
    return out;
}

bool render_gpu_apply(const std::string& choice, std::string& log) {
    if (choice.empty() || lower(choice) == "auto") { log = "automatic"; return false; }
    if (const char* set = getenv("VK_LOADER_DRIVERS_SELECT"); set && *set) {
        log = std::string("Vulkan, VK_LOADER_DRIVERS_SELECT already set to ") + set;
        return true;
    }
    for (const auto& g : render_gpu_list()) {
        if (lower(g.name).find(lower(choice)) == std::string::npos) continue;
        const std::string glob = "*" + g.driver;
#ifdef _WIN32
        SetEnvironmentVariableA("VK_LOADER_DRIVERS_SELECT", glob.c_str());
        _putenv_s("VK_LOADER_DRIVERS_SELECT", glob.c_str());
#else
        setenv("VK_LOADER_DRIVERS_SELECT", glob.c_str(), 1);
#endif
        log = "Vulkan on \"" + g.name + "\" (driver " + g.driver + ")";
        return true;
    }
    log = "\"" + choice + "\" not found on this system: automatic";
    return false;
}
