// D3D12 backend for Desktop.Gpu (Windows).
// Lifecycle: create / beginFrame / clear / endFrame / present.
// Mesh RHI: HLSL shaders, VB/IB (upload heaps), PSO + input layout,
// draw/drawIndexed, float/int/float2 uniforms via root CBV b0.
// Textures/samplers not yet (use D3D11 / OpenGL).

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
    g_lastError = std::string("D3D12 backend: ") + (what ? what : "unsupported");
}
extern "C" int64_t absolute_desktop_gpu_d3d12_shader_create(int64_t, const char*, const char*) { return 0; }
extern "C" void absolute_desktop_gpu_d3d12_shader_destroy(int64_t, int64_t) {}
extern "C" int64_t absolute_desktop_gpu_d3d12_buffer_create(int64_t, const float*, int32_t) { return 0; }
extern "C" void absolute_desktop_gpu_d3d12_buffer_destroy(int64_t, int64_t) {}
extern "C" int32_t absolute_desktop_gpu_d3d12_buffer_float_count(int64_t) { return 0; }
extern "C" int64_t absolute_desktop_gpu_d3d12_index_buffer_create(int64_t, const int32_t*, int32_t) { return 0; }
extern "C" void absolute_desktop_gpu_d3d12_index_buffer_destroy(int64_t, int64_t) {}
extern "C" int32_t absolute_desktop_gpu_d3d12_index_buffer_count(int64_t) { return 0; }
extern "C" int64_t absolute_desktop_gpu_d3d12_pipeline_create(
    int64_t, int64_t, int32_t, const int32_t*, const int32_t*, const int32_t*, int32_t) {
    return 0;
}
extern "C" void absolute_desktop_gpu_d3d12_pipeline_destroy(int64_t, int64_t) {}
extern "C" void absolute_desktop_gpu_d3d12_bind_pipeline(int64_t, int64_t) {}
extern "C" void absolute_desktop_gpu_d3d12_bind_buffer(int64_t, int64_t) {}
extern "C" void absolute_desktop_gpu_d3d12_bind_index_buffer(int64_t, int64_t) {}
extern "C" void absolute_desktop_gpu_d3d12_draw(int64_t, int32_t) {}
extern "C" void absolute_desktop_gpu_d3d12_draw_indexed(int64_t, int32_t) {}
extern "C" void absolute_desktop_gpu_d3d12_set_uniform_f(int64_t, const char*, float) {}
extern "C" void absolute_desktop_gpu_d3d12_set_uniform_i(int64_t, const char*, int32_t) {}
extern "C" void absolute_desktop_gpu_d3d12_set_uniform_2f(int64_t, const char*, float, float) {}
extern "C" int32_t absolute_desktop_gpu_d3d12_is_resource(int64_t) { return 0; }

#else

#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "desktop_gpu_magic.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")

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

    constexpr uint32_t kResShader = 0xD3125348u;   // 'D2SH'
    constexpr uint32_t kResBuffer = 0xD3125642u;   // 'D2VB'
    constexpr uint32_t kResIndex = 0xD3124942u;    // 'D2IB'
    constexpr uint32_t kResPipeline = 0xD312504Cu; // 'D2PL'

    uint32_t PeekRes(int64_t handle) {
        if (!handle) return 0;
        return *reinterpret_cast<const uint32_t*>(static_cast<intptr_t>(handle));
    }

    int64_t PtrToHandle(void* p) {
        return static_cast<int64_t>(reinterpret_cast<intptr_t>(p));
    }

    DXGI_FORMAT FormatForComponents(int32_t components) {
        switch (components) {
        case 1: return DXGI_FORMAT_R32_FLOAT;
        case 2: return DXGI_FORMAT_R32G32_FLOAT;
        case 3: return DXGI_FORMAT_R32G32B32_FLOAT;
        case 4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        default: return DXGI_FORMAT_UNKNOWN;
        }
    }

    void SemanticForLocation(int32_t location, const char** name, UINT* index) {
        if (location <= 0) {
            *name = "POSITION";
            *index = 0;
            return;
        }
        *name = "TEXCOORD";
        *index = static_cast<UINT>(location - 1);
    }

    UINT Align256(UINT size) {
        return (size + 255u) & ~255u;
    }

    bool CreateUploadBuffer(
        ID3D12Device* device,
        UINT64 sizeBytes,
        ComPtr<ID3D12Resource>& out,
        void** mapped) {
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = sizeBytes;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        HRESULT hr = device->CreateCommittedResource(
            &hp,
            D3D12_HEAP_FLAG_NONE,
            &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&out));
        if (FAILED(hr) || !out) return false;
        if (mapped) {
            hr = out->Map(0, nullptr, mapped);
            if (FAILED(hr) || !*mapped) return false;
        }
        return true;
    }

    struct UniformVar {
        UINT offset = 0;
        UINT size = 0;
    };

    struct D3D12Shader {
        uint32_t magic = kResShader;
        ComPtr<ID3DBlob> vsBlob;
        ComPtr<ID3DBlob> psBlob;
        std::unordered_map<std::string, UniformVar> uniforms;
        UINT cbufferSize = 0;
        std::vector<uint8_t> cbufferCpu;
        bool cbufferDirty = true;

        void Destroy() {
            vsBlob.Reset();
            psBlob.Reset();
            uniforms.clear();
            cbufferCpu.clear();
            cbufferSize = 0;
        }
    };

    struct D3D12Buffer {
        uint32_t magic = kResBuffer;
        ComPtr<ID3D12Resource> resource;
        int32_t floatCount = 0;
        int32_t byteSize = 0;
        D3D12_VERTEX_BUFFER_VIEW view{};

        void Destroy() {
            resource.Reset();
        }
    };

    struct D3D12IndexBuffer {
        uint32_t magic = kResIndex;
        ComPtr<ID3D12Resource> resource;
        int32_t indexCount = 0;
        D3D12_INDEX_BUFFER_VIEW view{};

        void Destroy() {
            resource.Reset();
        }
    };

    struct D3D12Pipeline {
        uint32_t magic = kResPipeline;
        ComPtr<ID3D12PipelineState> pso;
        ComPtr<ID3D12Resource> cbuffer;
        uint8_t* cbufferMapped = nullptr;
        D3D12Shader* shader = nullptr; // not owned
        int32_t strideBytes = 0;

        void Destroy() {
            if (cbuffer && cbufferMapped) {
                cbuffer->Unmap(0, nullptr);
                cbufferMapped = nullptr;
            }
            cbuffer.Reset();
            pso.Reset();
            shader = nullptr;
        }
    };

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
        ComPtr<ID3D12RootSignature> rootSig;
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
        int64_t boundPipeline = 0;
        int64_t boundBuffer = 0;
        int64_t boundIndexBuffer = 0;

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

        bool CreateRootSignature() {
            D3D12_ROOT_PARAMETER rp{};
            rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            rp.Descriptor.ShaderRegister = 0;
            rp.Descriptor.RegisterSpace = 0;
            rp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            D3D12_ROOT_SIGNATURE_DESC rsd{};
            rsd.NumParameters = 1;
            rsd.pParameters = &rp;
            rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            ComPtr<ID3DBlob> sig;
            ComPtr<ID3DBlob> err;
            HRESULT hr = D3D12SerializeRootSignature(
                &rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
            if (FAILED(hr) || !sig) {
                std::string msg = "D3D12SerializeRootSignature failed";
                if (err && err->GetBufferPointer()) {
                    msg += ": ";
                    msg += static_cast<const char*>(err->GetBufferPointer());
                }
                SetError(std::move(msg));
                return false;
            }
            hr = device->CreateRootSignature(
                0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&rootSig));
            if (FAILED(hr) || !rootSig) {
                SetError("CreateRootSignature failed");
                return false;
            }
            return true;
        }

        void Destroy() {
            if (valid) WaitIdle();
            cmdOpen = false;
            ReleaseRenderTargets();
            cmdList.Reset();
            for (UINT i = 0; i < kFrameCount; ++i) {
                allocators[i].Reset();
                fenceValues[i] = 0;
            }
            rootSig.Reset();
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
            boundPipeline = 0;
            boundBuffer = 0;
            boundIndexBuffer = 0;
        }
    };

    D3D12Device* DeviceFrom(int64_t handle) {
        if (!AbsoluteGpuIsD3D12(handle)) return nullptr;
        return reinterpret_cast<D3D12Device*>(static_cast<intptr_t>(handle));
    }

    D3D12Shader* ShaderFrom(int64_t handle) {
        if (PeekRes(handle) != kResShader) return nullptr;
        return reinterpret_cast<D3D12Shader*>(static_cast<intptr_t>(handle));
    }

    D3D12Buffer* BufferFrom(int64_t handle) {
        if (PeekRes(handle) != kResBuffer) return nullptr;
        return reinterpret_cast<D3D12Buffer*>(static_cast<intptr_t>(handle));
    }

    D3D12IndexBuffer* IndexFrom(int64_t handle) {
        if (PeekRes(handle) != kResIndex) return nullptr;
        return reinterpret_cast<D3D12IndexBuffer*>(static_cast<intptr_t>(handle));
    }

    D3D12Pipeline* PipelineFrom(int64_t handle) {
        if (PeekRes(handle) != kResPipeline) return nullptr;
        return reinterpret_cast<D3D12Pipeline*>(static_cast<intptr_t>(handle));
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

    ID3DBlob* CompileHlsl(const char* source, const char* entry, const char* target) {
        if (!source || !source[0]) {
            SetError("empty HLSL source");
            return nullptr;
        }
        if (std::strncmp(source, "#version", 8) == 0
            || std::strstr(source, "#version") != nullptr) {
            SetError("D3D12 createShader expects HLSL (got GLSL-looking source with #version)");
            return nullptr;
        }

        ID3DBlob* code = nullptr;
        ID3DBlob* errors = nullptr;
        const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS
#if defined(_DEBUG)
            | D3DCOMPILE_DEBUG
#endif
            ;
        HRESULT hr = D3DCompile(
            source, std::strlen(source), nullptr, nullptr, nullptr,
            entry, target, flags, 0, &code, &errors);
        if (FAILED(hr) || !code) {
            std::string msg = "D3DCompile failed (";
            msg += target;
            msg += ")";
            if (errors && errors->GetBufferPointer()) {
                msg += ": ";
                msg += static_cast<const char*>(errors->GetBufferPointer());
            }
            SetError(std::move(msg));
            if (errors) errors->Release();
            if (code) code->Release();
            return nullptr;
        }
        if (errors) errors->Release();
        return code;
    }

    bool ReflectUniforms(ID3DBlob* vsBlob, D3D12Shader* shader) {
        shader->uniforms.clear();
        shader->cbufferSize = 0;
        shader->cbufferCpu.clear();

        ID3D11ShaderReflection* refl = nullptr;
        HRESULT hr = D3DReflect(
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            IID_ID3D11ShaderReflection,
            reinterpret_cast<void**>(&refl));
        if (FAILED(hr) || !refl) return true;

        D3D11_SHADER_DESC desc{};
        refl->GetDesc(&desc);
        for (UINT i = 0; i < desc.ConstantBuffers; ++i) {
            ID3D11ShaderReflectionConstantBuffer* cb = refl->GetConstantBufferByIndex(i);
            if (!cb) continue;
            D3D11_SHADER_BUFFER_DESC cbd{};
            if (FAILED(cb->GetDesc(&cbd))) continue;
            if (cbd.Size == 0) continue;
            shader->cbufferSize = cbd.Size;
            for (UINT v = 0; v < cbd.Variables; ++v) {
                ID3D11ShaderReflectionVariable* var = cb->GetVariableByIndex(v);
                if (!var) continue;
                D3D11_SHADER_VARIABLE_DESC vd{};
                if (FAILED(var->GetDesc(&vd))) continue;
                if (!vd.Name) continue;
                UniformVar uv;
                uv.offset = vd.StartOffset;
                uv.size = vd.Size;
                shader->uniforms[vd.Name] = uv;
            }
            break;
        }
        refl->Release();

        if (shader->cbufferSize > 0) {
            shader->cbufferSize = Align256(shader->cbufferSize);
            shader->cbufferCpu.assign(shader->cbufferSize, 0);
            shader->cbufferDirty = true;
        }
        return true;
    }

    bool WriteUniformBytes(D3D12Shader* sh, const char* name, const void* data, UINT bytes) {
        if (!sh || !name || !data) return false;
        auto it = sh->uniforms.find(name);
        if (it == sh->uniforms.end()) {
            SetError(std::string("D3D12 uniform not found: ") + name);
            return false;
        }
        const UniformVar& uv = it->second;
        if (uv.offset + bytes > sh->cbufferCpu.size()) {
            SetError(std::string("D3D12 uniform out of range: ") + name);
            return false;
        }
        std::memcpy(sh->cbufferCpu.data() + uv.offset, data, bytes);
        if (bytes < uv.size) {
            std::memset(sh->cbufferCpu.data() + uv.offset + bytes, 0, uv.size - bytes);
        }
        sh->cbufferDirty = true;
        g_lastError.clear();
        return true;
    }

    void FlushCbuffer(D3D12Pipeline* pipe) {
        if (!pipe || !pipe->shader || !pipe->cbufferMapped) return;
        D3D12Shader* sh = pipe->shader;
        if (!sh->cbufferDirty || sh->cbufferCpu.empty()) return;
        std::memcpy(pipe->cbufferMapped, sh->cbufferCpu.data(), sh->cbufferCpu.size());
        sh->cbufferDirty = false;
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
    hr = dev->device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&dev->rtvHeap));
    if (FAILED(hr) || !dev->rtvHeap) {
        SetError("CreateDescriptorHeap (RTV) failed");
        delete dev;
        return 0;
    }
    dev->rtvDescriptorSize =
        dev->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    if (!dev->CreateRenderTargets() || !dev->CreateRootSignature()) {
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
    return PtrToHandle(dev);
}

extern "C" void absolute_desktop_gpu_d3d12_destroy(int64_t handle) {
    D3D12Device* dev = DeviceFrom(handle);
    if (!dev) return;
    dev->Destroy();
    delete dev;
}

extern "C" int32_t absolute_desktop_gpu_d3d12_is_valid(int64_t handle) {
    const D3D12Device* dev = DeviceFrom(handle);
    return dev && dev->valid ? 1 : 0;
}

extern "C" const char* absolute_desktop_gpu_d3d12_backend() {
    return "d3d12";
}

extern "C" const char* absolute_desktop_gpu_d3d12_last_error() {
    return g_lastError.c_str();
}

extern "C" void absolute_desktop_gpu_d3d12_begin_frame(int64_t handle) {
    D3D12Device* dev = DeviceFrom(handle);
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
    dev->boundPipeline = 0;
    dev->boundBuffer = 0;
    dev->boundIndexBuffer = 0;

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

    if (dev->rootSig) {
        dev->cmdList->SetGraphicsRootSignature(dev->rootSig.Get());
    }
    dev->inFrame = true;
}

extern "C" void absolute_desktop_gpu_d3d12_clear(
    int64_t handle, float r, float g, float b, float a) {
    D3D12Device* dev = DeviceFrom(handle);
    if (!dev || !dev->valid || !dev->cmdOpen || !dev->cmdList) return;
    dev->clearColor[0] = r;
    dev->clearColor[1] = g;
    dev->clearColor[2] = b;
    dev->clearColor[3] = a;
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = dev->RtvHandle(dev->frameIndex);
    dev->cmdList->ClearRenderTargetView(rtv, dev->clearColor, 0, nullptr);
}

extern "C" void absolute_desktop_gpu_d3d12_end_frame(int64_t handle) {
    D3D12Device* dev = DeviceFrom(handle);
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
    D3D12Device* dev = DeviceFrom(handle);
    if (!dev || !dev->valid || !dev->swap || !dev->queue || !dev->fence) return;

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
        + " (textures/samplers still OpenGL/D3D11; use HLSL for shaders)");
}

extern "C" int32_t absolute_desktop_gpu_d3d12_is_resource(int64_t handle) {
    const uint32_t m = PeekRes(handle);
    return (m == kResShader || m == kResBuffer || m == kResIndex || m == kResPipeline) ? 1 : 0;
}

extern "C" int64_t absolute_desktop_gpu_d3d12_shader_create(
    int64_t gpuHandle, const char* vertexSource, const char* fragmentSource) {
    g_lastError.clear();
    D3D12Device* dev = DeviceFrom(gpuHandle);
    if (!dev || !dev->device || !vertexSource || !fragmentSource) {
        SetError("D3D12 createShader: invalid arguments");
        return 0;
    }

    ID3DBlob* vsBlob = CompileHlsl(vertexSource, "main", "vs_5_0");
    if (!vsBlob) return 0;
    ID3DBlob* psBlob = CompileHlsl(fragmentSource, "main", "ps_5_0");
    if (!psBlob) {
        vsBlob->Release();
        return 0;
    }

    auto* shader = new D3D12Shader();
    shader->vsBlob.Attach(vsBlob);
    shader->psBlob.Attach(psBlob);
    ReflectUniforms(vsBlob, shader);
    return PtrToHandle(shader);
}

extern "C" void absolute_desktop_gpu_d3d12_shader_destroy(int64_t /*gpuHandle*/, int64_t shaderHandle) {
    D3D12Shader* shader = ShaderFrom(shaderHandle);
    if (!shader) return;
    shader->Destroy();
    delete shader;
}

extern "C" int64_t absolute_desktop_gpu_d3d12_buffer_create(
    int64_t gpuHandle, const float* data, int32_t floatCount) {
    g_lastError.clear();
    D3D12Device* dev = DeviceFrom(gpuHandle);
    if (!dev || !dev->device || !data || floatCount <= 0) {
        SetError("D3D12 vertex buffer requires non-empty float data");
        return 0;
    }

    auto* buf = new D3D12Buffer();
    buf->floatCount = floatCount;
    buf->byteSize = floatCount * static_cast<int32_t>(sizeof(float));

    void* mapped = nullptr;
    if (!CreateUploadBuffer(
            dev->device.Get(), static_cast<UINT64>(buf->byteSize), buf->resource, &mapped)) {
        SetError("D3D12 CreateCommittedResource (vertex) failed");
        delete buf;
        return 0;
    }
    std::memcpy(mapped, data, static_cast<size_t>(buf->byteSize));
    // Keep mapped for simplicity (upload heap); unmap optional.
    buf->resource->Unmap(0, nullptr);

    buf->view.BufferLocation = buf->resource->GetGPUVirtualAddress();
    buf->view.SizeInBytes = static_cast<UINT>(buf->byteSize);
    buf->view.StrideInBytes = 0; // filled at draw from pipeline stride
    return PtrToHandle(buf);
}

extern "C" void absolute_desktop_gpu_d3d12_buffer_destroy(int64_t gpuHandle, int64_t bufferHandle) {
    D3D12Device* dev = DeviceFrom(gpuHandle);
    D3D12Buffer* buf = BufferFrom(bufferHandle);
    if (!buf) return;
    if (dev && dev->boundBuffer == bufferHandle) dev->boundBuffer = 0;
    if (dev) dev->WaitIdle();
    buf->Destroy();
    delete buf;
}

extern "C" int32_t absolute_desktop_gpu_d3d12_buffer_float_count(int64_t bufferHandle) {
    const D3D12Buffer* buf = BufferFrom(bufferHandle);
    return buf ? buf->floatCount : 0;
}

extern "C" int64_t absolute_desktop_gpu_d3d12_index_buffer_create(
    int64_t gpuHandle, const int32_t* indices, int32_t indexCount) {
    g_lastError.clear();
    D3D12Device* dev = DeviceFrom(gpuHandle);
    if (!dev || !dev->device || !indices || indexCount <= 0) {
        SetError("D3D12 index buffer requires non-empty int32 indices");
        return 0;
    }

    std::vector<uint32_t> u32(static_cast<size_t>(indexCount));
    for (int32_t i = 0; i < indexCount; ++i) {
        u32[static_cast<size_t>(i)] = static_cast<uint32_t>(indices[i] < 0 ? 0 : indices[i]);
    }

    auto* buf = new D3D12IndexBuffer();
    buf->indexCount = indexCount;
    const UINT byteSize = static_cast<UINT>(indexCount * static_cast<int32_t>(sizeof(uint32_t)));

    void* mapped = nullptr;
    if (!CreateUploadBuffer(dev->device.Get(), byteSize, buf->resource, &mapped)) {
        SetError("D3D12 CreateCommittedResource (index) failed");
        delete buf;
        return 0;
    }
    std::memcpy(mapped, u32.data(), byteSize);
    buf->resource->Unmap(0, nullptr);

    buf->view.BufferLocation = buf->resource->GetGPUVirtualAddress();
    buf->view.SizeInBytes = byteSize;
    buf->view.Format = DXGI_FORMAT_R32_UINT;
    return PtrToHandle(buf);
}

extern "C" void absolute_desktop_gpu_d3d12_index_buffer_destroy(
    int64_t gpuHandle, int64_t bufferHandle) {
    D3D12Device* dev = DeviceFrom(gpuHandle);
    D3D12IndexBuffer* buf = IndexFrom(bufferHandle);
    if (!buf) return;
    if (dev && dev->boundIndexBuffer == bufferHandle) dev->boundIndexBuffer = 0;
    if (dev) dev->WaitIdle();
    buf->Destroy();
    delete buf;
}

extern "C" int32_t absolute_desktop_gpu_d3d12_index_buffer_count(int64_t bufferHandle) {
    const D3D12IndexBuffer* buf = IndexFrom(bufferHandle);
    return buf ? buf->indexCount : 0;
}

extern "C" int64_t absolute_desktop_gpu_d3d12_pipeline_create(
    int64_t gpuHandle,
    int64_t shaderHandle,
    int32_t strideBytes,
    const int32_t* locations,
    const int32_t* components,
    const int32_t* offsets,
    int32_t attrCount) {
    g_lastError.clear();
    D3D12Device* dev = DeviceFrom(gpuHandle);
    D3D12Shader* shader = ShaderFrom(shaderHandle);
    if (!dev || !dev->device || !dev->rootSig || !shader || !shader->vsBlob || !shader->psBlob) {
        SetError("D3D12 createPipeline requires valid gpu and HLSL shader");
        return 0;
    }
    if (strideBytes <= 0 || attrCount <= 0 || !locations || !components || !offsets) {
        SetError("D3D12 createPipeline needs stride and attributes");
        return 0;
    }

    std::vector<D3D12_INPUT_ELEMENT_DESC> elems(static_cast<size_t>(attrCount));
    for (int32_t i = 0; i < attrCount; ++i) {
        const char* semName = nullptr;
        UINT semIndex = 0;
        SemanticForLocation(locations[i], &semName, &semIndex);
        DXGI_FORMAT fmt = FormatForComponents(components[i]);
        if (fmt == DXGI_FORMAT_UNKNOWN) {
            SetError("D3D12 invalid attribute components (1-4)");
            return 0;
        }
        D3D12_INPUT_ELEMENT_DESC& e = elems[static_cast<size_t>(i)];
        e.SemanticName = semName;
        e.SemanticIndex = semIndex;
        e.Format = fmt;
        e.InputSlot = 0;
        e.AlignedByteOffset = static_cast<UINT>(offsets[i]);
        e.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        e.InstanceDataStepRate = 0;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = dev->rootSig.Get();
    psoDesc.VS = {shader->vsBlob->GetBufferPointer(), shader->vsBlob->GetBufferSize()};
    psoDesc.PS = {shader->psBlob->GetBufferPointer(), shader->psBlob->GetBufferSize()};
    psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = FALSE;
    D3D12_RENDER_TARGET_BLEND_DESC& rt = psoDesc.BlendState.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.LogicOpEnable = FALSE;
    rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.InputLayout = {elems.data(), static_cast<UINT>(elems.size())};
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    auto* pipe = new D3D12Pipeline();
    pipe->shader = shader;
    pipe->strideBytes = strideBytes;

    HRESULT hr = dev->device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipe->pso));
    if (FAILED(hr) || !pipe->pso) {
        SetError("CreateGraphicsPipelineState failed (check HLSL semantics POSITION/TEXCOORDn)");
        delete pipe;
        return 0;
    }

    // Always allocate a 256-byte (or larger) root CBV slot.
    const UINT cbSize = shader->cbufferSize > 0 ? shader->cbufferSize : 256u;
    void* mapped = nullptr;
    if (!CreateUploadBuffer(dev->device.Get(), cbSize, pipe->cbuffer, &mapped)) {
        SetError("D3D12 cbuffer CreateCommittedResource failed");
        pipe->Destroy();
        delete pipe;
        return 0;
    }
    pipe->cbufferMapped = static_cast<uint8_t*>(mapped);
    std::memset(pipe->cbufferMapped, 0, cbSize);

    return PtrToHandle(pipe);
}

extern "C" void absolute_desktop_gpu_d3d12_pipeline_destroy(
    int64_t gpuHandle, int64_t pipelineHandle) {
    D3D12Device* dev = DeviceFrom(gpuHandle);
    D3D12Pipeline* pipe = PipelineFrom(pipelineHandle);
    if (!pipe) return;
    if (dev && dev->boundPipeline == pipelineHandle) dev->boundPipeline = 0;
    if (dev) dev->WaitIdle();
    pipe->Destroy();
    delete pipe;
}

extern "C" void absolute_desktop_gpu_d3d12_bind_pipeline(int64_t gpuHandle, int64_t pipelineHandle) {
    D3D12Device* dev = DeviceFrom(gpuHandle);
    if (!dev) return;
    dev->boundPipeline = pipelineHandle;
}

extern "C" void absolute_desktop_gpu_d3d12_bind_buffer(int64_t gpuHandle, int64_t bufferHandle) {
    D3D12Device* dev = DeviceFrom(gpuHandle);
    if (!dev) return;
    dev->boundBuffer = bufferHandle;
}

extern "C" void absolute_desktop_gpu_d3d12_bind_index_buffer(int64_t gpuHandle, int64_t bufferHandle) {
    D3D12Device* dev = DeviceFrom(gpuHandle);
    if (!dev) return;
    dev->boundIndexBuffer = bufferHandle;
}

static bool BindDrawState(D3D12Device* dev, D3D12Pipeline* pipe, D3D12Buffer* vb) {
    if (!dev->cmdOpen || !dev->cmdList || !pipe->pso || !vb->resource) {
        SetError("D3D12 draw requires open frame, pipeline, and vertex buffer");
        return false;
    }
    FlushCbuffer(pipe);
    dev->cmdList->SetPipelineState(pipe->pso.Get());
    if (dev->rootSig) {
        dev->cmdList->SetGraphicsRootSignature(dev->rootSig.Get());
    }
    if (pipe->cbuffer) {
        dev->cmdList->SetGraphicsRootConstantBufferView(
            0, pipe->cbuffer->GetGPUVirtualAddress());
    }
    D3D12_VERTEX_BUFFER_VIEW vbv = vb->view;
    vbv.StrideInBytes = static_cast<UINT>(pipe->strideBytes);
    dev->cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dev->cmdList->IASetVertexBuffers(0, 1, &vbv);
    return true;
}

extern "C" void absolute_desktop_gpu_d3d12_draw(int64_t gpuHandle, int32_t vertexCount) {
    D3D12Device* dev = DeviceFrom(gpuHandle);
    if (!dev || vertexCount <= 0) return;
    D3D12Pipeline* pipe = PipelineFrom(dev->boundPipeline);
    D3D12Buffer* vb = BufferFrom(dev->boundBuffer);
    if (!pipe || !vb) {
        SetError("D3D12 draw requires bound pipeline and vertex buffer");
        return;
    }
    if (!BindDrawState(dev, pipe, vb)) return;
    dev->cmdList->DrawInstanced(static_cast<UINT>(vertexCount), 1, 0, 0);
}

extern "C" void absolute_desktop_gpu_d3d12_draw_indexed(int64_t gpuHandle, int32_t indexCount) {
    D3D12Device* dev = DeviceFrom(gpuHandle);
    if (!dev || indexCount <= 0) return;
    D3D12Pipeline* pipe = PipelineFrom(dev->boundPipeline);
    D3D12Buffer* vb = BufferFrom(dev->boundBuffer);
    D3D12IndexBuffer* ib = IndexFrom(dev->boundIndexBuffer);
    if (!pipe || !vb || !ib) {
        SetError("D3D12 drawIndexed requires bound pipeline, VB, and IB");
        return;
    }
    if (!BindDrawState(dev, pipe, vb)) return;
    dev->cmdList->IASetIndexBuffer(&ib->view);
    dev->cmdList->DrawIndexedInstanced(static_cast<UINT>(indexCount), 1, 0, 0, 0);
}

extern "C" void absolute_desktop_gpu_d3d12_set_uniform_f(
    int64_t gpuHandle, const char* name, float value) {
    D3D12Device* dev = DeviceFrom(gpuHandle);
    if (!dev) return;
    D3D12Pipeline* pipe = PipelineFrom(dev->boundPipeline);
    if (!pipe || !pipe->shader) return;
    WriteUniformBytes(pipe->shader, name, &value, sizeof(float));
}

extern "C" void absolute_desktop_gpu_d3d12_set_uniform_i(
    int64_t gpuHandle, const char* name, int32_t value) {
    D3D12Device* dev = DeviceFrom(gpuHandle);
    if (!dev) return;
    D3D12Pipeline* pipe = PipelineFrom(dev->boundPipeline);
    if (!pipe || !pipe->shader) return;
    WriteUniformBytes(pipe->shader, name, &value, sizeof(int32_t));
}

extern "C" void absolute_desktop_gpu_d3d12_set_uniform_2f(
    int64_t gpuHandle, const char* name, float x, float y) {
    D3D12Device* dev = DeviceFrom(gpuHandle);
    if (!dev) return;
    D3D12Pipeline* pipe = PipelineFrom(dev->boundPipeline);
    if (!pipe || !pipe->shader) return;
    const float v[2] = {x, y};
    WriteUniformBytes(pipe->shader, name, v, sizeof(v));
}

#endif
