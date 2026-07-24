// Soft-sprite PNG loader.
// Windows: WIC (windowscodecs) → soft 0x00RRGGBB buffer. Other platforms: stub.

#include <cstdint>
#include <string>
#include <vector>

extern "C" int64_t absolute_desktop_sprite_from_pixels(
    int32_t width, int32_t height, const uint32_t* pixels);

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#include <wincodec.h>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace {
    struct ComInit {
        HRESULT hr = E_FAIL;
        bool shouldUninit = false;
        ComInit() {
            hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (hr == S_OK) {
                shouldUninit = true;
            } else if (hr == S_FALSE || hr == RPC_E_CHANGED_MODE) {
                // Already initialized (possibly different mode) — still usable.
                hr = S_OK;
                shouldUninit = false;
            }
        }
        ~ComInit() {
            if (shouldUninit) CoUninitialize();
        }
        bool ok() const { return SUCCEEDED(hr); }
    };

    std::wstring Utf8ToWide(const char* text) {
        if (!text || !text[0]) return L"";
        const int size = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
        if (size <= 0) return L"";
        std::wstring result(static_cast<std::size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), size);
        if (!result.empty() && result.back() == L'\0') result.pop_back();
        return result;
    }
}

extern "C" int64_t absolute_desktop_sprite_load_png(const char* path) {
    if (!path || !path[0]) return 0;

    ComInit com;
    if (!com.ok()) return 0;

    const std::wstring wide = Utf8ToWide(path);
    if (wide.empty()) return 0;

    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) return 0;

    IWICBitmapDecoder* decoder = nullptr;
    hr = factory->CreateDecoderFromFilename(
        wide.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr) || !decoder) {
        factory->Release();
        return 0;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) {
        decoder->Release();
        factory->Release();
        return 0;
    }

    IWICFormatConverter* converter = nullptr;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr) || !converter) {
        frame->Release();
        decoder->Release();
        factory->Release();
        return 0;
    }

    hr = converter->Initialize(
        frame,
        GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        converter->Release();
        frame->Release();
        decoder->Release();
        factory->Release();
        return 0;
    }

    UINT width = 0;
    UINT height = 0;
    hr = converter->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0 || width > 16384 || height > 16384) {
        converter->Release();
        frame->Release();
        decoder->Release();
        factory->Release();
        return 0;
    }

    const UINT stride = width * 4u;
    const UINT bufferSize = stride * height;
    std::vector<uint8_t> rgba(bufferSize);
    hr = converter->CopyPixels(nullptr, stride, bufferSize, rgba.data());
    converter->Release();
    frame->Release();
    decoder->Release();
    factory->Release();
    if (FAILED(hr)) return 0;

    std::vector<uint32_t> soft(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (UINT y = 0; y < height; ++y) {
        for (UINT x = 0; x < width; ++x) {
            const uint8_t* px = rgba.data()
                + static_cast<std::size_t>(y) * stride
                + static_cast<std::size_t>(x) * 4u;
            const uint8_t r = px[0];
            const uint8_t g = px[1];
            const uint8_t b = px[2];
            const uint8_t a = px[3];
            uint32_t color = 0;
            if (a >= 8) {
                color = (static_cast<uint32_t>(r) << 16U)
                    | (static_cast<uint32_t>(g) << 8U)
                    | static_cast<uint32_t>(b);
                if (color == 0) color = 0x000001u;
            }
            soft[static_cast<std::size_t>(y) * width + x] = color;
        }
    }

    return absolute_desktop_sprite_from_pixels(
        static_cast<int32_t>(width), static_cast<int32_t>(height), soft.data());
}

#else

extern "C" int64_t absolute_desktop_sprite_load_png(const char*) {
    // Portable libpng/stb path can land later; soft BMP remains cross-platform.
    return 0;
}

#endif
