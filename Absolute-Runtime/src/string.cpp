#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// Allocating `bytes` of text plus its terminator, behind a header that says
// how many names are holding it. The pointer handed back is the first byte of
// the text, so every consumer -- including `printf` and the C ABI -- sees the
// same bare `const char*` it always saw.
// Declared, not included: real_text.h defines its functions, and it belongs
// to exactly one translation unit per build (real_text.cpp here).
#define ABSOLUTE_REAL_TEXT_CAPACITY 32
extern "C" std::int32_t absolute_double_text(double value, char* out, std::int32_t capacity);

extern "C" char* absolute_string_alloc(std::size_t bytes);
extern "C" const char* absolute_string_retain(const char* text);
extern "C" void absolute_string_release(const char* text);

namespace {
    thread_local std::string lastStringError;
    thread_local std::string lastStringResult;

    // Absolute `string` is a bare `const char*` and stays one: it crosses the C
    // boundary and `printf` takes it. What changes is what sits behind it.
    // Every string the language allocates carries a header immediately before
    // its first byte, so the pointer alone is enough to retain or release the
    // storage -- which is what a value with no lifetime was missing.
    //
    // Shared rather than unique, because a string is a value: assigning one
    // copies the pointer, and two names for the same bytes is the ordinary
    // case rather than an error. The count says how many of them there are.
    //
    // The magic word is not decoration. A `const char*` can also arrive from
    // outside -- a plugin, a C function, a literal in a library built before
    // this -- and releasing one of those would free storage the language never
    // allocated. A header that does not say so is not ours and nothing
    // happens.
    constexpr std::uint32_t StringMagic = 0x41425331u;   // "ABS1"
    constexpr std::uint32_t StringStatic = 0xFFFFFFFFu;  // never released

    struct StringHeader {
        std::uint32_t magic;
        std::atomic<std::uint32_t> refs;
    };

    StringHeader* HeaderOf(const char* text) {
        if (!text) return nullptr;
        auto* header = reinterpret_cast<StringHeader*>(
            const_cast<char*>(text) - sizeof(StringHeader));
        return header->magic == StringMagic ? header : nullptr;
    }

    // Runtime helpers must return durable storage: a single thread-local buffer
    // is invalidated by the next string-producing call (breaks uri/http parse
    // pipelines that keep fields).
    const char* DurableCopy(const std::string& value) {
        char* copy = absolute_string_alloc(value.size());
        if (!copy) {
            lastStringError = "string allocation failed";
            return "";
        }
        std::memcpy(copy, value.c_str(), value.size());
        return copy;
    }

    const char* DurableCString(const char* value) {
        if (!value) value = "";
        const std::size_t length = std::strlen(value);
        char* copy = absolute_string_alloc(length);
        if (!copy) {
            lastStringError = "string allocation failed";
            return "";
        }
        std::memcpy(copy, value, length);
        return copy;
    }

    // Helper: decode single UTF-8 code point starting at ptr, advances ptr
    uint32_t DecodeUtf8(const char*& ptr) {
        if (!ptr || !*ptr) return 0;
        const uint8_t c = static_cast<uint8_t>(*ptr++);
        if (c < 0x80) return c;
        if ((c & 0xE0) == 0xC0) {
            uint32_t cp = (c & 0x1F) << 6;
            if (*ptr && (static_cast<uint8_t>(*ptr) & 0xC0) == 0x80) {
                cp |= (static_cast<uint8_t>(*ptr++) & 0x3F);
            }
            return cp;
        }
        if ((c & 0xF0) == 0xE0) {
            uint32_t cp = (c & 0x0F) << 12;
            if (*ptr && (static_cast<uint8_t>(*ptr) & 0xC0) == 0x80) {
                cp |= (static_cast<uint8_t>(*ptr++) & 0x3F) << 6;
                if (*ptr && (static_cast<uint8_t>(*ptr) & 0xC0) == 0x80) {
                    cp |= (static_cast<uint8_t>(*ptr++) & 0x3F);
                }
            }
            return cp;
        }
        if ((c & 0xF8) == 0xF0) {
            uint32_t cp = (c & 0x07) << 18;
            if (*ptr && (static_cast<uint8_t>(*ptr) & 0xC0) == 0x80) {
                cp |= (static_cast<uint8_t>(*ptr++) & 0x3F) << 12;
                if (*ptr && (static_cast<uint8_t>(*ptr) & 0xC0) == 0x80) {
                    cp |= (static_cast<uint8_t>(*ptr++) & 0x3F) << 6;
                    if (*ptr && (static_cast<uint8_t>(*ptr) & 0xC0) == 0x80) {
                        cp |= (static_cast<uint8_t>(*ptr++) & 0x3F);
                    }
                }
            }
            return cp;
        }
        return c;
    }

    std::size_t ByteOffsetForCodePoint(const char* text, int32_t index) {
        if (!text || index <= 0) return 0;
        const char* cursor = text;
        int32_t current = 0;
        while (*cursor && current < index) {
            DecodeUtf8(cursor);
            ++current;
        }
        return static_cast<std::size_t>(cursor - text);
    }

    int32_t CodePointIndexForByteOffset(const char* text, std::size_t byteOffset) {
        if (!text || byteOffset == 0) return 0;
        const char* cursor = text;
        const char* end = text + byteOffset;
        int32_t index = 0;
        while (*cursor && cursor < end) {
            DecodeUtf8(cursor);
            ++index;
        }
        return index;
    }

    // Helper: encode single code point to UTF-8 std::string
    std::string EncodeUtf8(uint32_t cp) {
        std::string res;
        if (cp <= 0x7F) {
            res.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            res.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
            res.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            res.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
            res.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            res.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0x10FFFF) {
            res.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
            res.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            res.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            res.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        return res;
    }

    // Simple Unicode case mapping for ASCII and Cyrillic/Latin-1
    uint32_t ToUpperCodePoint(uint32_t cp) {
        if (cp >= 'a' && cp <= 'z') return cp - ('a' - 'A');
        // Cyrillic small letters (а-я -> А-Я)
        if (cp >= 0x0430 && cp <= 0x044F) return cp - 32;
        // Cyrillic ё -> Ё
        if (cp == 0x0451) return 0x0401;
        return cp;
    }

    uint32_t ToLowerCodePoint(uint32_t cp) {
        if (cp >= 'A' && cp <= 'Z') return cp + ('a' - 'A');
        // Cyrillic capital letters (А-Я -> а-я)
        if (cp >= 0x0410 && cp <= 0x042F) return cp + 32;
        // Cyrillic Ё -> ё
        if (cp == 0x0401) return 0x0451;
        return cp;
    }
}

extern "C" const char* absolute_string_error() {
    return lastStringError.c_str();
}

extern "C" int32_t absolute_string_code_point_count(const char* text) {
    if (!text) return 0;
    int32_t count = 0;
    const char* p = text;
    while (*p) {
        DecodeUtf8(p);
        count++;
    }
    return count;
}

extern "C" int32_t absolute_string_utf8_byte_count(const char* text) {
    return text ? static_cast<int32_t>(std::strlen(text)) : 0;
}

extern "C" int32_t absolute_string_utf8_copy(
    const char* text, std::uint8_t* output, std::int32_t capacity) {
    const std::size_t length = text ? std::strlen(text) : 0;
    if (capacity < 0 || length > static_cast<std::size_t>(capacity) ||
        (!output && length != 0)) {
        return -1;
    }
    if (length != 0) std::memcpy(output, text, length);
    return static_cast<std::int32_t>(length);
}

extern "C" int32_t absolute_string_code_point_at(const char* text, int32_t index) {
    if (!text || index < 0) return -1;
    int32_t cur = 0;
    const char* p = text;
    while (*p) {
        uint32_t cp = DecodeUtf8(p);
        if (cur == index) return static_cast<int32_t>(cp);
        cur++;
    }
    return -1;
}

extern "C" const char* absolute_string_from_code_point(int32_t codePoint) {
    if (codePoint < 0) {
        return DurableCopy(std::string());
    }
    return DurableCopy(EncodeUtf8(static_cast<uint32_t>(codePoint)));
}

extern "C" const char* absolute_string_to_upper(const char* text) {
    lastStringResult.clear();
    if (!text) return DurableCopy(lastStringResult);
    const char* p = text;
    while (*p) {
        uint32_t cp = DecodeUtf8(p);
        lastStringResult += EncodeUtf8(ToUpperCodePoint(cp));
    }
    return DurableCopy(lastStringResult);
}

extern "C" const char* absolute_string_to_lower(const char* text) {
    lastStringResult.clear();
    if (!text) return DurableCopy(lastStringResult);
    const char* p = text;
    while (*p) {
        uint32_t cp = DecodeUtf8(p);
        lastStringResult += EncodeUtf8(ToLowerCodePoint(cp));
    }
    return DurableCopy(lastStringResult);
}

extern "C" const char* absolute_string_trim(const char* text) {
    lastStringResult.clear();
    if (!text || !*text) return DurableCopy(lastStringResult);
    std::string str(text);
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return DurableCopy(lastStringResult);
    }
    size_t end = str.find_last_not_of(" \t\r\n");
    lastStringResult = str.substr(start, end - start + 1);
    return DurableCopy(lastStringResult);
}

extern "C" const char* absolute_string_trim_start(const char* text) {
    lastStringResult.clear();
    if (!text || !*text) return DurableCopy(lastStringResult);
    std::string str(text);
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return DurableCopy(lastStringResult);
    }
    lastStringResult = str.substr(start);
    return DurableCopy(lastStringResult);
}

extern "C" const char* absolute_string_trim_end(const char* text) {
    lastStringResult.clear();
    if (!text || !*text) return DurableCopy(lastStringResult);
    std::string str(text);
    size_t end = str.find_last_not_of(" \t\r\n");
    if (end == std::string::npos) {
        return DurableCopy(lastStringResult);
    }
    lastStringResult = str.substr(0, end + 1);
    return DurableCopy(lastStringResult);
}

extern "C" int32_t absolute_string_starts_with(const char* text, const char* prefix) {
    if (!text || !prefix) return 0;
    size_t textLen = std::strlen(text);
    size_t prefLen = std::strlen(prefix);
    if (prefLen > textLen) return 0;
    return std::strncmp(text, prefix, prefLen) == 0 ? 1 : 0;
}

extern "C" int32_t absolute_string_ends_with(const char* text, const char* suffix) {
    if (!text || !suffix) return 0;
    size_t textLen = std::strlen(text);
    size_t suffLen = std::strlen(suffix);
    if (suffLen > textLen) return 0;
    return std::strcmp(text + (textLen - suffLen), suffix) == 0 ? 1 : 0;
}

extern "C" int32_t absolute_string_contains(const char* text, const char* sub) {
    if (!text || !sub) return 0;
    return std::strstr(text, sub) != nullptr ? 1 : 0;
}

extern "C" int32_t absolute_string_index_of(const char* text, const char* sub) {
    if (!text || !sub) return -1;
    const char* pos = std::strstr(text, sub);
    if (!pos) return -1;
    return CodePointIndexForByteOffset(text, static_cast<std::size_t>(pos - text));
}

extern "C" int32_t absolute_string_last_index_of(const char* text, const char* sub) {
    if (!text || !sub) return -1;
    // The empty string occurs at every index, including the end. indexOf("")
    // already answered 0; lastIndexOf("") answered -1, as if it were absent.
    if (!*sub) return absolute_string_code_point_count(text);
    std::string s(text);
    std::string needle(sub);
    size_t pos = s.rfind(needle);
    if (pos == std::string::npos) return -1;
    return CodePointIndexForByteOffset(text, pos);
}

extern "C" const char* absolute_string_replace(const char* text, const char* fromText, const char* toText) {
    lastStringResult.clear();
    if (!text) return DurableCopy(lastStringResult);
    if (!fromText || !*fromText) {
        return DurableCString(text);
    }
    const char* toVal = toText ? toText : "";
    std::string str(text);
    std::string fromStr(fromText);
    std::string toStr(toVal);
    size_t pos = 0;
    while ((pos = str.find(fromStr, pos)) != std::string::npos) {
        str.replace(pos, fromStr.length(), toStr);
        pos += toStr.length();
    }
    lastStringResult = str;
    return DurableCopy(lastStringResult);
}

extern "C" const char* absolute_string_substring(const char* text, int32_t start, int32_t length) {
    lastStringResult.clear();
    if (!text || start < 0 || length <= 0) return DurableCopy(lastStringResult);
    const int32_t codePoints = absolute_string_code_point_count(text);
    if (start >= codePoints) return DurableCopy(lastStringResult);
    const int32_t remaining = codePoints - start;
    const int32_t endIndex = length > remaining ? codePoints : start + length;
    const std::size_t startByte = ByteOffsetForCodePoint(text, start);
    const std::size_t endByte = ByteOffsetForCodePoint(text, endIndex);
    lastStringResult = std::string(text + startByte, endByte - startByte);
    return DurableCopy(lastStringResult);
}

extern "C" void* absolute_string_builder_create() {
    return new std::string();
}

extern "C" void absolute_string_builder_free(void* handle) {
    if (handle) {
        delete static_cast<std::string*>(handle);
    }
}

extern "C" void absolute_string_builder_append(void* handle, const char* text) {
    if (handle && text) {
        static_cast<std::string*>(handle)->append(text);
    }
}

extern "C" void absolute_string_builder_append_char(void* handle, int32_t codePoint) {
    if (handle && codePoint >= 0) {
        static_cast<std::string*>(handle)->append(EncodeUtf8(static_cast<uint32_t>(codePoint)));
    }
}

extern "C" void absolute_string_builder_append_int(void* handle, int64_t value) {
    if (handle) {
        static_cast<std::string*>(handle)->append(std::to_string(value));
    }
}

extern "C" void absolute_string_builder_append_double(void* handle, double value) {
    if (handle) {
        // The shared routine, not "%g": six significant digits is not the
        // value, and the wasm builder writes the same text this one does.
        char text[ABSOLUTE_REAL_TEXT_CAPACITY];
        absolute_double_text(value, text, static_cast<std::int32_t>(sizeof(text)));
        static_cast<std::string*>(handle)->append(text);
    }
}

extern "C" void absolute_string_builder_append_bool(void* handle, int32_t value) {
    if (handle) {
        static_cast<std::string*>(handle)->append(value ? "true" : "false");
    }
}

extern "C" void absolute_string_builder_clear(void* handle) {
    if (handle) {
        static_cast<std::string*>(handle)->clear();
    }
}

extern "C" int32_t absolute_string_builder_length(void* handle) {
    if (!handle) return 0;
    return absolute_string_code_point_count(static_cast<std::string*>(handle)->c_str());
}

extern "C" const char* absolute_string_builder_to_string(void* handle) {
    lastStringResult.clear();
    if (handle) {
        lastStringResult = *static_cast<std::string*>(handle);
    }
    return DurableCopy(lastStringResult);
}

extern "C" const char* absolute_string_concat(const char* a, const char* b) {
    lastStringResult.clear();
    if (a) lastStringResult += a;
    if (b) lastStringResult += b;
    return DurableCopy(lastStringResult);
}

extern "C" int32_t absolute_string_parse_int(const char* text) {
    if (!text || !*text) return 0;
    try {
        return std::stoi(text);
    } catch (...) {
        return 0;
    }
}

extern "C" char* absolute_string_alloc(std::size_t bytes) {
    void* block = std::malloc(sizeof(StringHeader) + bytes + 1);
    if (!block) return nullptr;
    auto* header = static_cast<StringHeader*>(block);
    header->magic = StringMagic;
    header->refs.store(1, std::memory_order_relaxed);
    char* text = reinterpret_cast<char*>(header + 1);
    text[bytes] = '\0';
    return text;
}

extern "C" const char* absolute_string_retain(const char* text) {
    if (StringHeader* header = HeaderOf(text)) {
        // A static string is written once and never released; incrementing its
        // count would eventually wrap it into an ordinary one.
        if (header->refs.load(std::memory_order_relaxed) != StringStatic)
            header->refs.fetch_add(1, std::memory_order_relaxed);
    }
    return text;
}

extern "C" void absolute_string_release(const char* text) {
    StringHeader* header = HeaderOf(text);
    if (!header) return;
    if (header->refs.load(std::memory_order_relaxed) == StringStatic) return;
    // Release-acquire around the last decrement: whatever the other holders
    // wrote to the text has to be visible to whoever frees it.
    if (header->refs.fetch_sub(1, std::memory_order_release) != 1) return;
    std::atomic_thread_fence(std::memory_order_acquire);
    header->magic = 0;
    std::free(header);
}

// A string that the language did not allocate -- a literal, a plugin's buffer,
// anything static -- can still be handed to code that retains and releases,
// as long as it says it is not to be freed. Copying it is the alternative and
// it is not always available: the pointer may be the only thing there is.
extern "C" const char* absolute_string_adopt_static(const char* text) {
    if (StringHeader* header = HeaderOf(text))
        header->refs.store(StringStatic, std::memory_order_relaxed);
    return text;
}
