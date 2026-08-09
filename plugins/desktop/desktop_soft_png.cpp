// Soft-sprite PNG loader.
// Windows: WIC. Other platforms: portable decoder when zlib is available.
// Output: soft 0x00RRGGBB buffer (low alpha -> transparent 0).

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
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

#elif defined(ABSOLUTE_DESKTOP_HAS_ZLIB)

#include <zlib.h>

namespace {
    uint32_t ReadU32BE(const uint8_t* p) {
        return (static_cast<uint32_t>(p[0]) << 24)
            | (static_cast<uint32_t>(p[1]) << 16)
            | (static_cast<uint32_t>(p[2]) << 8)
            | static_cast<uint32_t>(p[3]);
    }

    bool InflateZlib(const std::vector<uint8_t>& src, std::vector<uint8_t>& dst, std::size_t expected) {
        dst.resize(expected);
        z_stream stream{};
        stream.next_in = const_cast<Bytef*>(src.data());
        stream.avail_in = static_cast<uInt>(src.size());
        stream.next_out = dst.data();
        stream.avail_out = static_cast<uInt>(dst.size());
        if (inflateInit(&stream) != Z_OK) return false;
        const int rc = inflate(&stream, Z_FINISH);
        inflateEnd(&stream);
        return rc == Z_STREAM_END && stream.total_out == expected;
    }

    void Paeth(uint8_t a, uint8_t b, uint8_t c, uint8_t& out) {
        const int p = static_cast<int>(a) + static_cast<int>(b) - static_cast<int>(c);
        const int pa = std::abs(p - static_cast<int>(a));
        const int pb = std::abs(p - static_cast<int>(b));
        const int pc = std::abs(p - static_cast<int>(c));
        // std::abs(int) from <cstdlib>
        if (pa <= pb && pa <= pc) out = a;
        else if (pb <= pc) out = b;
        else out = c;
    }

    bool DecodePngRgba(const std::vector<uint8_t>& file,
        std::vector<uint32_t>& soft, int32_t& width, int32_t& height) {
        if (file.size() < 8) return false;
        static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
        if (std::memcmp(file.data(), sig, 8) != 0) return false;

        uint32_t w = 0, h = 0;
        uint8_t bitDepth = 0, colorType = 0;
        std::vector<uint8_t> idat;

        std::size_t offset = 8;
        while (offset + 12 <= file.size()) {
            const uint32_t len = ReadU32BE(file.data() + offset);
            if (offset + 12 + len > file.size()) return false;
            const char* type = reinterpret_cast<const char*>(file.data() + offset + 4);
            const uint8_t* data = file.data() + offset + 8;
            if (std::memcmp(type, "IHDR", 4) == 0) {
                if (len < 13) return false;
                w = ReadU32BE(data);
                h = ReadU32BE(data + 4);
                bitDepth = data[8];
                colorType = data[9];
                if (data[10] != 0 || data[11] != 0 || data[12] != 0) return false; // only deflate, no interlace
            } else if (std::memcmp(type, "IDAT", 4) == 0) {
                idat.insert(idat.end(), data, data + len);
            } else if (std::memcmp(type, "IEND", 4) == 0) {
                break;
            }
            offset += 12 + len;
        }

        if (w == 0 || h == 0 || w > 16384 || h > 16384) return false;
        if (bitDepth != 8) return false;
        if (colorType != 2 && colorType != 6) return false; // RGB or RGBA
        const int bpp = (colorType == 6) ? 4 : 3;
        const std::size_t stride = static_cast<std::size_t>(w) * static_cast<std::size_t>(bpp);
        const std::size_t rawSize = (stride + 1) * static_cast<std::size_t>(h);
        std::vector<uint8_t> raw;
        if (!InflateZlib(idat, raw, rawSize)) return false;

        std::vector<uint8_t> rgba(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u);
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t filter = raw[y * (stride + 1)];
            const uint8_t* src = raw.data() + y * (stride + 1) + 1;
            uint8_t* dst = rgba.data() + static_cast<std::size_t>(y) * w * 4u;
            const uint8_t* prev = y > 0 ? rgba.data() + static_cast<std::size_t>(y - 1) * w * 4u : nullptr;

            // Defilter into a temporary row of bpp*w, then expand to RGBA.
            std::vector<uint8_t> row(stride);
            for (std::size_t i = 0; i < stride; ++i) {
                const uint8_t x = src[i];
                const uint8_t a = (i >= static_cast<std::size_t>(bpp)) ? row[i - static_cast<std::size_t>(bpp)] : 0;
                // Previous scanline is in reconstructed bpp packing — track parallel recon buffer.
                uint8_t b = 0;
                uint8_t c = 0;
                // We'll reconstruct into `row` then copy to rgba; for prev use last full rgba converted.
                // Simpler: maintain reconBpp rows.
                (void)b; (void)c; (void)prev;
                row[i] = x; // filled below by filter type
            }

            // Proper recon with previous bpp row.
            static thread_local std::vector<uint8_t> prevRow;
            if (prevRow.size() != stride) prevRow.assign(stride, 0);
            for (std::size_t i = 0; i < stride; ++i) {
                const uint8_t x = src[i];
                const uint8_t left = (i >= static_cast<std::size_t>(bpp)) ? row[i - static_cast<std::size_t>(bpp)] : 0;
                const uint8_t up = prevRow[i];
                const uint8_t upLeft = (i >= static_cast<std::size_t>(bpp)) ? prevRow[i - static_cast<std::size_t>(bpp)] : 0;
                uint8_t val = x;
                switch (filter) {
                case 0: val = x; break;
                case 1: val = static_cast<uint8_t>(x + left); break;
                case 2: val = static_cast<uint8_t>(x + up); break;
                case 3: val = static_cast<uint8_t>(x + ((left + up) / 2)); break;
                case 4: {
                    uint8_t p = 0;
                    Paeth(left, up, upLeft, p);
                    val = static_cast<uint8_t>(x + p);
                    break;
                }
                default: return false;
                }
                row[i] = val;
            }
            prevRow = row;

            for (uint32_t x = 0; x < w; ++x) {
                const uint8_t* px = row.data() + static_cast<std::size_t>(x) * bpp;
                dst[x * 4u + 0] = px[0];
                dst[x * 4u + 1] = px[1];
                dst[x * 4u + 2] = px[2];
                dst[x * 4u + 3] = (bpp == 4) ? px[3] : 255;
            }
        }

        soft.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
        for (uint32_t i = 0; i < w * h; ++i) {
            const uint8_t r = rgba[i * 4u + 0];
            const uint8_t g = rgba[i * 4u + 1];
            const uint8_t b = rgba[i * 4u + 2];
            const uint8_t a = rgba[i * 4u + 3];
            uint32_t color = 0;
            if (a >= 8) {
                color = (static_cast<uint32_t>(r) << 16U)
                    | (static_cast<uint32_t>(g) << 8U)
                    | static_cast<uint32_t>(b);
                if (color == 0) color = 0x000001u;
            }
            soft[i] = color;
        }
        width = static_cast<int32_t>(w);
        height = static_cast<int32_t>(h);
        return true;
    }
}

extern "C" int64_t absolute_desktop_sprite_load_png(const char* path) {
    if (!path || !path[0]) return 0;
    std::ifstream file(path, std::ios::binary);
    if (!file) return 0;
    file.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    if (size < 33 || size > 64u * 1024u * 1024u) return 0;
    std::vector<uint8_t> data(size);
    if (!file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size))) return 0;

    std::vector<uint32_t> soft;
    int32_t w = 0, h = 0;
    if (!DecodePngRgba(data, soft, w, h)) return 0;
    return absolute_desktop_sprite_from_pixels(w, h, soft.data());
}

#else

extern "C" int64_t absolute_desktop_sprite_load_png(const char*) {
    return 0;
}

#endif
