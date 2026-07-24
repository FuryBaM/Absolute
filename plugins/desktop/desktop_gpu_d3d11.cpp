// D3D11 backend for Desktop.Gpu (Windows).
// Supports create/clear/present lifecycle. Full mesh RHI still OpenGL-primary;
// resource create APIs return 0 with lastError when called on a D3D11 device.

#if !defined(_WIN32)

#include <string>

namespace {
    thread_local std::string g_lastError = "D3D11 only on Windows";
}

extern "C" int64_t absolute_desktop_gpu_d3d11_create(int64_t) { return 0; }
extern "C" void absolute_desktop_gpu_d3d11_destroy(int64_t) {}
extern "C" int32_t absolute_desktop_gpu_d3d11_is_valid(int64_t) { return 0; }
extern "C" const char* absolute_desktop_gpu_d3d11_backend() { return "none"; }
extern "C" const char* absolute_desktop_gpu_d3d11_last_error() { return g_lastError.c_str(); }
extern "C" void absolute_desktop_gpu_d3d11_begin_frame(int64_t) {}
extern "C" void absolute_desktop_gpu_d3d11_end_frame(int64_t) {}
extern "C" void absolute_desktop_gpu_d3d11_clear(int64_t, float, float, float, float) {}
extern "C" void absolute_desktop_gpu_d3d11_present(int64_t) {}
extern "C" void absolute_desktop_gpu_d3d11_unsupported(const char* what) {
    g_lastError = std::string("D3D11 backend: ") + (what ? what : "unsupported");
}

#else

#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "desktop_gpu_magic.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

extern "C" void* absolute_desktop_native_window(int64_t handle);
extern "C" int32_t absolute_desktop_width(int64_t handle);
extern "C" int32_t absolute_desktop_height(int64_t handle);

namespace {
    thread_local std::string g_lastError;

    void SetError(std::string message) {
        g_lastError = std::move(message);
    }

    struct D3D11Device {
        uint32_t magic = kAbsoluteGpuMagicD3D11;
        int64_t windowHandle = 0;
        HWND hwnd = nullptr;
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
        IDXGISwapChain* swap = nullptr;
        ID3D11RenderTargetView* rtv = nullptr;
        bool valid = false;
        bool inFrame = false;
        float clearColor[4] = {0.06f, 0.07f, 0.12f, 1.0f};

        void ReleaseRtv() {
            if (rtv) {
                rtv->Release();
                rtv = nullptr;
            }
        }

        bool CreateRtv() {
            ReleaseRtv();
            if (!swap || !device) return false;
            ID3D11Texture2D* back = nullptr;
            HRESULT hr = swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back));
            if (FAILED(hr) || !back) {
                SetError("IDXGISwapChain::GetBuffer failed");
                return false;
            }
            hr = device->CreateRenderTargetView(back, nullptr, &rtv);
            back->Release();
            if (FAILED(hr) || !rtv) {
                SetError("CreateRenderTargetView failed");
                return false;
            }
            return true;
        }

        void ResizeIfNeeded() {
            if (!swap || !context || !hwnd) return;
            const int32_t w = absolute_desktop_width(windowHandle);
            const int32_t h = absolute_desktop_height(windowHandle);
            if (w <= 0 || h <= 0) return;

            DXGI_SWAP_CHAIN_DESC desc{};
            if (FAILED(swap->GetDesc(&desc))) return;
            if (static_cast<int32_t>(desc.BufferDesc.Width) == w
                && static_cast<int32_t>(desc.BufferDesc.Height) == h) {
                return;
            }

            context->OMSetRenderTargets(0, nullptr, nullptr);
            ReleaseRtv();
            if (FAILED(swap->ResizeBuffers(0, static_cast<UINT>(w), static_cast<UINT>(h),
                    DXGI_FORMAT_UNKNOWN, 0))) {
                SetError("ResizeBuffers failed");
                return;
            }
            CreateRtv();
        }

        void Destroy() {
            if (context) context->ClearState();
            ReleaseRtv();
            if (swap) {
                swap->Release();
                swap = nullptr;
            }
            if (context) {
                context->Release();
                context = nullptr;
            }
            if (device) {
                device->Release();
                device = nullptr;
            }
            hwnd = nullptr;
            valid = false;
            inFrame = false;
        }
    };

    D3D11Device* FromHandle(int64_t handle) {
        if (!AbsoluteGpuIsD3D11(handle)) return nullptr;
        return reinterpret_cast<D3D11Device*>(static_cast<intptr_t>(handle));
    }

    int64_t ToHandle(D3D11Device* device) {
        return static_cast<int64_t>(reinterpret_cast<intptr_t>(device));
    }
}

extern "C" int64_t absolute_desktop_gpu_d3d11_create(int64_t windowHandle) {
    g_lastError.clear();
    HWND hwnd = static_cast<HWND>(absolute_desktop_native_window(windowHandle));
    if (!hwnd) {
        SetError("D3D11: window has no HWND");
        return 0;
    }

    int32_t width = absolute_desktop_width(windowHandle);
    int32_t height = absolute_desktop_height(windowHandle);
    if (width <= 0) width = 1;
    if (height <= 0) height = 1;

    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = static_cast<UINT>(width);
    scd.BufferDesc.Height = static_cast<UINT>(height);
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL gotLevel{};
    UINT flags = 0;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    auto* dev = new D3D11Device();
    dev->windowHandle = windowHandle;
    dev->hwnd = hwnd;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        &featureLevel,
        1,
        D3D11_SDK_VERSION,
        &scd,
        &dev->swap,
        &dev->device,
        &gotLevel,
        &dev->context);

    if (FAILED(hr) || !dev->device || !dev->context || !dev->swap) {
        // Retry without debug flag.
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            &featureLevel,
            1,
            D3D11_SDK_VERSION,
            &scd,
            &dev->swap,
            &dev->device,
            &gotLevel,
            &dev->context);
    }

    if (FAILED(hr) || !dev->device || !dev->context || !dev->swap) {
        SetError("D3D11CreateDeviceAndSwapChain failed");
        dev->Destroy();
        delete dev;
        return 0;
    }

    if (!dev->CreateRtv()) {
        dev->Destroy();
        delete dev;
        return 0;
    }

    dev->valid = true;
    return ToHandle(dev);
}

extern "C" void absolute_desktop_gpu_d3d11_destroy(int64_t handle) {
    D3D11Device* dev = FromHandle(handle);
    if (!dev) return;
    dev->Destroy();
    delete dev;
}

extern "C" int32_t absolute_desktop_gpu_d3d11_is_valid(int64_t handle) {
    const D3D11Device* dev = FromHandle(handle);
    return dev && dev->valid ? 1 : 0;
}

extern "C" const char* absolute_desktop_gpu_d3d11_backend() {
    return "d3d11";
}

extern "C" const char* absolute_desktop_gpu_d3d11_last_error() {
    return g_lastError.c_str();
}

extern "C" void absolute_desktop_gpu_d3d11_begin_frame(int64_t handle) {
    D3D11Device* dev = FromHandle(handle);
    if (!dev || !dev->valid || !dev->context) return;
    dev->ResizeIfNeeded();
    if (dev->rtv) {
        dev->context->OMSetRenderTargets(1, &dev->rtv, nullptr);
        const int32_t w = absolute_desktop_width(dev->windowHandle);
        const int32_t h = absolute_desktop_height(dev->windowHandle);
        D3D11_VIEWPORT vp{};
        vp.Width = static_cast<float>(w > 0 ? w : 1);
        vp.Height = static_cast<float>(h > 0 ? h : 1);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        dev->context->RSSetViewports(1, &vp);
    }
    dev->inFrame = true;
}

extern "C" void absolute_desktop_gpu_d3d11_end_frame(int64_t handle) {
    D3D11Device* dev = FromHandle(handle);
    if (!dev) return;
    dev->inFrame = false;
}

extern "C" void absolute_desktop_gpu_d3d11_clear(
    int64_t handle, float r, float g, float b, float a) {
    D3D11Device* dev = FromHandle(handle);
    if (!dev || !dev->valid || !dev->context || !dev->rtv) return;
    dev->clearColor[0] = r;
    dev->clearColor[1] = g;
    dev->clearColor[2] = b;
    dev->clearColor[3] = a;
    dev->context->ClearRenderTargetView(dev->rtv, dev->clearColor);
}

extern "C" void absolute_desktop_gpu_d3d11_present(int64_t handle) {
    D3D11Device* dev = FromHandle(handle);
    if (!dev || !dev->valid || !dev->swap) return;
    dev->swap->Present(1, 0);
}

// Shared lastError bridge for unsupported ops on D3D11 handles.
extern "C" void absolute_desktop_gpu_d3d11_unsupported(const char* what) {
    SetError(std::string("D3D11 backend: ") + (what ? what : "unsupported")
        + " (use OpenGL for full RHI; D3D11 supports clear/present)");
}

#endif
