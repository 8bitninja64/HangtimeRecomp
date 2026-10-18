// Minimal present-rate test: a 960x720 window, D3D11 flip-model swap chain on a chosen adapter,
// clear + Present for N seconds. Reports presents/s and Present() call time percentiles.
// usage: present_test.exe ADAPTER_INDEX SYNC_INTERVAL SECONDS [waitable]
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

int main(int argc, char** argv) {
    const int adapter_index = argc > 1 ? atoi(argv[1]) : 0, sync = argc > 2 ? atoi(argv[2]) : 0, secs = argc > 3 ? atoi(argv[3]) : 5;
    const bool waitable = argc > 4;
    IDXGIFactory6* factory = nullptr; CreateDXGIFactory1(__uuidof(IDXGIFactory6), (void**)&factory);
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d; adapter->GetDesc1(&d);
        wprintf(L"adapter %u: %s%s\n", i, d.Description, (int)i == adapter_index ? L"  <- used" : L"");
        adapter->Release();
    }
    factory->EnumAdapters1(adapter_index, &adapter);
    WNDCLASSW wc{}; wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"present_test";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(L"present_test", L"present_test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 960, 720, nullptr, nullptr, wc.hInstance, nullptr);
    SetForegroundWindow(hwnd);
    ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
    D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
    DXGI_SWAP_CHAIN_DESC1 sd{}; sd.Width = 960; sd.Height = 720; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (waitable) sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    IDXGISwapChain1* sc1 = nullptr;
    HRESULT hr = factory->CreateSwapChainForHwnd(dev, hwnd, &sd, nullptr, nullptr, &sc1);
    if (FAILED(hr)) { printf("CreateSwapChainForHwnd failed 0x%08lX\n", hr); return 1; }
    IDXGISwapChain2* sc2 = nullptr; HANDLE wait_obj = nullptr;
    if (waitable && SUCCEEDED(sc1->QueryInterface(__uuidof(IDXGISwapChain2), (void**)&sc2))) { sc2->SetMaximumFrameLatency(1); wait_obj = sc2->GetFrameLatencyWaitableObject(); }
    ID3D11Texture2D* bb = nullptr; sc1->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
    ID3D11RenderTargetView* rtv = nullptr; dev->CreateRenderTargetView(bb, nullptr, &rtv);
    std::vector<double> present_ms;
    const auto t0 = std::chrono::steady_clock::now();
    int frame = 0;
    while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(secs)) {
        MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
        if (wait_obj) WaitForSingleObject(wait_obj, 1000);
        float c[4] = {(frame & 1) ? 1.0f : 0.0f, 0.2f, 0.4f, 1.0f};
        ctx->OMSetRenderTargets(1, &rtv, nullptr); ctx->ClearRenderTargetView(rtv, c);
        const auto p0 = std::chrono::steady_clock::now();
        sc1->Present(sync, 0);
        present_ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - p0).count());
        ++frame;
    }
    const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::sort(present_ms.begin(), present_ms.end());
    auto pct = [&](double p) { return present_ms[(size_t)((present_ms.size() - 1) * p)]; };
    printf("adapter=%d sync=%d waitable=%d foreground=%d presents/s=%.1f present_ms p50=%.2f p95=%.2f max=%.2f\n", adapter_index, sync, (int)waitable,
           GetForegroundWindow() == hwnd, frame / total, pct(0.5), pct(0.95), present_ms.back());
    return 0;
}
