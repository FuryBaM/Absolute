// Soft sprite batch: queue atlas / sprite draws and flush in one pass.
// drawSpriteRect uses zero-copy strided blits; batching avoids per-call Absolute
// overhead and auto-flushes when the queue hits capacity.

#include <cstdint>
#include <vector>

extern "C" void absolute_desktop_sprite_draw(
    int64_t windowHandle, int64_t spriteHandle, int32_t x, int32_t y);
extern "C" void absolute_desktop_sprite_draw_rect(
    int64_t windowHandle, int64_t spriteHandle,
    int32_t destX, int32_t destY,
    int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH);
extern "C" int32_t absolute_desktop_sprite_width(int64_t handle);
extern "C" int32_t absolute_desktop_sprite_height(int64_t handle);

namespace {
    constexpr int32_t kMaxBatchEntries = 8192;

    struct BatchEntry {
        int64_t spriteHandle = 0; // 0 => batch atlas
        int32_t destX = 0;
        int32_t destY = 0;
        int32_t srcX = 0;
        int32_t srcY = 0;
        int32_t srcW = 0; // 0 => full sprite
        int32_t srcH = 0;
    };

    struct SpriteBatch {
        int64_t windowHandle = 0;
        int64_t atlasHandle = 0;
        std::vector<BatchEntry> entries;
        bool active = false;
    };

    SpriteBatch* BatchFromHandle(int64_t handle) {
        return reinterpret_cast<SpriteBatch*>(static_cast<intptr_t>(handle));
    }

    int64_t BatchToHandle(SpriteBatch* batch) {
        return static_cast<int64_t>(reinterpret_cast<intptr_t>(batch));
    }

    void FlushEntries(SpriteBatch& batch) {
        if (batch.entries.empty() || batch.windowHandle == 0) return;

        for (const BatchEntry& e : batch.entries) {
            const int64_t sprite = e.spriteHandle != 0 ? e.spriteHandle : batch.atlasHandle;
            if (sprite == 0) continue;

            if (e.srcW <= 0 || e.srcH <= 0) {
                absolute_desktop_sprite_draw(batch.windowHandle, sprite, e.destX, e.destY);
            } else {
                absolute_desktop_sprite_draw_rect(
                    batch.windowHandle, sprite,
                    e.destX, e.destY, e.srcX, e.srcY, e.srcW, e.srcH);
            }
        }

        batch.entries.clear();
    }

    void PushEntry(SpriteBatch& batch, const BatchEntry& entry) {
        if (!batch.active || batch.windowHandle == 0) return;
        if (static_cast<int32_t>(batch.entries.size()) >= kMaxBatchEntries) {
            FlushEntries(batch);
        }
        batch.entries.push_back(entry);
    }
}

extern "C" int64_t absolute_desktop_batch_create() {
    auto* batch = new SpriteBatch();
    batch->entries.reserve(256);
    return BatchToHandle(batch);
}

extern "C" void absolute_desktop_batch_destroy(int64_t handle) {
    delete BatchFromHandle(handle);
}

extern "C" void absolute_desktop_batch_begin(int64_t handle, int64_t windowHandle, int64_t atlasHandle) {
    SpriteBatch* batch = BatchFromHandle(handle);
    if (!batch) return;
    FlushEntries(*batch);
    batch->windowHandle = windowHandle;
    batch->atlasHandle = atlasHandle;
    batch->active = windowHandle != 0;
    batch->entries.clear();
}

extern "C" void absolute_desktop_batch_set_atlas(int64_t handle, int64_t atlasHandle) {
    SpriteBatch* batch = BatchFromHandle(handle);
    if (!batch || !batch->active) return;
    if (batch->atlasHandle == atlasHandle) return;
    FlushEntries(*batch);
    batch->atlasHandle = atlasHandle;
}

extern "C" void absolute_desktop_batch_draw(int64_t handle, int32_t x, int32_t y) {
    SpriteBatch* batch = BatchFromHandle(handle);
    if (!batch || !batch->active || batch->atlasHandle == 0) return;
    const int32_t w = absolute_desktop_sprite_width(batch->atlasHandle);
    const int32_t h = absolute_desktop_sprite_height(batch->atlasHandle);
    if (w <= 0 || h <= 0) return;
    BatchEntry e{};
    e.spriteHandle = 0;
    e.destX = x;
    e.destY = y;
    e.srcX = 0;
    e.srcY = 0;
    e.srcW = w;
    e.srcH = h;
    PushEntry(*batch, e);
}

extern "C" void absolute_desktop_batch_draw_rect(
    int64_t handle,
    int32_t destX, int32_t destY,
    int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH) {
    SpriteBatch* batch = BatchFromHandle(handle);
    if (!batch || !batch->active || batch->atlasHandle == 0) return;
    if (srcW <= 0 || srcH <= 0) return;
    BatchEntry e{};
    e.spriteHandle = 0;
    e.destX = destX;
    e.destY = destY;
    e.srcX = srcX;
    e.srcY = srcY;
    e.srcW = srcW;
    e.srcH = srcH;
    PushEntry(*batch, e);
}

extern "C" void absolute_desktop_batch_draw_sprite(
    int64_t handle, int64_t spriteHandle, int32_t x, int32_t y) {
    SpriteBatch* batch = BatchFromHandle(handle);
    if (!batch || !batch->active || spriteHandle == 0) return;
    BatchEntry e{};
    e.spriteHandle = spriteHandle;
    e.destX = x;
    e.destY = y;
    e.srcX = 0;
    e.srcY = 0;
    e.srcW = 0;
    e.srcH = 0;
    PushEntry(*batch, e);
}

extern "C" void absolute_desktop_batch_draw_sprite_rect(
    int64_t handle, int64_t spriteHandle,
    int32_t destX, int32_t destY,
    int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH) {
    SpriteBatch* batch = BatchFromHandle(handle);
    if (!batch || !batch->active || spriteHandle == 0) return;
    if (srcW <= 0 || srcH <= 0) return;
    BatchEntry e{};
    e.spriteHandle = spriteHandle;
    e.destX = destX;
    e.destY = destY;
    e.srcX = srcX;
    e.srcY = srcY;
    e.srcW = srcW;
    e.srcH = srcH;
    PushEntry(*batch, e);
}

extern "C" void absolute_desktop_batch_flush(int64_t handle) {
    SpriteBatch* batch = BatchFromHandle(handle);
    if (!batch) return;
    FlushEntries(*batch);
}

extern "C" void absolute_desktop_batch_end(int64_t handle) {
    SpriteBatch* batch = BatchFromHandle(handle);
    if (!batch) return;
    FlushEntries(*batch);
    batch->windowHandle = 0;
    batch->atlasHandle = 0;
    batch->active = false;
}

extern "C" int32_t absolute_desktop_batch_count(int64_t handle) {
    const SpriteBatch* batch = BatchFromHandle(handle);
    return batch ? static_cast<int32_t>(batch->entries.size()) : 0;
}
