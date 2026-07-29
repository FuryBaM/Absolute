#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <algorithm>

struct AbsoluteByteBufferImpl {
    uint8_t* data = nullptr;
    int64_t capacity = 0;
    bool isOwner = true;
};

extern "C" {

void* absolute_byte_buffer_create(int64_t capacity) {
    if (capacity <= 0) capacity = 1024;
    auto* buf = new AbsoluteByteBufferImpl();
    buf->data = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(capacity)));
    if (buf->data) {
        std::memset(buf->data, 0, static_cast<size_t>(capacity));
    }
    buf->capacity = capacity;
    buf->isOwner = true;
    return buf;
}

void* absolute_byte_buffer_wrap(const void* data, int64_t size) {
    auto* buf = new AbsoluteByteBufferImpl();
    buf->data = const_cast<uint8_t*>(static_cast<const uint8_t*>(data));
    buf->capacity = size > 0 ? size : 0;
    buf->isOwner = false;
    return buf;
}

void absolute_byte_buffer_free(void* handle) {
    if (!handle) return;
    auto* buf = static_cast<AbsoluteByteBufferImpl*>(handle);
    if (buf->isOwner && buf->data) {
        std::free(buf->data);
    }
    delete buf;
}

void* absolute_byte_buffer_slice(void* handle, int64_t offset, int64_t length) {
    if (!handle) return nullptr;
    auto* buf = static_cast<AbsoluteByteBufferImpl*>(handle);
    if (offset < 0) offset = 0;
    if (offset > buf->capacity) offset = buf->capacity;
    if (length < 0) length = 0;
    if (offset + length > buf->capacity) length = buf->capacity - offset;

    auto* sliceBuf = new AbsoluteByteBufferImpl();
    sliceBuf->data = buf->data + offset;
    sliceBuf->capacity = length;
    sliceBuf->isOwner = false;
    return sliceBuf;
}

uint8_t* absolute_byte_buffer_get_data(void* handle) {
    if (!handle) return nullptr;
    auto* buf = static_cast<AbsoluteByteBufferImpl*>(handle);
    return buf->data;
}

int64_t absolute_byte_buffer_capacity(void* handle) {
    if (!handle) return 0;
    auto* buf = static_cast<AbsoluteByteBufferImpl*>(handle);
    return buf->capacity;
}

uint8_t absolute_byte_buffer_get_byte(void* handle, int64_t offset) {
    if (!handle) return 0;
    auto* buf = static_cast<AbsoluteByteBufferImpl*>(handle);
    if (offset < 0 || offset >= buf->capacity) return 0;
    return buf->data[offset];
}

void absolute_byte_buffer_set_byte(void* handle, int64_t offset, uint8_t val) {
    if (!handle) return;
    auto* buf = static_cast<AbsoluteByteBufferImpl*>(handle);
    if (offset < 0 || offset >= buf->capacity) return;
    buf->data[offset] = val;
}

void absolute_byte_buffer_compact(void* handle, int64_t position, int64_t remaining) {
    if (!handle) return;
    auto* buf = static_cast<AbsoluteByteBufferImpl*>(handle);
    if (position <= 0 || remaining <= 0) return;
    if (position + remaining > buf->capacity) remaining = buf->capacity - position;
    std::memmove(buf->data, buf->data + position, static_cast<size_t>(remaining));
}

} // extern "C"
