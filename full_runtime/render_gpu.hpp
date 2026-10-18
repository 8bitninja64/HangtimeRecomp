#pragma once
// Per-system render GPU choice (hangtime.ini `render_gpu=`, launcher Graphics page, --render-gpu).
// RT64 itself always takes the GPU with the most memory; a choice here works below RT64: the
// Vulkan loader is told to load only the chosen GPU's Vulkan driver (VK_LOADER_DRIVERS_SELECT)
// and RT64 runs on Vulkan. Empty choice = automatic = unchanged behaviour (D3D12 on Windows).
// Use case: hybrid laptops whose screen is driven by the integrated GPU (PERFORMANCE.md).
#include <string>
#include <vector>

struct RenderGpu {
    std::string name;      // adapter name (Windows) or Vulkan driver manifest name (Linux)
    std::string driver;    // Vulkan driver manifest file name, e.g. igvk64.json
};
std::vector<RenderGpu> render_gpu_list();
// Applies a choice before the renderer starts: true when a GPU matched (case-insensitive
// substring of its name) and the Vulkan loader was restricted to its driver; `log` says what
// happened. Leaves an already set VK_LOADER_DRIVERS_SELECT alone.
bool render_gpu_apply(const std::string& choice, std::string& log);
