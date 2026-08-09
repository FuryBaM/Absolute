// Soft TTF / system font rasterizer (Windows GDI).
// Creates a SoftFont handle, measures UTF-8 text, draws into window blit or sprite pixels.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

extern "C" void absolute_desktop_blit(
    int64_t handle, int32_t destX, int32_t destY, int32_t width, int32_t height, const uint32_t* pixels);

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace {
    struct SoftFont {
        HFONT font = nullptr;
        int32_t pixelHeight = 16;
        std::wstring privatePath;
        bool hasPrivateResource = false;
    };

    SoftFont* FontFromHandle(int64_t handle) {
        return reinterpret_cast<SoftFont*>(static_cast<intptr_t>(handle));
    }

    int64_t FontToHandle(SoftFont* font) {
        return static_cast<int64_t>(reinterpret_cast<intptr_t>(font));
    }

    // Soft sprite layout (must match desktop_soft_sprites.cpp).
    struct DesktopSpriteView {
        int32_t width = 1;
        int32_t height = 1;
        std::vector<uint32_t> pixels;
    };

    DesktopSpriteView* SpriteFromHandle(int64_t handle) {
        return reinterpret_cast<DesktopSpriteView*>(static_cast<intptr_t>(handle));
    }

    std::wstring Utf8ToWide(const char* text) {
        if (!text) return L"";
        const int size = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
        if (size <= 0) return L"";
        std::wstring result(static_cast<std::size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), size);
        if (!result.empty() && result.back() == L'\0') result.pop_back();
        return result;
    }

    // Best-effort family name from TTF/OTF name table (Windows Unicode, English).
    std::wstring ReadFontFamilyFromFile(const std::wstring& path) {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return L"";

        LARGE_INTEGER fileSize{};
        if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart < 12 || fileSize.QuadPart > 64 * 1024 * 1024) {
            CloseHandle(file);
            return L"";
        }

        std::vector<uint8_t> data(static_cast<std::size_t>(fileSize.QuadPart));
        DWORD read = 0;
        if (!ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr)
            || read != data.size()) {
            CloseHandle(file);
            return L"";
        }
        CloseHandle(file);

        auto u16be = [](const uint8_t* p) -> uint16_t {
            return static_cast<uint16_t>((p[0] << 8) | p[1]);
        };
        auto u32be = [](const uint8_t* p) -> uint32_t {
            return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16)
                | (static_cast<uint32_t>(p[2]) << 8) | p[3];
        };

        if (data.size() < 12) return L"";
        const uint16_t numTables = u16be(data.data() + 4);
        std::size_t nameOffset = 0;
        uint32_t nameLength = 0;
        for (uint16_t i = 0; i < numTables; ++i) {
            const std::size_t entry = 12 + static_cast<std::size_t>(i) * 16;
            if (entry + 16 > data.size()) break;
            const char tag[5] = {
                static_cast<char>(data[entry]), static_cast<char>(data[entry + 1]),
                static_cast<char>(data[entry + 2]), static_cast<char>(data[entry + 3]), 0
            };
            if (std::strcmp(tag, "name") == 0) {
                nameOffset = u32be(data.data() + entry + 8);
                nameLength = u32be(data.data() + entry + 12);
                break;
            }
        }
        if (nameOffset == 0 || nameOffset + 6 > data.size()) return L"";
        if (nameOffset + nameLength > data.size()) nameLength = static_cast<uint32_t>(data.size() - nameOffset);

        const uint8_t* name = data.data() + nameOffset;
        const uint16_t count = u16be(name + 2);
        const uint16_t stringOffset = u16be(name + 4);
        std::wstring best;
        for (uint16_t i = 0; i < count; ++i) {
            const std::size_t rec = 6 + static_cast<std::size_t>(i) * 12;
            if (rec + 12 > nameLength) break;
            const uint16_t platformID = u16be(name + rec);
            const uint16_t encodingID = u16be(name + rec + 2);
            const uint16_t languageID = u16be(name + rec + 4);
            const uint16_t nameID = u16be(name + rec + 6);
            const uint16_t length = u16be(name + rec + 8);
            const uint16_t offset = u16be(name + rec + 10);
            // Prefer Windows Unicode English family (nameID 1) or full name (4).
            if (platformID != 3 || (encodingID != 1 && encodingID != 10)) continue;
            if (nameID != 1 && nameID != 4) continue;
            if (languageID != 0x0409 && languageID != 0 && best.empty() == false && nameID != 1) continue;
            const std::size_t strPos = stringOffset + offset;
            if (strPos + length > nameLength || (length % 2) != 0) continue;
            std::wstring value;
            value.reserve(length / 2);
            for (uint16_t c = 0; c + 1 < length; c += 2) {
                value.push_back(static_cast<wchar_t>(u16be(name + strPos + c)));
            }
            if (value.empty()) continue;
            if (nameID == 1 && (languageID == 0x0409 || best.empty())) {
                best = value;
                if (languageID == 0x0409) break;
            } else if (best.empty() && nameID == 4) {
                best = value;
            }
        }
        return best;
    }

    HFONT CreateGdiFont(const std::wstring& face, int32_t pixelHeight, int32_t style) {
        if (pixelHeight <= 0) pixelHeight = 16;
        if (pixelHeight > 512) pixelHeight = 512;
        const int weight = (style & 1) ? FW_BOLD : FW_NORMAL;
        const BYTE italic = (style & 2) ? TRUE : FALSE;
        return CreateFontW(
            -pixelHeight, 0, 0, 0, weight, italic, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            face.empty() ? L"Segoe UI" : face.c_str());
    }

    struct LineInfo {
        std::wstring text;
        int32_t width = 0;
    };

    bool MeasureAndSplit(HDC dc, const std::wstring& text, std::vector<LineInfo>& lines, int32_t& totalW, int32_t& totalH, int32_t lineH) {
        lines.clear();
        totalW = 0;
        totalH = 0;
        std::wstring current;
        auto flush = [&]() {
            LineInfo line;
            line.text = current;
            SIZE size{};
            if (!line.text.empty()) {
                GetTextExtentPoint32W(dc, line.text.c_str(), static_cast<int>(line.text.size()), &size);
                line.width = size.cx;
            } else {
                line.width = 0;
            }
            totalW = std::max(totalW, line.width);
            lines.push_back(line);
            current.clear();
        };

        for (wchar_t ch : text) {
            if (ch == L'\n') {
                flush();
            } else if (ch == L'\r') {
                continue;
            } else {
                current.push_back(ch);
            }
        }
        flush();
        if (lines.empty()) {
            lines.push_back(LineInfo{});
        }
        totalH = lineH * static_cast<int32_t>(lines.size());
        if (totalW <= 0) totalW = 1;
        if (totalH <= 0) totalH = lineH > 0 ? lineH : 1;
        return true;
    }

    bool RasterizeText(
        SoftFont* font, const char* utf8, uint32_t color,
        std::vector<uint32_t>& outPixels, int32_t& outW, int32_t& outH) {
        if (!font || !font->font) return false;
        const std::wstring text = Utf8ToWide(utf8 ? utf8 : "");

        HDC screen = GetDC(nullptr);
        if (!screen) return false;
        HDC dc = CreateCompatibleDC(screen);
        if (!dc) {
            ReleaseDC(nullptr, screen);
            return false;
        }
        HGDIOBJ oldFont = SelectObject(dc, font->font);

        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        const int32_t lineH = tm.tmHeight + tm.tmExternalLeading;
        std::vector<LineInfo> lines;
        int32_t totalW = 0;
        int32_t totalH = 0;
        MeasureAndSplit(dc, text, lines, totalW, totalH, lineH);
        // Padding so ClearType fringes fit.
        totalW += 2;
        totalH += 2;
        if (totalW > 8192) totalW = 8192;
        if (totalH > 8192) totalH = 8192;

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = totalW;
        bmi.bmiHeader.biHeight = -totalH; // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!dib || !bits) {
            SelectObject(dc, oldFont);
            DeleteDC(dc);
            ReleaseDC(nullptr, screen);
            return false;
        }
        HGDIOBJ oldBmp = SelectObject(dc, dib);
        RECT rect{0, 0, totalW, totalH};
        HBRUSH black = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        FillRect(dc, &rect, black);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));

        int32_t y = 1;
        for (const LineInfo& line : lines) {
            if (!line.text.empty()) {
                TextOutW(dc, 1, y, line.text.c_str(), static_cast<int>(line.text.size()));
            }
            y += lineH;
        }

        outW = totalW;
        outH = totalH;
        outPixels.assign(static_cast<std::size_t>(totalW) * static_cast<std::size_t>(totalH), 0);
        const uint32_t* src = static_cast<const uint32_t*>(bits);
        const uint32_t rgb = color & 0x00FFFFFFu;
        const uint32_t writeColor = rgb == 0 ? 0x000001u : rgb;
        for (int32_t i = 0; i < totalW * totalH; ++i) {
            // DIB is BGRA in memory on little-endian Windows.
            const uint32_t px = src[i];
            const uint32_t b = px & 0xFF;
            const uint32_t g = (px >> 8) & 0xFF;
            const uint32_t r = (px >> 16) & 0xFF;
            const uint32_t lum = (r * 30 + g * 59 + b * 11) / 100;
            if (lum >= 24) {
                outPixels[static_cast<std::size_t>(i)] = writeColor;
            }
        }

        SelectObject(dc, oldBmp);
        SelectObject(dc, oldFont);
        DeleteObject(dib);
        DeleteDC(dc);
        ReleaseDC(nullptr, screen);
        return true;
    }

    int32_t MeasureWidth(SoftFont* font, const char* utf8) {
        if (!font || !font->font) return 0;
        HDC screen = GetDC(nullptr);
        if (!screen) return 0;
        HDC dc = CreateCompatibleDC(screen);
        HGDIOBJ oldFont = SelectObject(dc, font->font);
        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        std::vector<LineInfo> lines;
        int32_t totalW = 0;
        int32_t totalH = 0;
        MeasureAndSplit(dc, Utf8ToWide(utf8), lines, totalW, totalH, tm.tmHeight);
        SelectObject(dc, oldFont);
        DeleteDC(dc);
        ReleaseDC(nullptr, screen);
        return totalW;
    }

    int32_t MeasureHeight(SoftFont* font, const char* utf8) {
        if (!font || !font->font) return 0;
        HDC screen = GetDC(nullptr);
        if (!screen) return 0;
        HDC dc = CreateCompatibleDC(screen);
        HGDIOBJ oldFont = SelectObject(dc, font->font);
        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        std::vector<LineInfo> lines;
        int32_t totalW = 0;
        int32_t totalH = 0;
        MeasureAndSplit(dc, Utf8ToWide(utf8), lines, totalW, totalH, tm.tmHeight + tm.tmExternalLeading);
        SelectObject(dc, oldFont);
        DeleteDC(dc);
        ReleaseDC(nullptr, screen);
        return totalH;
    }
}

extern "C" int64_t absolute_desktop_font_create(const char* faceName, int32_t pixelHeight, int32_t style) {
    auto* font = new SoftFont();
    font->pixelHeight = pixelHeight > 0 ? pixelHeight : 16;
    const std::wstring face = Utf8ToWide(faceName && faceName[0] ? faceName : "Segoe UI");
    font->font = CreateGdiFont(face, font->pixelHeight, style);
    if (!font->font) {
        delete font;
        return 0;
    }
    return FontToHandle(font);
}

extern "C" int64_t absolute_desktop_font_load_file(const char* path, int32_t pixelHeight, int32_t style) {
    if (!path || !path[0]) return 0;
    const std::wstring widePath = Utf8ToWide(path);
    if (widePath.empty()) return 0;

    if (AddFontResourceExW(widePath.c_str(), FR_PRIVATE, 0) == 0) {
        return 0;
    }

    std::wstring family = ReadFontFamilyFromFile(widePath);
    if (family.empty()) {
        // Fallback: file stem often matches family for simple installs.
        const std::size_t slash = widePath.find_last_of(L"\\/");
        const std::size_t start = slash == std::wstring::npos ? 0 : slash + 1;
        const std::size_t dot = widePath.find_last_of(L'.');
        family = widePath.substr(start, dot != std::wstring::npos && dot > start ? dot - start : std::wstring::npos);
    }

    auto* font = new SoftFont();
    font->pixelHeight = pixelHeight > 0 ? pixelHeight : 16;
    font->privatePath = widePath;
    font->hasPrivateResource = true;
    font->font = CreateGdiFont(family, font->pixelHeight, style);
    if (!font->font) {
        RemoveFontResourceExW(widePath.c_str(), FR_PRIVATE, 0);
        delete font;
        return 0;
    }
    return FontToHandle(font);
}

extern "C" void absolute_desktop_font_destroy(int64_t handle) {
    SoftFont* font = FontFromHandle(handle);
    if (!font) return;
    if (font->font) DeleteObject(font->font);
    if (font->hasPrivateResource && !font->privatePath.empty()) {
        RemoveFontResourceExW(font->privatePath.c_str(), FR_PRIVATE, 0);
    }
    delete font;
}

extern "C" int32_t absolute_desktop_font_is_valid(int64_t handle) {
    const SoftFont* font = FontFromHandle(handle);
    return font && font->font ? 1 : 0;
}

extern "C" int32_t absolute_desktop_font_line_height(int64_t handle) {
    SoftFont* font = FontFromHandle(handle);
    if (!font || !font->font) return 0;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HGDIOBJ oldFont = SelectObject(dc, font->font);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    const int32_t h = tm.tmHeight + tm.tmExternalLeading;
    SelectObject(dc, oldFont);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return h;
}

extern "C" int32_t absolute_desktop_font_measure(int64_t handle, const char* text) {
    return MeasureWidth(FontFromHandle(handle), text);
}

extern "C" int32_t absolute_desktop_font_measure_height(int64_t handle, const char* text) {
    return MeasureHeight(FontFromHandle(handle), text);
}

extern "C" void absolute_desktop_font_draw_window(
    int64_t windowHandle, int64_t fontHandle, int32_t x, int32_t y,
    const char* text, uint32_t color) {
    SoftFont* font = FontFromHandle(fontHandle);
    std::vector<uint32_t> pixels;
    int32_t w = 0;
    int32_t h = 0;
    if (!RasterizeText(font, text, color, pixels, w, h) || pixels.empty()) return;
    absolute_desktop_blit(windowHandle, x, y, w, h, pixels.data());
}

extern "C" void absolute_desktop_font_draw_sprite(
    int64_t spriteHandle, int64_t fontHandle, int32_t x, int32_t y,
    const char* text, uint32_t color) {
    SoftFont* font = FontFromHandle(fontHandle);
    DesktopSpriteView* sprite = SpriteFromHandle(spriteHandle);
    if (!sprite || sprite->pixels.empty()) return;
    std::vector<uint32_t> pixels;
    int32_t w = 0;
    int32_t h = 0;
    if (!RasterizeText(font, text, color, pixels, w, h) || pixels.empty()) return;

    for (int32_t row = 0; row < h; ++row) {
        const int32_t dy = y + row;
        if (dy < 0 || dy >= sprite->height) continue;
        for (int32_t col = 0; col < w; ++col) {
            const int32_t dx = x + col;
            if (dx < 0 || dx >= sprite->width) continue;
            const uint32_t px = pixels[static_cast<std::size_t>(row) * static_cast<std::size_t>(w)
                + static_cast<std::size_t>(col)];
            if (px == 0) continue;
            sprite->pixels[static_cast<std::size_t>(dy) * static_cast<std::size_t>(sprite->width)
                + static_cast<std::size_t>(dx)] = px;
        }
    }
}

#else

extern "C" int64_t absolute_desktop_font_create(const char*, int32_t, int32_t) { return 0; }
extern "C" int64_t absolute_desktop_font_load_file(const char*, int32_t, int32_t) { return 0; }
extern "C" void absolute_desktop_font_destroy(int64_t) {}
extern "C" int32_t absolute_desktop_font_is_valid(int64_t) { return 0; }
extern "C" int32_t absolute_desktop_font_line_height(int64_t) { return 0; }
extern "C" int32_t absolute_desktop_font_measure(int64_t, const char*) { return 0; }
extern "C" int32_t absolute_desktop_font_measure_height(int64_t, const char*) { return 0; }
extern "C" void absolute_desktop_font_draw_window(int64_t, int64_t, int32_t, int32_t, const char*, uint32_t) {}
extern "C" void absolute_desktop_font_draw_sprite(int64_t, int64_t, int32_t, int32_t, const char*, uint32_t) {}

#endif
