#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
    thread_local std::string lastStringError;
    thread_local std::string lastStringResult;

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
        lastStringResult.clear();
        return lastStringResult.c_str();
    }
    lastStringResult = EncodeUtf8(static_cast<uint32_t>(codePoint));
    return lastStringResult.c_str();
}

extern "C" const char* absolute_string_to_upper(const char* text) {
    lastStringResult.clear();
    if (!text) return lastStringResult.c_str();
    const char* p = text;
    while (*p) {
        uint32_t cp = DecodeUtf8(p);
        lastStringResult += EncodeUtf8(ToUpperCodePoint(cp));
    }
    return lastStringResult.c_str();
}

extern "C" const char* absolute_string_to_lower(const char* text) {
    lastStringResult.clear();
    if (!text) return lastStringResult.c_str();
    const char* p = text;
    while (*p) {
        uint32_t cp = DecodeUtf8(p);
        lastStringResult += EncodeUtf8(ToLowerCodePoint(cp));
    }
    return lastStringResult.c_str();
}

extern "C" const char* absolute_string_trim(const char* text) {
    lastStringResult.clear();
    if (!text || !*text) return lastStringResult.c_str();
    std::string str(text);
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return lastStringResult.c_str();
    }
    size_t end = str.find_last_not_of(" \t\r\n");
    lastStringResult = str.substr(start, end - start + 1);
    return lastStringResult.c_str();
}

extern "C" const char* absolute_string_trim_start(const char* text) {
    lastStringResult.clear();
    if (!text || !*text) return lastStringResult.c_str();
    std::string str(text);
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return lastStringResult.c_str();
    }
    lastStringResult = str.substr(start);
    return lastStringResult.c_str();
}

extern "C" const char* absolute_string_trim_end(const char* text) {
    lastStringResult.clear();
    if (!text || !*text) return lastStringResult.c_str();
    std::string str(text);
    size_t end = str.find_last_not_of(" \t\r\n");
    if (end == std::string::npos) {
        return lastStringResult.c_str();
    }
    lastStringResult = str.substr(0, end + 1);
    return lastStringResult.c_str();
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
    return static_cast<int32_t>(pos - text);
}

extern "C" int32_t absolute_string_last_index_of(const char* text, const char* sub) {
    if (!text || !sub || !*sub) return -1;
    std::string s(text);
    std::string needle(sub);
    size_t pos = s.rfind(needle);
    if (pos == std::string::npos) return -1;
    return static_cast<int32_t>(pos);
}

extern "C" const char* absolute_string_replace(const char* text, const char* fromText, const char* toText) {
    lastStringResult.clear();
    if (!text) return lastStringResult.c_str();
    if (!fromText || !*fromText) {
        lastStringResult = text;
        return lastStringResult.c_str();
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
    return lastStringResult.c_str();
}

extern "C" const char* absolute_string_substring(const char* text, int32_t start, int32_t length) {
    lastStringResult.clear();
    if (!text || start < 0 || length <= 0) return lastStringResult.c_str();
    size_t textLen = std::strlen(text);
    if (static_cast<size_t>(start) >= textLen) return lastStringResult.c_str();
    size_t count = static_cast<size_t>(length);
    if (static_cast<size_t>(start) + count > textLen) {
        count = textLen - static_cast<size_t>(start);
    }
    lastStringResult = std::string(text + start, count);
    return lastStringResult.c_str();
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
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%g", value);
        static_cast<std::string*>(handle)->append(buf);
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
    return lastStringResult.c_str();
}

extern "C" const char* absolute_string_concat(const char* a, const char* b) {
    lastStringResult.clear();
    if (a) lastStringResult += a;
    if (b) lastStringResult += b;
    return lastStringResult.c_str();
}

extern "C" int32_t absolute_string_parse_int(const char* text) {
    if (!text || !*text) return 0;
    try {
        return std::stoi(text);
    } catch (...) {
        return 0;
    }
}
