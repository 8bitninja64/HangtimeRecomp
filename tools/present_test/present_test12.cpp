// D3D12 variant of present_test: flip-model swap chain on a chosen adapter, clear + Present for
// N seconds (one frame in flight: fence wait after each frame). Reports presents/s and Present()
// call time.
// usage: present_test12.exe ADAPTER_INDEX SYNC_INTERVAL SECONDS [option...]
// options: waitable  buffers=N  tearing (ALLOW_TEARING swap chain + present flag when sync 0)
//          sequential (FLIP_SEQUENTIAL)  size=WxH  borderless (window covering the monitor)
//          rate=HZ (pace presents with a sleep, like a game at that rate)
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "winmm.lib")

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

int main(int argc, char** argv) {
    const int adapter_index = argc > 1 ? atoi(argv[1]) : 0, sync = argc > 2 ? atoi(argv[2]) : 0, secs = argc > 3 ? atoi(argv[3]) : 5;
    bool waitable = false, tearing = false, sequential = false, borderless = false;
    int buffers = 2, width = 960, height = 720, rate = 0;
    for (int i = 4; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "waitable" || a == "w") waitable = true;
        else if (a == "tearing") tearing = true;
        else if (a == "sequential") sequential = true;
        else if (a == "borderless") borderless = true;
        else if (a.rfind("buffers=", 0) == 0) buffers = atoi(a.c_str() + 8);
        else if (a.rfind("rate=", 0) == 0) rate = atoi(a.c_str() + 5);
        else if (a.rfind("size=", 0) == 0) sscanf(a.c_str() + 5, "%dx%d", &width, &height);
    }
    timeBeginPeriod(1);
    IDXGIFactory6* factory = nullptr; CreateDXGIFactory2(0, __uuidof(IDXGIFactory6), (void**)&factory);
    IDXGIAdapter1* adapter = nullptr; factory->EnumAdapters1(adapter_index, &adapter);
    DXGI_ADAPTER_DESC1 ad; adapter->GetDesc1(&ad);
    wprintf(L"adapter %d: %s\n", adapter_index, ad.Description);
    BOOL allow_tearing = FALSE;
    factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow_tearing, sizeof(allow_tearing));
    if (tearing && !allow_tearing) printf("tearing not supported by DXGI here\n");
    WNDCLASSW wc{}; wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"present_test12";
    RegisterClassW(&wc);
    HWND hwnd;
    if (borderless) {
        width = GetSystemMetrics(SM_CXSCREEN); height = GetSystemMetrics(SM_CYSCREEN);
        hwnd = CreateWindowExW(WS_EX_TOPMOST, L"present_test12", L"present_test12", WS_POPUP | WS_VISIBLE, 0, 0, width, height, nullptr, nullptr, wc.hInstance, nullptr);
    } else {
        hwnd = CreateWindowW(L"present_test12", L"present_test12", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, width, height, nullptr, nullptr, wc.hInstance, nullptr);
        RECT r; GetClientRect(hwnd, &r); width = r.right; height = r.bottom;
    }
    SetForegroundWindow(hwnd);
    ID3D12Device* dev = nullptr;
    if (FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&dev))) { printf("D3D12CreateDevice failed\n"); return 1; }
    D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* queue = nullptr; dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&queue);
    DXGI_SWAP_CHAIN_DESC1 sd{}; sd.Width = width; sd.Height = height; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = buffers;
    sd.SwapEffect = sequential ? DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL : DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (waitable) sd.Flags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    if (tearing && allow_tearing) sd.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    IDXGISwapChain1* sc1 = nullptr;
    HRESULT hr = factory->CreateSwapChainForHwnd(queue, hwnd, &sd, nullptr, nullptr, &sc1);
    if (FAILED(hr)) { printf("CreateSwapChainForHwnd failed 0x%08lX\n", hr); return 1; }
    IDXGISwapChain3* sc = nullptr; sc1->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&sc);
    HANDLE wait_obj = nullptr;
    if (waitable) { sc->SetMaximumFrameLatency(1); wait_obj = sc->GetFrameLatencyWaitableObject(); }
    D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; hd.NumDescriptors = buffers;
    ID3D12DescriptorHeap* heap = nullptr; dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&heap);
    const UINT inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    std::vector<ID3D12Resource*> bufs(buffers);
    for (int i = 0; i < buffers; ++i) {
        sc->GetBuffer(i, __uuidof(ID3D12Resource), (void**)&bufs[i]);
        D3D12_CPU_DESCRIPTOR_HANDLE h = heap->GetCPUDescriptorHandleForHeapStart(); h.ptr += i * inc;
        dev->CreateRenderTargetView(bufs[i], nullptr, h);
    }
    ID3D12CommandAllocator* alloc = nullptr; dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&alloc);
    ID3D12GraphicsCommandList* list = nullptr; dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&list);
    list->Close();
    ID3D12Fence* fence = nullptr; dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&fence);
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    UINT64 fv = 0;
    std::vector<double> present_ms;
    const auto t0 = std::chrono::steady_clock::now();
    int frame = 0;
    const UINT present_flags = (tearing && allow_tearing && sync == 0) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(secs)) {
        MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
        if (rate > 0) std::this_thread::sleep_until(t0 + std::chrono::microseconds((long long)frame * 1000000 / rate));
        if (wait_obj) WaitForSingleObject(wait_obj, 1000);
        const UINT bi = sc->GetCurrentBackBufferIndex();
        alloc->Reset(); list->Reset(alloc, nullptr);
        D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = bufs[bi];
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT; b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        list->ResourceBarrier(1, &b);
        D3D12_CPU_DESCRIPTOR_HANDLE h = heap->GetCPUDescriptorHandleForHeapStart(); h.ptr += bi * inc;
        float c[4] = {(frame & 1) ? 1.0f : 0.0f, 0.2f, 0.4f, 1.0f};
        list->ClearRenderTargetView(h, c, 0, nullptr);
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        list->ResourceBarrier(1, &b);
        list->Close();
        ID3D12CommandList* lists[] = {list}; queue->ExecuteCommandLists(1, lists);
        const auto p0 = std::chrono::steady_clock::now();
        sc->Present(sync, present_flags);
        present_ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - p0).count());
        queue->Signal(fence, ++fv);
        if (fence->GetCompletedValue() < fv) { fence->SetEventOnCompletion(fv, ev); WaitForSingleObject(ev, INFINITE); }
        ++frame;
    }
    const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::sort(present_ms.begin(), present_ms.end());
    auto pct = [&](double p) { return present_ms[(size_t)((present_ms.size() - 1) * p)]; };
    printf("d3d12 adapter=%d sync=%d waitable=%d buffers=%d tearing=%d sequential=%d borderless=%d size=%dx%d rate=%d foreground=%d presents/s=%.1f present_ms p50=%.2f p95=%.2f max=%.2f\n",
           adapter_index, sync, (int)waitable, buffers, (int)(tearing && allow_tearing), (int)sequential, (int)borderless, width, height, rate,
           GetForegroundWindow() == hwnd, frame / total, pct(0.5), pct(0.95), present_ms.back());
    return 0;
}
