// Shared soft-sprite buffers + BMP loader (platform-independent).
// Window blits go through absolute_desktop_blit provided by the platform runtime.

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <vector>

extern "C" void absolute_desktop_blit(
    int64_t handle, int32_t destX, int32_t destY, int32_t width, int32_t height, const uint32_t* pixels);
extern "C" void absolute_desktop_blit_strided(
    int64_t handle, int32_t destX, int32_t destY, int32_t width, int32_t height,
    const uint32_t* pixels, int32_t pitch);

namespace {
    struct DesktopSprite {
        int32_t width = 1;
        int32_t height = 1;
        std::vector<uint32_t> pixels;
    };

    DesktopSprite* SpriteFromHandle(int64_t handle) {
        return reinterpret_cast<DesktopSprite*>(static_cast<intptr_t>(handle));
    }

    int64_t SpriteToHandle(DesktopSprite* sprite) {
        return static_cast<int64_t>(reinterpret_cast<intptr_t>(sprite));
    }

    uint32_t ReadU32LE(const uint8_t* p) {
        return static_cast<uint32_t>(p[0])
            | (static_cast<uint32_t>(p[1]) << 8)
            | (static_cast<uint32_t>(p[2]) << 16)
            | (static_cast<uint32_t>(p[3]) << 24);
    }

    uint16_t ReadU16LE(const uint8_t* p) {
        return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
    }

    // Decode 24/32-bit uncompressed BMP (BI_RGB / BI_BITFIELDS 32 with BGRA) into 0x00RRGGBB.
    // Pixel value 0 remains transparent for soft blits. Returns null on failure.
    DesktopSprite* LoadBmpFile(const char* path) {
        if (!path || !path[0]) return nullptr;

        std::ifstream file(path, std::ios::binary);
        if (!file) return nullptr;

        file.seekg(0, std::ios::end);
        const auto size = static_cast<std::size_t>(file.tellg());
        file.seekg(0, std::ios::beg);
        if (size < 54 || size > 64u * 1024u * 1024u) return nullptr;

        std::vector<uint8_t> data(size);
        if (!file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size))) {
            return nullptr;
        }

        if (data[0] != 'B' || data[1] != 'M') return nullptr;

        const uint32_t pixelOffset = ReadU32LE(data.data() + 10);
        const uint32_t dibSize = ReadU32LE(data.data() + 14);
        if (dibSize < 40 || pixelOffset >= size) return nullptr;

        const int32_t width = static_cast<int32_t>(ReadU32LE(data.data() + 18));
        int32_t heightRaw = static_cast<int32_t>(ReadU32LE(data.data() + 22));
        const uint16_t planes = ReadU16LE(data.data() + 26);
        const uint16_t bitCount = ReadU16LE(data.data() + 28);
        const uint32_t compression = ReadU32LE(data.data() + 30);

        if (planes != 1 || width <= 0 || width > 16384) return nullptr;
        if (bitCount != 24 && bitCount != 32) return nullptr;
        // BI_RGB = 0; BI_BITFIELDS = 3 (common for 32-bit).
        if (compression != 0 && !(compression == 3 && bitCount == 32)) return nullptr;

        const bool topDown = heightRaw < 0;
        const int32_t height = topDown ? -heightRaw : heightRaw;
        if (height <= 0 || height > 16384) return nullptr;

        const int32_t bytesPerPixel = bitCount / 8;
        const int32_t rowStride = ((width * bytesPerPixel + 3) / 4) * 4;
        const std::size_t needed = static_cast<std::size_t>(pixelOffset)
            + static_cast<std::size_t>(rowStride) * static_cast<std::size_t>(height);
        if (needed > size) return nullptr;

        auto* sprite = new DesktopSprite();
        sprite->width = width;
        sprite->height = height;
        sprite->pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);

        for (int32_t y = 0; y < height; ++y) {
            const int32_t srcY = topDown ? y : (height - 1 - y);
            const uint8_t* row = data.data() + pixelOffset
                + static_cast<std::size_t>(srcY) * static_cast<std::size_t>(rowStride);
            for (int32_t x = 0; x < width; ++x) {
                const uint8_t* px = row + static_cast<std::size_t>(x) * bytesPerPixel;
                const uint32_t b = px[0];
                const uint32_t g = px[1];
                const uint32_t r = px[2];
                // Soft buffer is 0x00RRGGBB; pure black becomes transparent via blit skip.
                // Keep non-black blacks as 0x000001 to avoid accidental holes — only exact 0 skips.
                const uint32_t color = (r << 16U) | (g << 8U) | b;
                sprite->pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
                    + static_cast<std::size_t>(x)] = color;
            }
        }
        return sprite;
    }
}

extern "C" int64_t absolute_desktop_sprite_create(int32_t width, int32_t height) {
    if (width <= 0 || height <= 0) return 0;
    auto* sprite = new DesktopSprite();
    sprite->width = width;
    sprite->height = height;
    sprite->pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    return SpriteToHandle(sprite);
}

extern "C" int64_t absolute_desktop_sprite_load_bmp(const char* path) {
    DesktopSprite* sprite = LoadBmpFile(path);
    return sprite ? SpriteToHandle(sprite) : 0;
}

// Adopt a 0x00RRGGBB pixel buffer into a soft sprite (used by PNG loader, etc.).
extern "C" int64_t absolute_desktop_sprite_from_pixels(
    int32_t width, int32_t height, const uint32_t* pixels) {
    if (width <= 0 || height <= 0 || !pixels) return 0;
    if (width > 16384 || height > 16384) return 0;
    auto* sprite = new DesktopSprite();
    sprite->width = width;
    sprite->height = height;
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    sprite->pixels.assign(pixels, pixels + count);
    return SpriteToHandle(sprite);
}

extern "C" void absolute_desktop_sprite_destroy(int64_t handle) {
    delete SpriteFromHandle(handle);
}

extern "C" int32_t absolute_desktop_sprite_width(int64_t handle) {
    const DesktopSprite* sprite = SpriteFromHandle(handle);
    return sprite ? sprite->width : 0;
}

extern "C" int32_t absolute_desktop_sprite_height(int64_t handle) {
    const DesktopSprite* sprite = SpriteFromHandle(handle);
    return sprite ? sprite->height : 0;
}

extern "C" void absolute_desktop_sprite_clear(int64_t handle, uint32_t color) {
    DesktopSprite* sprite = SpriteFromHandle(handle);
    if (sprite) std::fill(sprite->pixels.begin(), sprite->pixels.end(), color);
}

extern "C" void absolute_desktop_sprite_pixel(int64_t handle, int32_t x, int32_t y, uint32_t color) {
    DesktopSprite* sprite = SpriteFromHandle(handle);
    if (!sprite || x < 0 || y < 0 || x >= sprite->width || y >= sprite->height) return;
    sprite->pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(sprite->width)
        + static_cast<std::size_t>(x)] = color;
}

extern "C" void absolute_desktop_sprite_fill_rect(
    int64_t handle, int32_t x, int32_t y, int32_t width, int32_t height, uint32_t color) {
    DesktopSprite* sprite = SpriteFromHandle(handle);
    if (!sprite || width <= 0 || height <= 0) return;
    const int32_t left = std::clamp<int32_t>(x, 0, sprite->width);
    const int32_t top = std::clamp<int32_t>(y, 0, sprite->height);
    const int32_t right = std::clamp<int32_t>(x + width, 0, sprite->width);
    const int32_t bottom = std::clamp<int32_t>(y + height, 0, sprite->height);
    for (int32_t row = top; row < bottom; ++row) {
        auto begin = sprite->pixels.begin()
            + static_cast<std::size_t>(row) * sprite->width + left;
        std::fill(begin, begin + (right - left), color);
    }
}

extern "C" void absolute_desktop_sprite_fill_circle(
    int64_t handle, int32_t cx, int32_t cy, int32_t radius, uint32_t color) {
    DesktopSprite* sprite = SpriteFromHandle(handle);
    if (!sprite || radius <= 0) return;
    const int32_t r2 = radius * radius;
    for (int32_t y = cy - radius; y <= cy + radius; ++y) {
        if (y < 0 || y >= sprite->height) continue;
        const int32_t dy = y - cy;
        const int32_t span2 = r2 - dy * dy;
        if (span2 < 0) continue;
        int32_t span = 0;
        while ((span + 1) * (span + 1) <= span2) ++span;
        const int32_t left = std::max(0, cx - span);
        const int32_t right = std::min(sprite->width - 1, cx + span);
        auto begin = sprite->pixels.begin()
            + static_cast<std::size_t>(y) * sprite->width + left;
        std::fill(begin, begin + (right - left + 1), color);
    }
}

extern "C" void absolute_desktop_sprite_color_key(int64_t handle, uint32_t color) {
    DesktopSprite* sprite = SpriteFromHandle(handle);
    if (!sprite) return;
    const uint32_t key = color & 0x00FFFFFFu;
    for (uint32_t& px : sprite->pixels) {
        if ((px & 0x00FFFFFFu) == key) px = 0;
    }
}

extern "C" void absolute_desktop_sprite_draw(int64_t windowHandle, int64_t spriteHandle, int32_t x, int32_t y) {
    DesktopSprite* sprite = SpriteFromHandle(spriteHandle);
    if (!sprite || sprite->pixels.empty()) return;
    absolute_desktop_blit(windowHandle, x, y, sprite->width, sprite->height, sprite->pixels.data());
}

extern "C" void absolute_desktop_sprite_draw_rect(
    int64_t windowHandle, int64_t spriteHandle,
    int32_t destX, int32_t destY,
    int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH) {
    DesktopSprite* sprite = SpriteFromHandle(spriteHandle);
    if (!sprite || sprite->pixels.empty() || srcW <= 0 || srcH <= 0) return;

    // Clip source rect to sprite bounds.
    if (srcX < 0) {
        destX -= srcX;
        srcW += srcX;
        srcX = 0;
    }
    if (srcY < 0) {
        destY -= srcY;
        srcH += srcY;
        srcY = 0;
    }
    if (srcX >= sprite->width || srcY >= sprite->height) return;
    if (srcX + srcW > sprite->width) srcW = sprite->width - srcX;
    if (srcY + srcH > sprite->height) srcH = sprite->height - srcY;
    if (srcW <= 0 || srcH <= 0) return;

    // Zero-copy atlas sub-rect blit via row pitch.
    const uint32_t* origin = sprite->pixels.data()
        + static_cast<std::size_t>(srcY) * static_cast<std::size_t>(sprite->width)
        + static_cast<std::size_t>(srcX);
    absolute_desktop_blit_strided(windowHandle, destX, destY, srcW, srcH, origin, sprite->width);
}
