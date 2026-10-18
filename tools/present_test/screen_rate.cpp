// What actually reaches the screen: Desktop Duplication of the output showing a window, counting
// the desktop images the compositor produced whose dirty rectangles touch that window.
// usage: screen_rate.exe "WINDOW TITLE PREFIX" SECONDS
// Reports window updates/s (unique composed images that changed the window), the interval
// percentiles between them, and all desktop images/s. A window whose content changes every frame
// (a game) shows one update per frame that reached the screen.
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

static HWND target = nullptr;
static std::wstring prefix;
static BOOL CALLBACK find(HWND h, LPARAM) {
    wchar_t t[256]; GetWindowTextW(h, t, 256);
    if (IsWindowVisible(h) && wcsncmp(t, prefix.c_str(), prefix.size()) == 0) { target = h; return FALSE; }
    return TRUE;
}

int main(int argc, char** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc < 3) { printf("usage: screen_rate.exe TITLE_PREFIX SECONDS\n"); return 2; }
    const int secs = atoi(argv[2]);
    prefix.assign(argv[1], argv[1] + strlen(argv[1]));
    for (int i = 0; i < 100 && !target; ++i) { EnumWindows(find, 0); if (!target) Sleep(100); }
    if (!target) { printf("window not found\n"); return 1; }
    RECT wr; GetWindowRect(target, &wr);
    const HMONITOR mon = MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST);
    IDXGIFactory1* factory = nullptr; CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory);
    IDXGIAdapter1* adapter = nullptr; IDXGIOutput* output = nullptr; DXGI_OUTPUT_DESC od{};
    for (UINT a = 0; !output && factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
        IDXGIOutput* o = nullptr;
        for (UINT i = 0; adapter->EnumOutputs(i, &o) != DXGI_ERROR_NOT_FOUND; ++i) {
            o->GetDesc(&od);
            if (od.Monitor == mon) { output = o; break; }
            o->Release();
        }
        if (!output) adapter->Release();
    }
    if (!output) { printf("output not found\n"); return 1; }
    DXGI_ADAPTER_DESC1 ad; adapter->GetDesc1(&ad);
    ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
    D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
    IDXGIOutput1* out1 = nullptr; output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&out1);
    IDXGIOutputDuplication* dup = nullptr;
    HRESULT hr = out1->DuplicateOutput(dev, &dup);
    if (FAILED(hr)) { printf("DuplicateOutput failed 0x%08lX\n", hr); return 1; }
    // Window rect relative to the output.
    RECT rel{wr.left - od.DesktopCoordinates.left, wr.top - od.DesktopCoordinates.top, wr.right - od.DesktopCoordinates.left, wr.bottom - od.DesktopCoordinates.top};
    std::vector<double> times; int images = 0;
    std::vector<BYTE> buf(1 << 16);
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(secs)) {
        DXGI_OUTDUPL_FRAME_INFO info{}; IDXGIResource* res = nullptr;
        hr = dup->AcquireNextFrame(100, &info, &res);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) continue;
        if (FAILED(hr)) { printf("AcquireNextFrame failed 0x%08lX\n", hr); break; }
        if (info.LastPresentTime.QuadPart) {
            ++images;
            UINT size = 0; bool touches = false;
            if (info.TotalMetadataBufferSize) {
                if (buf.size() < info.TotalMetadataBufferSize) buf.resize(info.TotalMetadataBufferSize);
                if (SUCCEEDED(dup->GetFrameDirtyRects((UINT)buf.size(), (RECT*)buf.data(), &size))) {
                    for (UINT i = 0; i < size / sizeof(RECT); ++i) {
                        const RECT& r = ((RECT*)buf.data())[i];
                        if (r.left < rel.right && r.right > rel.left && r.top < rel.bottom && r.bottom > rel.top) { touches = true; break; }
                    }
                }
            }
            if (touches) times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        if (res) res->Release();
        dup->ReleaseFrame();
    }
    const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::vector<double> iv;
    for (size_t i = 1; i < times.size(); ++i) iv.push_back(times[i] - times[i - 1]);
    std::sort(iv.begin(), iv.end());
    auto pct = [&](double p) { return iv.empty() ? 0.0 : iv[(size_t)((iv.size() - 1) * p)]; };
    wprintf(L"output on %s\n", ad.Description);
    printf("window_updates/s=%.1f interval_ms p50=%.2f p95=%.2f max=%.2f desktop_images/s=%.1f\n",
           times.size() / total, pct(0.5), pct(0.95), iv.empty() ? 0.0 : iv.back(), images / total);
    return 0;
}
