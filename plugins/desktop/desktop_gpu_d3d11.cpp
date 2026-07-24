// D3D11 backend for Desktop.Gpu (Windows).
// Supports: create/clear/present, HLSL shaders, vertex/index buffers,
// pipelines (input layout), bind/draw/drawIndexed, float/int/float2 uniforms
// via cbuffer b0, textures (from soft sprite) + sampler objects.
// HLSL: Texture2D t0 + SamplerState s0 (bindTexture/bindSampler units).

#if !defined(_WIN32)

#include <cstdint>
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
extern "C" int64_t absolute_desktop_gpu_d3d11_shader_create(int64_t, const char*, const char*) { return 0; }
extern "C" void absolute_desktop_gpu_d3d11_shader_destroy(int64_t, int64_t) {}
extern "C" int64_t absolute_desktop_gpu_d3d11_buffer_create(int64_t, const float*, int32_t) { return 0; }
extern "C" void absolute_desktop_gpu_d3d11_buffer_destroy(int64_t, int64_t) {}
extern "C" int32_t absolute_desktop_gpu_d3d11_buffer_float_count(int64_t) { return 0; }
extern "C" int64_t absolute_desktop_gpu_d3d11_index_buffer_create(int64_t, const int32_t*, int32_t) { return 0; }
extern "C" void absolute_desktop_gpu_d3d11_index_buffer_destroy(int64_t, int64_t) {}
extern "C" int32_t absolute_desktop_gpu_d3d11_index_buffer_count(int64_t) { return 0; }
extern "C" int64_t absolute_desktop_gpu_d3d11_pipeline_create(
    int64_t, int64_t, int32_t, const int32_t*, const int32_t*, const int32_t*, int32_t) {
    return 0;
}
extern "C" void absolute_desktop_gpu_d3d11_pipeline_destroy(int64_t, int64_t) {}
extern "C" void absolute_desktop_gpu_d3d11_bind_pipeline(int64_t, int64_t) {}
extern "C" void absolute_desktop_gpu_d3d11_bind_buffer(int64_t, int64_t) {}
extern "C" void absolute_desktop_gpu_d3d11_bind_index_buffer(int64_t, int64_t) {}
extern "C" void absolute_desktop_gpu_d3d11_draw(int64_t, int32_t) {}
extern "C" void absolute_desktop_gpu_d3d11_draw_indexed(int64_t, int32_t) {}
extern "C" void absolute_desktop_gpu_d3d11_set_uniform_f(int64_t, const char*, float) {}
extern "C" void absolute_desktop_gpu_d3d11_set_uniform_i(int64_t, const char*, int32_t) {}
extern "C" void absolute_desktop_gpu_d3d11_set_uniform_2f(int64_t, const char*, float, float) {}
extern "C" int32_t absolute_desktop_gpu_d3d11_is_resource(int64_t) { return 0; }
extern "C" int64_t absolute_desktop_gpu_d3d11_texture_from_sprite(int64_t, int64_t) { return 0; }
extern "C" void absolute_desktop_gpu_d3d11_texture_destroy(int64_t, int64_t) {}
extern "C" int32_t absolute_desktop_gpu_d3d11_texture_width(int64_t) { return 0; }
extern "C" int32_t absolute_desktop_gpu_d3d11_texture_height(int64_t) { return 0; }
extern "C" void absolute_desktop_gpu_d3d11_bind_texture(int64_t, int64_t, int32_t) {}
extern "C" int64_t absolute_desktop_gpu_d3d11_sampler_create(int64_t, int32_t, int32_t) { return 0; }
extern "C" void absolute_desktop_gpu_d3d11_sampler_destroy(int64_t, int64_t) {}
extern "C" void absolute_desktop_gpu_d3d11_bind_sampler(int64_t, int64_t, int32_t) {}

#else

#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "desktop_gpu_magic.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")

extern "C" void* absolute_desktop_native_window(int64_t handle);
extern "C" int32_t absolute_desktop_width(int64_t handle);
extern "C" int32_t absolute_desktop_height(int64_t handle);

namespace {
    thread_local std::string g_lastError;

    void SetError(std::string message) {
        g_lastError = std::move(message);
    }

    // Resource magics (first uint32) — distinct from device magics.
    constexpr uint32_t kResShader = 0xD3115348u;   // 'D3SH'
    constexpr uint32_t kResBuffer = 0xD3115642u;   // 'D3VB'
    constexpr uint32_t kResIndex = 0xD3114942u;    // 'D3IB'
    constexpr uint32_t kResPipeline = 0xD311504Cu; // 'D3PL'
    constexpr uint32_t kResTexture = 0xD3115445u;  // 'D3TE'
    constexpr uint32_t kResSampler = 0xD3115341u;  // 'D3SA'

    uint32_t PeekRes(int64_t handle) {
        if (!handle) return 0;
        return *reinterpret_cast<const uint32_t*>(static_cast<intptr_t>(handle));
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

    // Location 0 → POSITION; others → TEXCOORD with semantic index location-1.
    // HLSL must match (e.g. float3 aColor : TEXCOORD0).
    void SemanticForLocation(int32_t location, const char** name, UINT* index) {
        if (location <= 0) {
            *name = "POSITION";
            *index = 0;
            return;
        }
        *name = "TEXCOORD";
        *index = static_cast<UINT>(location - 1);
    }

    struct UniformVar {
        UINT offset = 0;
        UINT size = 0; // bytes
    };

    struct D3D11Shader {
        uint32_t magic = kResShader;
        ID3D11VertexShader* vs = nullptr;
        ID3D11PixelShader* ps = nullptr;
        ID3DBlob* vsBlob = nullptr; // kept for CreateInputLayout
        std::unordered_map<std::string, UniformVar> uniforms;
        UINT cbufferSize = 0;
        std::vector<uint8_t> cbufferCpu;
        bool cbufferDirty = true;

        void Destroy() {
            if (vs) {
                vs->Release();
                vs = nullptr;
            }
            if (ps) {
                ps->Release();
                ps = nullptr;
            }
            if (vsBlob) {
                vsBlob->Release();
                vsBlob = nullptr;
            }
            uniforms.clear();
            cbufferCpu.clear();
            cbufferSize = 0;
        }
    };

    struct D3D11Buffer {
        uint32_t magic = kResBuffer;
        ID3D11Buffer* buffer = nullptr;
        int32_t floatCount = 0;
        int32_t byteSize = 0;

        void Destroy() {
            if (buffer) {
                buffer->Release();
                buffer = nullptr;
            }
        }
    };

    struct D3D11IndexBuffer {
        uint32_t magic = kResIndex;
        ID3D11Buffer* buffer = nullptr;
        int32_t indexCount = 0;

        void Destroy() {
            if (buffer) {
                buffer->Release();
                buffer = nullptr;
            }
        }
    };

    struct D3D11Pipeline {
        uint32_t magic = kResPipeline;
        ID3D11VertexShader* vs = nullptr; // not owned
        ID3D11PixelShader* ps = nullptr;  // not owned
        ID3D11InputLayout* layout = nullptr;
        ID3D11Buffer* cbuffer = nullptr;
        D3D11Shader* shader = nullptr; // not owned; for uniform uploads
        int32_t strideBytes = 0;

        void Destroy() {
            if (layout) {
                layout->Release();
                layout = nullptr;
            }
            if (cbuffer) {
                cbuffer->Release();
                cbuffer = nullptr;
            }
            vs = nullptr;
            ps = nullptr;
            shader = nullptr;
        }
    };

    struct D3D11Texture {
        uint32_t magic = kResTexture;
        ID3D11Texture2D* texture = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
        int32_t width = 0;
        int32_t height = 0;

        void Destroy() {
            if (srv) {
                srv->Release();
                srv = nullptr;
            }
            if (texture) {
                texture->Release();
                texture = nullptr;
            }
        }
    };

    struct D3D11Sampler {
        uint32_t magic = kResSampler;
        ID3D11SamplerState* state = nullptr;

        void Destroy() {
            if (state) {
                state->Release();
                state = nullptr;
            }
        }
    };

    // Must match DesktopSprite in desktop_soft_sprites.cpp.
    struct DesktopSpriteView {
        int32_t width = 1;
        int32_t height = 1;
        std::vector<uint32_t> pixels;
    };

    struct D3D11Device {
        uint32_t magic = kAbsoluteGpuMagicD3D11;
        int64_t windowHandle = 0;
        HWND hwnd = nullptr;
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
        IDXGISwapChain* swap = nullptr;
        ID3D11RenderTargetView* rtv = nullptr;
        ID3D11BlendState* blend = nullptr;
        ID3D11RasterizerState* raster = nullptr;
        ID3D11DepthStencilState* depthOff = nullptr;
        bool valid = false;
        bool inFrame = false;
        float clearColor[4] = {0.06f, 0.07f, 0.12f, 1.0f};
        int64_t boundPipeline = 0;
        int64_t boundBuffer = 0;
        int64_t boundIndexBuffer = 0;

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

        bool CreateStates() {
            D3D11_BLEND_DESC bd{};
            bd.RenderTarget[0].BlendEnable = TRUE;
            bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
            bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            if (FAILED(device->CreateBlendState(&bd, &blend))) {
                SetError("CreateBlendState failed");
                return false;
            }

            D3D11_RASTERIZER_DESC rd{};
            rd.FillMode = D3D11_FILL_SOLID;
            rd.CullMode = D3D11_CULL_NONE;
            rd.DepthClipEnable = TRUE;
            if (FAILED(device->CreateRasterizerState(&rd, &raster))) {
                SetError("CreateRasterizerState failed");
                return false;
            }

            D3D11_DEPTH_STENCIL_DESC dd{};
            dd.DepthEnable = FALSE;
            dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
            if (FAILED(device->CreateDepthStencilState(&dd, &depthOff))) {
                SetError("CreateDepthStencilState failed");
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
            if (blend) {
                blend->Release();
                blend = nullptr;
            }
            if (raster) {
                raster->Release();
                raster = nullptr;
            }
            if (depthOff) {
                depthOff->Release();
                depthOff = nullptr;
            }
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
            boundPipeline = 0;
            boundBuffer = 0;
            boundIndexBuffer = 0;
        }
    };

    D3D11Device* DeviceFrom(int64_t handle) {
        if (!AbsoluteGpuIsD3D11(handle)) return nullptr;
        return reinterpret_cast<D3D11Device*>(static_cast<intptr_t>(handle));
    }

    D3D11Shader* ShaderFrom(int64_t handle) {
        if (PeekRes(handle) != kResShader) return nullptr;
        return reinterpret_cast<D3D11Shader*>(static_cast<intptr_t>(handle));
    }

    D3D11Buffer* BufferFrom(int64_t handle) {
        if (PeekRes(handle) != kResBuffer) return nullptr;
        return reinterpret_cast<D3D11Buffer*>(static_cast<intptr_t>(handle));
    }

    D3D11IndexBuffer* IndexFrom(int64_t handle) {
        if (PeekRes(handle) != kResIndex) return nullptr;
        return reinterpret_cast<D3D11IndexBuffer*>(static_cast<intptr_t>(handle));
    }

    D3D11Pipeline* PipelineFrom(int64_t handle) {
        if (PeekRes(handle) != kResPipeline) return nullptr;
        return reinterpret_cast<D3D11Pipeline*>(static_cast<intptr_t>(handle));
    }

    D3D11Texture* TextureFrom(int64_t handle) {
        if (PeekRes(handle) != kResTexture) return nullptr;
        return reinterpret_cast<D3D11Texture*>(static_cast<intptr_t>(handle));
    }

    D3D11Sampler* SamplerFrom(int64_t handle) {
        if (PeekRes(handle) != kResSampler) return nullptr;
        return reinterpret_cast<D3D11Sampler*>(static_cast<intptr_t>(handle));
    }

    DesktopSpriteView* SpriteFrom(int64_t handle) {
        if (!handle) return nullptr;
        return reinterpret_cast<DesktopSpriteView*>(static_cast<intptr_t>(handle));
    }

    int64_t PtrToHandle(void* p) {
        return static_cast<int64_t>(reinterpret_cast<intptr_t>(p));
    }

    D3D11_FILTER FilterToD3D(int32_t filter) {
        return filter == 1 ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_MIP_POINT;
    }

    D3D11_TEXTURE_ADDRESS_MODE WrapToD3D(int32_t wrap) {
        switch (wrap) {
        case 1: return D3D11_TEXTURE_ADDRESS_WRAP;
        case 2: return D3D11_TEXTURE_ADDRESS_MIRROR;
        default: return D3D11_TEXTURE_ADDRESS_CLAMP;
        }
    }

    ID3DBlob* CompileHlsl(const char* source, const char* entry, const char* target) {
        if (!source || !source[0]) {
            SetError("empty HLSL source");
            return nullptr;
        }
        // GLSL often starts with #version — fail with a clear message.
        if (std::strncmp(source, "#version", 8) == 0
            || std::strstr(source, "#version") != nullptr) {
            SetError("D3D11 createShader expects HLSL (got GLSL-looking source with #version)");
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
            source,
            std::strlen(source),
            nullptr,
            nullptr,
            nullptr,
            entry,
            target,
            flags,
            0,
            &code,
            &errors);
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

    bool ReflectUniforms(ID3DBlob* vsBlob, D3D11Shader* shader) {
        shader->uniforms.clear();
        shader->cbufferSize = 0;
        shader->cbufferCpu.clear();

        ID3D11ShaderReflection* refl = nullptr;
        HRESULT hr = D3DReflect(
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            IID_ID3D11ShaderReflection,
            reinterpret_cast<void**>(&refl));
        if (FAILED(hr) || !refl) {
            // No uniforms is fine.
            return true;
        }

        D3D11_SHADER_DESC desc{};
        refl->GetDesc(&desc);
        for (UINT i = 0; i < desc.ConstantBuffers; ++i) {
            ID3D11ShaderReflectionConstantBuffer* cb = refl->GetConstantBufferByIndex(i);
            if (!cb) continue;
            D3D11_SHADER_BUFFER_DESC cbd{};
            if (FAILED(cb->GetDesc(&cbd))) continue;
            // Use first non-empty cbuffer (typically b0 / $Globals).
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
            break; // only first cbuffer (b0)
        }
        refl->Release();

        if (shader->cbufferSize > 0) {
            // Align to 16 bytes for UpdateSubresource.
            shader->cbufferSize = (shader->cbufferSize + 15u) & ~15u;
            shader->cbufferCpu.assign(shader->cbufferSize, 0);
            shader->cbufferDirty = true;
        }
        return true;
    }

    void UploadCbuffer(D3D11Device* dev, D3D11Pipeline* pipe) {
        if (!dev || !pipe || !pipe->cbuffer || !pipe->shader) return;
        D3D11Shader* sh = pipe->shader;
        if (!sh->cbufferDirty || sh->cbufferCpu.empty()) return;
        dev->context->UpdateSubresource(
            pipe->cbuffer, 0, nullptr, sh->cbufferCpu.data(), 0, 0);
        sh->cbufferDirty = false;
    }

    bool WriteUniformBytes(D3D11Shader* sh, const char* name, const void* data, UINT bytes) {
        if (!sh || !name || !data) return false;
        auto it = sh->uniforms.find(name);
        if (it == sh->uniforms.end()) {
            SetError(std::string("D3D11 uniform not found: ") + name);
            return false;
        }
        const UniformVar& uv = it->second;
        if (uv.offset + bytes > sh->cbufferCpu.size()) {
            SetError(std::string("D3D11 uniform out of range: ") + name);
            return false;
        }
        std::memcpy(sh->cbufferCpu.data() + uv.offset, data, bytes);
        // Zero pad remainder of variable (float into float4 slot).
        if (bytes < uv.size) {
            std::memset(sh->cbufferCpu.data() + uv.offset + bytes, 0, uv.size - bytes);
        }
        sh->cbufferDirty = true;
        g_lastError.clear();
        return true;
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

    if (!dev->CreateRtv() || !dev->CreateStates()) {
        dev->Destroy();
        delete dev;
        return 0;
    }

    dev->valid = true;
    return PtrToHandle(dev);
}

extern "C" void absolute_desktop_gpu_d3d11_destroy(int64_t handle) {
    D3D11Device* dev = DeviceFrom(handle);
    if (!dev) return;
    dev->Destroy();
    delete dev;
}

extern "C" int32_t absolute_desktop_gpu_d3d11_is_valid(int64_t handle) {
    const D3D11Device* dev = DeviceFrom(handle);
    return dev && dev->valid ? 1 : 0;
}

extern "C" const char* absolute_desktop_gpu_d3d11_backend() {
    return "d3d11";
}

extern "C" const char* absolute_desktop_gpu_d3d11_last_error() {
    return g_lastError.c_str();
}

extern "C" void absolute_desktop_gpu_d3d11_begin_frame(int64_t handle) {
    D3D11Device* dev = DeviceFrom(handle);
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
    if (dev->blend) {
        const float factor[4] = {0, 0, 0, 0};
        dev->context->OMSetBlendState(dev->blend, factor, 0xffffffff);
    }
    if (dev->raster) dev->context->RSSetState(dev->raster);
    if (dev->depthOff) dev->context->OMSetDepthStencilState(dev->depthOff, 0);
    dev->boundPipeline = 0;
    dev->boundBuffer = 0;
    dev->boundIndexBuffer = 0;
    dev->inFrame = true;
}

extern "C" void absolute_desktop_gpu_d3d11_end_frame(int64_t handle) {
    D3D11Device* dev = DeviceFrom(handle);
    if (!dev) return;
    dev->inFrame = false;
    dev->boundPipeline = 0;
    dev->boundBuffer = 0;
    dev->boundIndexBuffer = 0;
}

extern "C" void absolute_desktop_gpu_d3d11_clear(
    int64_t handle, float r, float g, float b, float a) {
    D3D11Device* dev = DeviceFrom(handle);
    if (!dev || !dev->valid || !dev->context || !dev->rtv) return;
    dev->clearColor[0] = r;
    dev->clearColor[1] = g;
    dev->clearColor[2] = b;
    dev->clearColor[3] = a;
    dev->context->ClearRenderTargetView(dev->rtv, dev->clearColor);
}

extern "C" void absolute_desktop_gpu_d3d11_present(int64_t handle) {
    D3D11Device* dev = DeviceFrom(handle);
    if (!dev || !dev->valid || !dev->swap) return;
    dev->swap->Present(1, 0);
}

extern "C" void absolute_desktop_gpu_d3d11_unsupported(const char* what) {
    SetError(std::string("D3D11 backend: ") + (what ? what : "unsupported")
        + " (use HLSL for shaders; Texture2D t0 + SamplerState s0)");
}

extern "C" int32_t absolute_desktop_gpu_d3d11_is_resource(int64_t handle) {
    const uint32_t m = PeekRes(handle);
    return (m == kResShader || m == kResBuffer || m == kResIndex || m == kResPipeline
        || m == kResTexture || m == kResSampler)
        ? 1
        : 0;
}

extern "C" int64_t absolute_desktop_gpu_d3d11_shader_create(
    int64_t gpuHandle, const char* vertexSource, const char* fragmentSource) {
    g_lastError.clear();
    D3D11Device* dev = DeviceFrom(gpuHandle);
    if (!dev || !dev->device || !vertexSource || !fragmentSource) {
        SetError("D3D11 createShader: invalid arguments");
        return 0;
    }

    ID3DBlob* vsBlob = CompileHlsl(vertexSource, "main", "vs_5_0");
    if (!vsBlob) return 0;
    ID3DBlob* psBlob = CompileHlsl(fragmentSource, "main", "ps_5_0");
    if (!psBlob) {
        vsBlob->Release();
        return 0;
    }

    auto* shader = new D3D11Shader();
    HRESULT hr = dev->device->CreateVertexShader(
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &shader->vs);
    if (FAILED(hr) || !shader->vs) {
        SetError("CreateVertexShader failed");
        psBlob->Release();
        vsBlob->Release();
        delete shader;
        return 0;
    }
    hr = dev->device->CreatePixelShader(
        psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &shader->ps);
    psBlob->Release();
    if (FAILED(hr) || !shader->ps) {
        SetError("CreatePixelShader failed");
        shader->Destroy();
        vsBlob->Release();
        delete shader;
        return 0;
    }

    shader->vsBlob = vsBlob;
    ReflectUniforms(vsBlob, shader);
    return PtrToHandle(shader);
}

extern "C" void absolute_desktop_gpu_d3d11_shader_destroy(int64_t /*gpuHandle*/, int64_t shaderHandle) {
    D3D11Shader* shader = ShaderFrom(shaderHandle);
    if (!shader) return;
    shader->Destroy();
    delete shader;
}

extern "C" int64_t absolute_desktop_gpu_d3d11_buffer_create(
    int64_t gpuHandle, const float* data, int32_t floatCount) {
    g_lastError.clear();
    D3D11Device* dev = DeviceFrom(gpuHandle);
    if (!dev || !dev->device || !data || floatCount <= 0) {
        SetError("D3D11 vertex buffer requires non-empty float data");
        return 0;
    }

    auto* buf = new D3D11Buffer();
    buf->floatCount = floatCount;
    buf->byteSize = floatCount * static_cast<int32_t>(sizeof(float));

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = static_cast<UINT>(buf->byteSize);
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sd{};
    sd.pSysMem = data;
    HRESULT hr = dev->device->CreateBuffer(&bd, &sd, &buf->buffer);
    if (FAILED(hr) || !buf->buffer) {
        SetError("D3D11 CreateBuffer (vertex) failed");
        delete buf;
        return 0;
    }
    return PtrToHandle(buf);
}

extern "C" void absolute_desktop_gpu_d3d11_buffer_destroy(int64_t gpuHandle, int64_t bufferHandle) {
    D3D11Device* dev = DeviceFrom(gpuHandle);
    D3D11Buffer* buf = BufferFrom(bufferHandle);
    if (!buf) return;
    if (dev && dev->boundBuffer == bufferHandle) dev->boundBuffer = 0;
    buf->Destroy();
    delete buf;
}

extern "C" int32_t absolute_desktop_gpu_d3d11_buffer_float_count(int64_t bufferHandle) {
    const D3D11Buffer* buf = BufferFrom(bufferHandle);
    return buf ? buf->floatCount : 0;
}

extern "C" int64_t absolute_desktop_gpu_d3d11_index_buffer_create(
    int64_t gpuHandle, const int32_t* indices, int32_t indexCount) {
    g_lastError.clear();
    D3D11Device* dev = DeviceFrom(gpuHandle);
    if (!dev || !dev->device || !indices || indexCount <= 0) {
        SetError("D3D11 index buffer requires non-empty int32 indices");
        return 0;
    }

    // Upload as uint32 for DXGI_FORMAT_R32_UINT.
    std::vector<uint32_t> u32(static_cast<size_t>(indexCount));
    for (int32_t i = 0; i < indexCount; ++i) {
        u32[static_cast<size_t>(i)] = static_cast<uint32_t>(indices[i] < 0 ? 0 : indices[i]);
    }

    auto* buf = new D3D11IndexBuffer();
    buf->indexCount = indexCount;

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = static_cast<UINT>(indexCount * static_cast<int32_t>(sizeof(uint32_t)));
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sd{};
    sd.pSysMem = u32.data();
    HRESULT hr = dev->device->CreateBuffer(&bd, &sd, &buf->buffer);
    if (FAILED(hr) || !buf->buffer) {
        SetError("D3D11 CreateBuffer (index) failed");
        delete buf;
        return 0;
    }
    return PtrToHandle(buf);
}

extern "C" void absolute_desktop_gpu_d3d11_index_buffer_destroy(
    int64_t gpuHandle, int64_t bufferHandle) {
    D3D11Device* dev = DeviceFrom(gpuHandle);
    D3D11IndexBuffer* buf = IndexFrom(bufferHandle);
    if (!buf) return;
    if (dev && dev->boundIndexBuffer == bufferHandle) dev->boundIndexBuffer = 0;
    buf->Destroy();
    delete buf;
}

extern "C" int32_t absolute_desktop_gpu_d3d11_index_buffer_count(int64_t bufferHandle) {
    const D3D11IndexBuffer* buf = IndexFrom(bufferHandle);
    return buf ? buf->indexCount : 0;
}

extern "C" int64_t absolute_desktop_gpu_d3d11_pipeline_create(
    int64_t gpuHandle,
    int64_t shaderHandle,
    int32_t strideBytes,
    const int32_t* locations,
    const int32_t* components,
    const int32_t* offsets,
    int32_t attrCount) {
    g_lastError.clear();
    D3D11Device* dev = DeviceFrom(gpuHandle);
    D3D11Shader* shader = ShaderFrom(shaderHandle);
    if (!dev || !dev->device || !shader || !shader->vs || !shader->ps || !shader->vsBlob) {
        SetError("D3D11 createPipeline requires valid gpu and HLSL shader");
        return 0;
    }
    if (strideBytes <= 0 || attrCount <= 0 || !locations || !components || !offsets) {
        SetError("D3D11 createPipeline needs stride and attributes");
        return 0;
    }

    std::vector<D3D11_INPUT_ELEMENT_DESC> elems(static_cast<size_t>(attrCount));
    std::vector<std::string> semanticStorage; // keep strings alive... use static names
    for (int32_t i = 0; i < attrCount; ++i) {
        const char* semName = nullptr;
        UINT semIndex = 0;
        SemanticForLocation(locations[i], &semName, &semIndex);
        DXGI_FORMAT fmt = FormatForComponents(components[i]);
        if (fmt == DXGI_FORMAT_UNKNOWN) {
            SetError("D3D11 invalid attribute components (1-4)");
            return 0;
        }
        D3D11_INPUT_ELEMENT_DESC& e = elems[static_cast<size_t>(i)];
        e.SemanticName = semName;
        e.SemanticIndex = semIndex;
        e.Format = fmt;
        e.InputSlot = 0;
        e.AlignedByteOffset = static_cast<UINT>(offsets[i]);
        e.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
        e.InstanceDataStepRate = 0;
    }

    auto* pipe = new D3D11Pipeline();
    pipe->vs = shader->vs;
    pipe->ps = shader->ps;
    pipe->shader = shader;
    pipe->strideBytes = strideBytes;

    HRESULT hr = dev->device->CreateInputLayout(
        elems.data(),
        static_cast<UINT>(elems.size()),
        shader->vsBlob->GetBufferPointer(),
        shader->vsBlob->GetBufferSize(),
        &pipe->layout);
    if (FAILED(hr) || !pipe->layout) {
        SetError("CreateInputLayout failed (check HLSL semantics: POSITION / TEXCOORDn)");
        delete pipe;
        return 0;
    }

    if (shader->cbufferSize > 0) {
        D3D11_BUFFER_DESC cbd{};
        cbd.ByteWidth = shader->cbufferSize;
        cbd.Usage = D3D11_USAGE_DEFAULT;
        cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = dev->device->CreateBuffer(&cbd, nullptr, &pipe->cbuffer);
        if (FAILED(hr) || !pipe->cbuffer) {
            SetError("CreateBuffer (cbuffer) failed");
            pipe->Destroy();
            delete pipe;
            return 0;
        }
    }

    return PtrToHandle(pipe);
}

extern "C" void absolute_desktop_gpu_d3d11_pipeline_destroy(
    int64_t gpuHandle, int64_t pipelineHandle) {
    D3D11Device* dev = DeviceFrom(gpuHandle);
    D3D11Pipeline* pipe = PipelineFrom(pipelineHandle);
    if (!pipe) return;
    if (dev && dev->boundPipeline == pipelineHandle) dev->boundPipeline = 0;
    pipe->Destroy();
    delete pipe;
}

extern "C" void absolute_desktop_gpu_d3d11_bind_pipeline(int64_t gpuHandle, int64_t pipelineHandle) {
    D3D11Device* dev = DeviceFrom(gpuHandle);
    if (!dev) return;
    dev->boundPipeline = pipelineHandle;
}

extern "C" void absolute_desktop_gpu_d3d11_bind_buffer(int64_t gpuHandle, int64_t bufferHandle) {
    D3D11Device* dev = DeviceFrom(gpuHandle);
    if (!dev) return;
    dev->boundBuffer = bufferHandle;
}

extern "C" void absolute_desktop_gpu_d3d11_bind_index_buffer(int64_t gpuHandle, int64_t bufferHandle) {
    D3D11Device* dev = DeviceFrom(gpuHandle);
    if (!dev) return;
    dev->boundIndexBuffer = bufferHandle;
}

static void BindPipelineState(D3D11Device* dev, D3D11Pipeline* pipe) {
    dev->context->IASetInputLayout(pipe->layout);
    dev->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dev->context->VSSetShader(pipe->vs, nullptr, 0);
    dev->context->PSSetShader(pipe->ps, nullptr, 0);
    if (pipe->cbuffer) {
        UploadCbuffer(dev, pipe);
        ID3D11Buffer* cbs[] = {pipe->cbuffer};
        dev->context->VSSetConstantBuffers(0, 1, cbs);
        dev->context->PSSetConstantBuffers(0, 1, cbs);
    }
}

extern "C" void absolute_desktop_gpu_d3d11_draw(int64_t gpuHandle, int32_t vertexCount) {
    D3D11Device* dev = DeviceFrom(gpuHandle);
    if (!dev || !dev->context || vertexCount <= 0) return;
    D3D11Pipeline* pipe = PipelineFrom(dev->boundPipeline);
    D3D11Buffer* vb = BufferFrom(dev->boundBuffer);
    if (!pipe || !vb || !pipe->layout || !vb->buffer) {
        SetError("D3D11 draw requires bound pipeline and vertex buffer");
        return;
    }
    BindPipelineState(dev, pipe);
    UINT stride = static_cast<UINT>(pipe->strideBytes);
    UINT offset = 0;
    ID3D11Buffer* bufs[] = {vb->buffer};
    dev->context->IASetVertexBuffers(0, 1, bufs, &stride, &offset);
    dev->context->Draw(static_cast<UINT>(vertexCount), 0);
}

extern "C" void absolute_desktop_gpu_d3d11_draw_indexed(int64_t gpuHandle, int32_t indexCount) {
    D3D11Device* dev = DeviceFrom(gpuHandle);
    if (!dev || !dev->context || indexCount <= 0) return;
    D3D11Pipeline* pipe = PipelineFrom(dev->boundPipeline);
    D3D11Buffer* vb = BufferFrom(dev->boundBuffer);
    D3D11IndexBuffer* ib = IndexFrom(dev->boundIndexBuffer);
    if (!pipe || !vb || !ib || !pipe->layout || !vb->buffer || !ib->buffer) {
        SetError("D3D11 drawIndexed requires bound pipeline, VB, and IB");
        return;
    }
    BindPipelineState(dev, pipe);
    UINT stride = static_cast<UINT>(pipe->strideBytes);
    UINT offset = 0;
    ID3D11Buffer* bufs[] = {vb->buffer};
    dev->context->IASetVertexBuffers(0, 1, bufs, &stride, &offset);
    dev->context->IASetIndexBuffer(ib->buffer, DXGI_FORMAT_R32_UINT, 0);
    dev->context->DrawIndexed(static_cast<UINT>(indexCount), 0, 0);
}

extern "C" void absolute_desktop_gpu_d3d11_set_uniform_f(
    int64_t gpuHandle, const char* name, float value) {
    D3D11Device* dev = DeviceFrom(gpuHandle);
    if (!dev) return;
    D3D11Pipeline* pipe = PipelineFrom(dev->boundPipeline);
    if (!pipe || !pipe->shader) return;
    WriteUniformBytes(pipe->shader, name, &value, sizeof(float));
}

extern "C" void absolute_desktop_gpu_d3d11_set_uniform_i(
    int64_t gpuHandle, const char* name, int32_t value) {
    D3D11Device* dev = DeviceFrom(gpuHandle);
    if (!dev) return;
    D3D11Pipeline* pipe = PipelineFrom(dev->boundPipeline);
    if (!pipe || !pipe->shader) return;
    WriteUniformBytes(pipe->shader, name, &value, sizeof(int32_t));
}

extern "C" void absolute_desktop_gpu_d3d11_set_uniform_2f(
    int64_t gpuHandle, const char* name, float x, float y) {
    D3D11Device* dev = DeviceFrom(gpuHandle);
    if (!dev) return;
    D3D11Pipeline* pipe = PipelineFrom(dev->boundPipeline);
    if (!pipe || !pipe->shader) return;
    const float v[2] = {x, y};
    WriteUniformBytes(pipe->shader, name, v, sizeof(v));
}

// Soft sprite 0x00RRGGBB → RGBA8 (0 = transparent). Flip rows to match GL UV origin.
extern "C" int64_t absolute_desktop_gpu_d3d11_texture_from_sprite(
    int64_t gpuHandle, int64_t spriteHandle) {
    g_lastError.clear();
    D3D11Device* dev = DeviceFrom(gpuHandle);
    DesktopSpriteView* sprite = SpriteFrom(spriteHandle);
    if (!dev || !dev->device || !sprite || sprite->pixels.empty()) {
        SetError("D3D11 invalid sprite for texture upload");
        return 0;
    }
    const int32_t w = sprite->width;
    const int32_t h = sprite->height;
    if (w <= 0 || h <= 0) {
        SetError("D3D11 texture requires positive size");
        return 0;
    }

    std::vector<uint8_t> rgba(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
    for (int32_t y = 0; y < h; ++y) {
        const int32_t srcY = h - 1 - y;
        for (int32_t x = 0; x < w; ++x) {
            const uint32_t c = sprite->pixels[
                static_cast<size_t>(srcY) * static_cast<size_t>(w)
                + static_cast<size_t>(x)];
            const size_t di = (static_cast<size_t>(y) * static_cast<size_t>(w)
                + static_cast<size_t>(x)) * 4u;
            rgba[di + 0] = static_cast<uint8_t>((c >> 16) & 0xFF);
            rgba[di + 1] = static_cast<uint8_t>((c >> 8) & 0xFF);
            rgba[di + 2] = static_cast<uint8_t>(c & 0xFF);
            rgba[di + 3] = c == 0 ? 0 : 255;
        }
    }

    auto* tex = new D3D11Texture();
    tex->width = w;
    tex->height = h;

    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(w);
    td.Height = static_cast<UINT>(h);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sd{};
    sd.pSysMem = rgba.data();
    sd.SysMemPitch = static_cast<UINT>(w * 4);

    HRESULT hr = dev->device->CreateTexture2D(&td, &sd, &tex->texture);
    if (FAILED(hr) || !tex->texture) {
        SetError("D3D11 CreateTexture2D failed");
        delete tex;
        return 0;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
    srvd.Format = td.Format;
    srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvd.Texture2D.MipLevels = 1;
    hr = dev->device->CreateShaderResourceView(tex->texture, &srvd, &tex->srv);
    if (FAILED(hr) || !tex->srv) {
        SetError("D3D11 CreateShaderResourceView failed");
        tex->Destroy();
        delete tex;
        return 0;
    }
    return PtrToHandle(tex);
}

extern "C" void absolute_desktop_gpu_d3d11_texture_destroy(
    int64_t /*gpuHandle*/, int64_t textureHandle) {
    D3D11Texture* tex = TextureFrom(textureHandle);
    if (!tex) return;
    tex->Destroy();
    delete tex;
}

extern "C" int32_t absolute_desktop_gpu_d3d11_texture_width(int64_t textureHandle) {
    const D3D11Texture* tex = TextureFrom(textureHandle);
    return tex ? tex->width : 0;
}

extern "C" int32_t absolute_desktop_gpu_d3d11_texture_height(int64_t textureHandle) {
    const D3D11Texture* tex = TextureFrom(textureHandle);
    return tex ? tex->height : 0;
}

extern "C" void absolute_desktop_gpu_d3d11_bind_texture(
    int64_t gpuHandle, int64_t textureHandle, int32_t unit) {
    D3D11Device* dev = DeviceFrom(gpuHandle);
    D3D11Texture* tex = TextureFrom(textureHandle);
    if (!dev || !dev->context || !tex || !tex->srv) return;
    if (unit < 0) unit = 0;
    ID3D11ShaderResourceView* srvs[] = {tex->srv};
    dev->context->PSSetShaderResources(static_cast<UINT>(unit), 1, srvs);
    // Also bind to VS for completeness (rarely needed for sprites).
    dev->context->VSSetShaderResources(static_cast<UINT>(unit), 1, srvs);
}

// filter: 0 nearest, 1 linear. wrap: 0 clamp, 1 repeat, 2 mirror.
extern "C" int64_t absolute_desktop_gpu_d3d11_sampler_create(
    int64_t gpuHandle, int32_t filter, int32_t wrap) {
    g_lastError.clear();
    D3D11Device* dev = DeviceFrom(gpuHandle);
    if (!dev || !dev->device) {
        SetError("D3D11 sampler requires valid GPU");
        return 0;
    }

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = FilterToD3D(filter);
    sd.AddressU = WrapToD3D(wrap);
    sd.AddressV = WrapToD3D(wrap);
    sd.AddressW = WrapToD3D(wrap);
    sd.MaxAnisotropy = 1;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MinLOD = 0;
    sd.MaxLOD = D3D11_FLOAT32_MAX;

    auto* sampler = new D3D11Sampler();
    HRESULT hr = dev->device->CreateSamplerState(&sd, &sampler->state);
    if (FAILED(hr) || !sampler->state) {
        SetError("D3D11 CreateSamplerState failed");
        delete sampler;
        return 0;
    }
    return PtrToHandle(sampler);
}

extern "C" void absolute_desktop_gpu_d3d11_sampler_destroy(
    int64_t /*gpuHandle*/, int64_t samplerHandle) {
    D3D11Sampler* sampler = SamplerFrom(samplerHandle);
    if (!sampler) return;
    sampler->Destroy();
    delete sampler;
}

extern "C" void absolute_desktop_gpu_d3d11_bind_sampler(
    int64_t gpuHandle, int64_t samplerHandle, int32_t unit) {
    D3D11Device* dev = DeviceFrom(gpuHandle);
    D3D11Sampler* sampler = SamplerFrom(samplerHandle);
    if (!dev || !dev->context || !sampler || !sampler->state) return;
    if (unit < 0) unit = 0;
    ID3D11SamplerState* ss[] = {sampler->state};
    dev->context->PSSetSamplers(static_cast<UINT>(unit), 1, ss);
    dev->context->VSSetSamplers(static_cast<UINT>(unit), 1, ss);
}

#endif
