#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <sstream>
#include <iomanip>

struct BinaryWriterImpl {
    std::vector<uint8_t> buffer;
};

struct BinaryReaderImpl {
    std::vector<uint8_t> buffer;
    size_t pos = 0;
};

namespace {
    thread_local std::string lastBinaryResult;
}

extern "C" {
    void* absolute_binary_writer_create() {
        return new BinaryWriterImpl();
    }

    void absolute_binary_writer_free(void* handle) {
        if (handle) {
            delete static_cast<BinaryWriterImpl*>(handle);
        }
    }

    void absolute_binary_writer_write_byte(void* handle, uint8_t val) {
        if (!handle) return;
        static_cast<BinaryWriterImpl*>(handle)->buffer.push_back(val);
    }

    void absolute_binary_writer_write_i16(void* handle, int16_t val) {
        if (!handle) return;
        auto writer = static_cast<BinaryWriterImpl*>(handle);
        uint8_t bytes[2];
        std::memcpy(bytes, &val, sizeof(val));
        writer->buffer.insert(writer->buffer.end(), bytes, bytes + 2);
    }

    void absolute_binary_writer_write_i32(void* handle, int32_t val) {
        if (!handle) return;
        auto writer = static_cast<BinaryWriterImpl*>(handle);
        uint8_t bytes[4];
        std::memcpy(bytes, &val, sizeof(val));
        writer->buffer.insert(writer->buffer.end(), bytes, bytes + 4);
    }

    void absolute_binary_writer_write_i64(void* handle, int64_t val) {
        if (!handle) return;
        auto writer = static_cast<BinaryWriterImpl*>(handle);
        uint8_t bytes[8];
        std::memcpy(bytes, &val, sizeof(val));
        writer->buffer.insert(writer->buffer.end(), bytes, bytes + 8);
    }

    void absolute_binary_writer_write_float(void* handle, float val) {
        if (!handle) return;
        auto writer = static_cast<BinaryWriterImpl*>(handle);
        uint8_t bytes[4];
        std::memcpy(bytes, &val, sizeof(val));
        writer->buffer.insert(writer->buffer.end(), bytes, bytes + 4);
    }

    void absolute_binary_writer_write_double(void* handle, double val) {
        if (!handle) return;
        auto writer = static_cast<BinaryWriterImpl*>(handle);
        uint8_t bytes[8];
        std::memcpy(bytes, &val, sizeof(val));
        writer->buffer.insert(writer->buffer.end(), bytes, bytes + 8);
    }

    void absolute_binary_writer_write_string(void* handle, const char* text) {
        if (!handle) return;
        std::string s = text ? text : "";
        int32_t len = static_cast<int32_t>(s.size());
        absolute_binary_writer_write_i32(handle, len);
        auto writer = static_cast<BinaryWriterImpl*>(handle);
        writer->buffer.insert(writer->buffer.end(), s.begin(), s.end());
    }

    void absolute_binary_writer_write_bytes(void* handle, const void* data, int64_t size) {
        if (!handle || !data || size <= 0) return;
        auto writer = static_cast<BinaryWriterImpl*>(handle);
        const uint8_t* ptr = static_cast<const uint8_t*>(data);
        writer->buffer.insert(writer->buffer.end(), ptr, ptr + size);
    }

    int64_t absolute_binary_writer_size(const void* handle) {
        if (!handle) return 0;
        return static_cast<int64_t>(static_cast<const BinaryWriterImpl*>(handle)->buffer.size());
    }

    const void* absolute_binary_writer_get_data(const void* handle) {
        if (!handle) return nullptr;
        return static_cast<const BinaryWriterImpl*>(handle)->buffer.data();
    }

    const char* absolute_binary_writer_to_hex(const void* handle) {
        if (!handle) return "";
        auto writer = static_cast<const BinaryWriterImpl*>(handle);
        std::ostringstream ss;
        for (uint8_t b : writer->buffer) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
        }
        lastBinaryResult = ss.str();
        return lastBinaryResult.c_str();
    }

    void* absolute_binary_reader_create(const void* data, int64_t size) {
        auto reader = new BinaryReaderImpl();
        if (data && size > 0) {
            const uint8_t* ptr = static_cast<const uint8_t*>(data);
            reader->buffer.assign(ptr, ptr + size);
        }
        return reader;
    }

    void absolute_binary_reader_free(void* handle) {
        if (handle) {
            delete static_cast<BinaryReaderImpl*>(handle);
        }
    }

    uint8_t absolute_binary_reader_read_byte(void* handle) {
        if (!handle) return 0;
        auto reader = static_cast<BinaryReaderImpl*>(handle);
        if (reader->pos >= reader->buffer.size()) return 0;
        return reader->buffer[reader->pos++];
    }

    int16_t absolute_binary_reader_read_i16(void* handle) {
        if (!handle) return 0;
        auto reader = static_cast<BinaryReaderImpl*>(handle);
        if (reader->pos + 2 > reader->buffer.size()) return 0;
        int16_t val;
        std::memcpy(&val, &reader->buffer[reader->pos], 2);
        reader->pos += 2;
        return val;
    }

    int32_t absolute_binary_reader_read_i32(void* handle) {
        if (!handle) return 0;
        auto reader = static_cast<BinaryReaderImpl*>(handle);
        if (reader->pos + 4 > reader->buffer.size()) return 0;
        int32_t val;
        std::memcpy(&val, &reader->buffer[reader->pos], 4);
        reader->pos += 4;
        return val;
    }

    int64_t absolute_binary_reader_read_i64(void* handle) {
        if (!handle) return 0;
        auto reader = static_cast<BinaryReaderImpl*>(handle);
        if (reader->pos + 8 > reader->buffer.size()) return 0;
        int64_t val;
        std::memcpy(&val, &reader->buffer[reader->pos], 8);
        reader->pos += 8;
        return val;
    }

    float absolute_binary_reader_read_float(void* handle) {
        if (!handle) return 0.0f;
        auto reader = static_cast<BinaryReaderImpl*>(handle);
        if (reader->pos + 4 > reader->buffer.size()) return 0.0f;
        float val;
        std::memcpy(&val, &reader->buffer[reader->pos], 4);
        reader->pos += 4;
        return val;
    }

    double absolute_binary_reader_read_double(void* handle) {
        if (!handle) return 0.0;
        auto reader = static_cast<BinaryReaderImpl*>(handle);
        if (reader->pos + 8 > reader->buffer.size()) return 0.0;
        double val;
        std::memcpy(&val, &reader->buffer[reader->pos], 8);
        reader->pos += 8;
        return val;
    }

    const char* absolute_binary_reader_read_string(void* handle) {
        if (!handle) return "";
        auto reader = static_cast<BinaryReaderImpl*>(handle);
        int32_t len = absolute_binary_reader_read_i32(handle);
        if (len <= 0 || reader->pos + len > reader->buffer.size()) {
            return "";
        }
        std::string s(reinterpret_cast<const char*>(&reader->buffer[reader->pos]), len);
        reader->pos += len;
        lastBinaryResult = s;
        return lastBinaryResult.c_str();
    }

    int64_t absolute_binary_reader_read_bytes(void* handle, void* dest, int64_t size) {
        if (!handle || !dest || size <= 0) return 0;
        auto reader = static_cast<BinaryReaderImpl*>(handle);
        int64_t avail = static_cast<int64_t>(reader->buffer.size() - reader->pos);
        if (avail <= 0) return 0;
        int64_t toRead = size < avail ? size : avail;
        std::memcpy(dest, &reader->buffer[reader->pos], toRead);
        reader->pos += toRead;
        return toRead;
    }

    int64_t absolute_binary_reader_position(const void* handle) {
        if (!handle) return 0;
        return static_cast<int64_t>(static_cast<const BinaryReaderImpl*>(handle)->pos);
    }

    int64_t absolute_binary_reader_remaining(const void* handle) {
        if (!handle) return 0;
        auto reader = static_cast<const BinaryReaderImpl*>(handle);
        if (reader->pos >= reader->buffer.size()) return 0;
        return static_cast<int64_t>(reader->buffer.size() - reader->pos);
    }

    int32_t absolute_binary_reader_is_eof(const void* handle) {
        if (!handle) return 1;
        auto reader = static_cast<const BinaryReaderImpl*>(handle);
        return reader->pos >= reader->buffer.size() ? 1 : 0;
    }

    void absolute_binary_reader_seek(void* handle, int64_t pos) {
        if (!handle || pos < 0) return;
        auto reader = static_cast<BinaryReaderImpl*>(handle);
        size_t p = static_cast<size_t>(pos);
        if (p > reader->buffer.size()) p = reader->buffer.size();
        reader->pos = p;
    }
}
