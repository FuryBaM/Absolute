// D3D12 backend for Desktop.Gpu (Windows).
// Lifecycle: create / beginFrame / clear / endFrame / present.
// Mesh RHI (shaders, buffers, draw) still OpenGL / D3D11.

#if !defined(_WIN32)

#include <cstdint>
#include <string>

namespace {
    thread_local std::string g_lastError = "D3D12 only on Windows";
}

extern "C" int64_t absolute_desktop_gpu_d3d12_create(int64_t) { return 0; }
extern "C" void absolute_desktop_gpu_d3d12_destroy(int64_t) {}
extern "C" int32_t absolute_desktop_gpu_d3d12_is_valid(int64_t) { return 0; }
extern "C" const char* absolute_desktop_gpu_d3d12_backend() { return "none"; }
extern "C" const char* absolute_desktop_gpu_d3d12_last_error() { return g_lastError.c_str(); }
extern "C" void absolute_desktop_gpu_d3d12_begin_frame(int64_t) {}
extern "C" void absolute_desktop_gpu_d3d12_end_frame(int64_t) {}
extern "C" void absolute_desktop_gpu_d3d12_clear(int64_t, float, float, float, float) {}
extern "C" void absolute_desktop_gpu_d3d12_present(int64_t) {}
extern "C" void absolute_desktop_gpu_d3d12_unsupported(const char* what) {
    g_lastError = std::string("D3D12 backend: ") + (what ? what : "unsupported")
        + " (clear/present only; use OpenGL or D3D11 for mesh RHI)";
}

#else

#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "desktop_gpu_magic.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

extern "C" void* absolute_desktop_native_window(int64_t handle);
extern "C" int32_t absolute_desktop_width(int64_t handle);
extern "C" int32_t absolute_desktop_height(int64_t handle);

using Microsoft::WRL::ComPtr;

namespace {
    thread_local std::string g_lastError;

    void SetError(std::string message) {
        g_lastError = std::move(message);
    }

    constexpr UINT kFrameCount = 2;

    struct D3D12Device {
        uint32_t magic = kAbsoluteGpuMagicD3D12;
        int64_t windowHandle = 0;
        HWND hwnd = nullptr;

        ComPtr<ID3D12Device> device;
        ComPtr<ID3D12CommandQueue> queue;
        ComPtr<IDXGISwapChain3> swap;
        ComPtr<ID3D12DescriptorHeap> rtvHeap;
        ComPtr<ID3D12Resource> renderTargets[kFrameCount];
        ComPtr<ID3D12CommandAllocator> allocators[kFrameCount];
        ComPtr<ID3D12GraphicsCommandList> cmdList;
        ComPtr<ID3D12Fence> fence;
        HANDLE fenceEvent = nullptr;

        UINT rtvDescriptorSize = 0;
        UINT frameIndex = 0;
        UINT64 fenceValues[kFrameCount] = {};
        UINT64 fenceValue = 0;

        bool valid = false;
        bool inFrame = false;
        bool cmdOpen = false;
        float clearColor[4] = {0.06f, 0.07f, 0.12f, 1.0f};
        int32_t backbufferW = 0;
        int32_t backbufferH = 0;

        D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle(UINT index) const {
            D3D12_CPU_DESCRIPTOR_HANDLE h = rtvHeap->GetCPUDescriptorHandleForHeapStart();
            h.ptr += static_cast<SIZE_T>(index) * static_cast<SIZE_T>(rtvDescriptorSize);
            return h;
        }

        void WaitFrame(UINT index) {
            if (!fence || !fenceEvent) return;
            const UINT64 value = fenceValues[index];
            if (fence->GetCompletedValue() < value) {
                fence->SetEventOnCompletion(value, fenceEvent);
                WaitForSingleObject(fenceEvent, INFINITE);
            }
        }

        void WaitIdle() {
            if (!queue || !fence || !fenceEvent) return;
            ++fenceValue;
            queue->Signal(fence.Get(), fenceValue);
            if (fence->GetCompletedValue() < fenceValue) {
                fence->SetEventOnCompletion(fenceValue, fenceEvent);
                WaitForSingleObject(fenceEvent, INFINITE);
            }
        }

        void ReleaseRenderTargets() {
            for (UINT i = 0; i < kFrameCount; ++i) {
                renderTargets[i].Reset();
            }
        }

        bool CreateRenderTargets() {
            ReleaseRenderTargets();
            if (!swap || !device || !rtvHeap) return false;
            for (UINT i = 0; i < kFrameCount; ++i) {
                HRESULT hr = swap->GetBuffer(i, IID_PPV_ARGS(&renderTargets[i]));
                if (FAILED(hr) || !renderTargets[i]) {
                    SetError("D3D12 GetBuffer failed");
                    return false;
                }
                device->CreateRenderTargetView(renderTargets[i].Get(), nullptr, RtvHandle(i));
            }
            frameIndex = swap->GetCurrentBackBufferIndex();
            return true;
        }

        bool ResizeIfNeeded() {
            if (!swap || !device) return true;
            const int32_t w = absolute_desktop_width(windowHandle);
            const int32_t h = absolute_desktop_height(windowHandle);
            if (w <= 0 || h <= 0) return true;
            if (w == backbufferW && h == backbufferH) return true;

            WaitIdle();
            ReleaseRenderTargets();

            HRESULT hr = swap->ResizeBuffers(
                kFrameCount,
                static_cast<UINT>(w),
                static_cast<UINT>(h),
                DXGI_FORMAT_R8G8B8A8_UNORM,
                0);
            if (FAILED(hr)) {
                SetError("D3D12 ResizeBuffers failed");
                return false;
            }
            backbufferW = w;
            backbufferH = h;
            return CreateRenderTargets();
        }

        void Destroy() {
            if (valid) WaitIdle();
            if (cmdOpen && cmdList) {
                // Best effort — list may already be closed.
                cmdOpen = false;
            }
            ReleaseRenderTargets();
            cmdList.Reset();
            for (UINT i = 0; i < kFrameCount; ++i) {
                allocators[i].Reset();
                fenceValues[i] = 0;
            }
            rtvHeap.Reset();
            swap.Reset();
            queue.Reset();
            fence.Reset();
            if (fenceEvent) {
                CloseHandle(fenceEvent);
                fenceEvent = nullptr;
            }
            device.Reset();
            hwnd = nullptr;
            valid = false;
            inFrame = false;
            cmdOpen = false;
        }
    };

    D3D12Device* FromHandle(int64_t handle) {
        if (!AbsoluteGpuIsD3D12(handle)) return nullptr;
        return reinterpret_cast<D3D12Device*>(static_cast<intptr_t>(handle));
    }

    int64_t ToHandle(D3D12Device* dev) {
        return static_cast<int64_t>(reinterpret_cast<intptr_t>(dev));
    }

    void Transition(
        ID3D12GraphicsCommandList* list,
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        b.Transition.pResource = resource;
        b.Transition.StateBefore = before;
        b.Transition.StateAfter = after;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &b);
    }
}

extern "C" int64_t absolute_desktop_gpu_d3d12_create(int64_t windowHandle) {
    g_lastError.clear();
    HWND hwnd = static_cast<HWND>(absolute_desktop_native_window(windowHandle));
    if (!hwnd) {
        SetError("D3D12: window has no HWND");
        return 0;
    }

    int32_t width = absolute_desktop_width(windowHandle);
    int32_t height = absolute_desktop_height(windowHandle);
    if (width <= 0) width = 1;
    if (height <= 0) height = 1;

    auto* dev = new D3D12Device();
    dev->windowHandle = windowHandle;
    dev->hwnd = hwnd;
    dev->backbufferW = width;
    dev->backbufferH = height;

#if defined(_DEBUG)
    {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
        }
    }
#endif

    ComPtr<IDXGIFactory4> factory;
    UINT factoryFlags = 0;
#if defined(_DEBUG)
    factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
    HRESULT hr = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    }
    if (FAILED(hr) || !factory) {
        SetError("CreateDXGIFactory2 failed");
        delete dev;
        return 0;
    }

    // Prefer hardware adapter that supports D3D12 feature level 11_0+.
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            adapter.Reset();
            continue;
        }
        if (SUCCEEDED(D3D12CreateDevice(
                adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) {
            break;
        }
        adapter.Reset();
    }

    if (adapter) {
        hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev->device));
    } else {
        // WARP fallback.
        ComPtr<IDXGIAdapter> warp;
        factory->EnumWarpAdapter(IID_PPV_ARGS(&warp));
        hr = D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev->device));
    }
    if (FAILED(hr) || !dev->device) {
        SetError("D3D12CreateDevice failed");
        delete dev;
        return 0;
    }

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = dev->device->CreateCommandQueue(&qd, IID_PPV_ARGS(&dev->queue));
    if (FAILED(hr) || !dev->queue) {
        SetError("CreateCommandQueue failed");
        delete dev;
        return 0;
    }

    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width = static_cast<UINT>(width);
    scd.Height = static_cast<UINT>(height);
    scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = kFrameCount;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.Flags = 0;

    ComPtr<IDXGISwapChain1> swap1;
    hr = factory->CreateSwapChainForHwnd(
        dev->queue.Get(), hwnd, &scd, nullptr, nullptr, &swap1);
    if (FAILED(hr) || !swap1) {
        SetError("CreateSwapChainForHwnd failed");
        delete dev;
        return 0;
    }
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    hr = swap1.As(&dev->swap);
    if (FAILED(hr) || !dev->swap) {
        SetError("QueryInterface IDXGISwapChain3 failed");
        delete dev;
        return 0;
    }

    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.NumDescriptors = kFrameCount;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    hr = dev->device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&dev->rtvHeap));
    if (FAILED(hr) || !dev->rtvHeap) {
        SetError("CreateDescriptorHeap (RTV) failed");
        delete dev;
        return 0;
    }
    dev->rtvDescriptorSize =
        dev->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    if (!dev->CreateRenderTargets()) {
        delete dev;
        return 0;
    }

    for (UINT i = 0; i < kFrameCount; ++i) {
        hr = dev->device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&dev->allocators[i]));
        if (FAILED(hr) || !dev->allocators[i]) {
            SetError("CreateCommandAllocator failed");
            delete dev;
            return 0;
        }
    }

    hr = dev->device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        dev->allocators[0].Get(),
        nullptr,
        IID_PPV_ARGS(&dev->cmdList));
    if (FAILED(hr) || !dev->cmdList) {
        SetError("CreateCommandList failed");
        delete dev;
        return 0;
    }
    // Start closed; begin_frame will Reset.
    dev->cmdList->Close();
    dev->cmdOpen = false;

    hr = dev->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&dev->fence));
    if (FAILED(hr) || !dev->fence) {
        SetError("CreateFence failed");
        delete dev;
        return 0;
    }
    dev->fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!dev->fenceEvent) {
        SetError("CreateEvent failed");
        delete dev;
        return 0;
    }

    dev->valid = true;
    return ToHandle(dev);
}

extern "C" void absolute_desktop_gpu_d3d12_destroy(int64_t handle) {
    D3D12Device* dev = FromHandle(handle);
    if (!dev) return;
    dev->Destroy();
    delete dev;
}

extern "C" int32_t absolute_desktop_gpu_d3d12_is_valid(int64_t handle) {
    const D3D12Device* dev = FromHandle(handle);
    return dev && dev->valid ? 1 : 0;
}

extern "C" const char* absolute_desktop_gpu_d3d12_backend() {
    return "d3d12";
}

extern "C" const char* absolute_desktop_gpu_d3d12_last_error() {
    return g_lastError.c_str();
}

extern "C" void absolute_desktop_gpu_d3d12_begin_frame(int64_t handle) {
    D3D12Device* dev = FromHandle(handle);
    if (!dev || !dev->valid || !dev->cmdList) return;

    if (!dev->ResizeIfNeeded()) return;

    dev->frameIndex = dev->swap->GetCurrentBackBufferIndex();
    dev->WaitFrame(dev->frameIndex);

    HRESULT hr = dev->allocators[dev->frameIndex]->Reset();
    if (FAILED(hr)) {
        SetError("CommandAllocator::Reset failed");
        return;
    }
    hr = dev->cmdList->Reset(dev->allocators[dev->frameIndex].Get(), nullptr);
    if (FAILED(hr)) {
        SetError("CommandList::Reset failed");
        return;
    }
    dev->cmdOpen = true;

    Transition(
        dev->cmdList.Get(),
        dev->renderTargets[dev->frameIndex].Get(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);

    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = dev->RtvHandle(dev->frameIndex);
    dev->cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    D3D12_VIEWPORT vp{};
    vp.Width = static_cast<float>(dev->backbufferW > 0 ? dev->backbufferW : 1);
    vp.Height = static_cast<float>(dev->backbufferH > 0 ? dev->backbufferH : 1);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    dev->cmdList->RSSetViewports(1, &vp);

    D3D12_RECT scissor{};
    scissor.right = dev->backbufferW > 0 ? dev->backbufferW : 1;
    scissor.bottom = dev->backbufferH > 0 ? dev->backbufferH : 1;
    dev->cmdList->RSSetScissorRects(1, &scissor);

    dev->inFrame = true;
}

extern "C" void absolute_desktop_gpu_d3d12_clear(
    int64_t handle, float r, float g, float b, float a) {
    D3D12Device* dev = FromHandle(handle);
    if (!dev || !dev->valid || !dev->cmdOpen || !dev->cmdList) return;
    dev->clearColor[0] = r;
    dev->clearColor[1] = g;
    dev->clearColor[2] = b;
    dev->clearColor[3] = a;
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = dev->RtvHandle(dev->frameIndex);
    dev->cmdList->ClearRenderTargetView(rtv, dev->clearColor, 0, nullptr);
}

extern "C" void absolute_desktop_gpu_d3d12_end_frame(int64_t handle) {
    D3D12Device* dev = FromHandle(handle);
    if (!dev || !dev->valid || !dev->cmdOpen || !dev->cmdList) return;

    Transition(
        dev->cmdList.Get(),
        dev->renderTargets[dev->frameIndex].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);

    HRESULT hr = dev->cmdList->Close();
    dev->cmdOpen = false;
    if (FAILED(hr)) {
        SetError("CommandList::Close failed");
        dev->inFrame = false;
        return;
    }

    ID3D12CommandList* lists[] = {dev->cmdList.Get()};
    dev->queue->ExecuteCommandLists(1, lists);
    dev->inFrame = false;
}

extern "C" void absolute_desktop_gpu_d3d12_present(int64_t handle) {
    D3D12Device* dev = FromHandle(handle);
    if (!dev || !dev->valid || !dev->swap || !dev->queue || !dev->fence) return;

    // If user skipped endFrame, close/execute so Present is valid.
    if (dev->cmdOpen) {
        absolute_desktop_gpu_d3d12_end_frame(handle);
    }

    HRESULT hr = dev->swap->Present(1, 0);
    if (FAILED(hr)) {
        SetError("IDXGISwapChain::Present failed");
        return;
    }

    const UINT64 signal = ++dev->fenceValue;
    dev->fenceValues[dev->frameIndex] = signal;
    dev->queue->Signal(dev->fence.Get(), signal);
    dev->frameIndex = dev->swap->GetCurrentBackBufferIndex();
}

extern "C" void absolute_desktop_gpu_d3d12_unsupported(const char* what) {
    SetError(std::string("D3D12 backend: ") + (what ? what : "unsupported")
        + " (clear/present only; use OpenGL or D3D11 for mesh RHI)");
}

#endif
