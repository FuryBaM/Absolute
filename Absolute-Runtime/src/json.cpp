#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <cstdint>
#include <cctype>
#include <cerrno>
#include <cstdlib>

enum JsonNodeType {
    JSON_NULL = 0,
    JSON_BOOL = 1,
    JSON_NUMBER = 2,
    JSON_STRING = 3,
    JSON_ARRAY = 4,
    JSON_OBJECT = 5
};

struct AbsoluteJsonNode {
    JsonNodeType type;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    std::vector<AbsoluteJsonNode*> arrayElements;
    std::map<std::string, AbsoluteJsonNode*> objectProperties;

    ~AbsoluteJsonNode() {
        for (auto child : arrayElements) {
            delete child;
        }
        for (auto& pair : objectProperties) {
            delete pair.second;
        }
    }
};

namespace {
    thread_local std::string lastJsonError;
    thread_local std::string lastJsonResult;

    class JsonParser {
        std::string src;
        size_t pos = 0;
        std::string error;

    public:
        JsonParser(const std::string& input) : src(input) {}

        void skipWhitespace() {
            while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\r' || src[pos] == '\n')) {
                pos++;
            }
        }

        AbsoluteJsonNode* parseValue() {
            skipWhitespace();
            if (pos >= src.size()) {
                error = "Unexpected end of JSON input";
                return nullptr;
            }

            char c = src[pos];
            if (c == 'n') return parseNull();
            if (c == 't' || c == 'f') return parseBool();
            if (c == '"') return parseString();
            if (c == '[') return parseArray();
            if (c == '{') return parseObject();
            if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber();

            error = std::string("Unexpected character '") + c + "' in JSON";
            return nullptr;
        }

        AbsoluteJsonNode* parseNull() {
            if (src.compare(pos, 4, "null") == 0) {
                pos += 4;
                auto node = new AbsoluteJsonNode();
                node->type = JSON_NULL;
                return node;
            }
            error = "Expected 'null'";
            return nullptr;
        }

        AbsoluteJsonNode* parseBool() {
            if (src.compare(pos, 4, "true") == 0) {
                pos += 4;
                auto node = new AbsoluteJsonNode();
                node->type = JSON_BOOL;
                node->boolValue = true;
                return node;
            }
            if (src.compare(pos, 5, "false") == 0) {
                pos += 5;
                auto node = new AbsoluteJsonNode();
                node->type = JSON_BOOL;
                node->boolValue = false;
                return node;
            }
            error = "Expected boolean 'true' or 'false'";
            return nullptr;
        }

        AbsoluteJsonNode* parseNumber() {
            size_t start = pos;
            if (src[pos] == '-') {
                pos++;
                if (pos >= src.size() ||
                    !std::isdigit(static_cast<unsigned char>(src[pos]))) {
                    error = "Invalid number";
                    return nullptr;
                }
            }
            if (pos >= src.size() ||
                !std::isdigit(static_cast<unsigned char>(src[pos]))) {
                error = "Invalid number";
                return nullptr;
            }
            // RFC 8259: a leading zero is only legal as the number zero itself.
            // The wasm parser already refused "01"; this one handed it to
            // strtod and kept 1.
            if (src[pos] == '0') {
                pos++;
                if (pos < src.size() &&
                    std::isdigit(static_cast<unsigned char>(src[pos]))) {
                    error = "Leading zero in number";
                    return nullptr;
                }
            } else {
                while (pos < src.size() &&
                    std::isdigit(static_cast<unsigned char>(src[pos]))) {
                    pos++;
                }
            }
            if (pos < src.size() && src[pos] == '.') {
                pos++;
                if (pos >= src.size() ||
                    !std::isdigit(static_cast<unsigned char>(src[pos]))) {
                    error = "Invalid fractional number";
                    return nullptr;
                }
                while (pos < src.size() &&
                    std::isdigit(static_cast<unsigned char>(src[pos]))) {
                    pos++;
                }
            }
            if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
                pos++;
                if (pos < src.size() && (src[pos] == '+' || src[pos] == '-')) {
                    pos++;
                }
                if (pos >= src.size() ||
                    !std::isdigit(static_cast<unsigned char>(src[pos]))) {
                    error = "Invalid exponent";
                    return nullptr;
                }
                while (pos < src.size() &&
                    std::isdigit(static_cast<unsigned char>(src[pos]))) {
                    pos++;
                }
            }
            std::string numStr = src.substr(start, pos - start);
            // strtod, not std::stod: stod throws whenever the C library flags
            // the result out of range, and that includes every subnormal. A
            // document containing 1e-308 -- a legal JSON number a double holds
            // exactly -- killed the process with an uncaught std::out_of_range
            // from inside the parser. Out-of-range input is data, not a
            // programming error: an overflow keeps the infinity strtod
            // returns, an underflow keeps its zero, and neither aborts.
            errno = 0;
            const char* numBegin = numStr.c_str();
            char* numEnd = nullptr;
            double val = std::strtod(numBegin, &numEnd);
            if (numEnd == numBegin) {
                error = "Invalid number";
                return nullptr;
            }
            errno = 0;
            auto node = new AbsoluteJsonNode();
            node->type = JSON_NUMBER;
            node->numberValue = val;
            return node;
        }

        AbsoluteJsonNode* parseString() {
            if (src[pos] != '"') return nullptr;
            pos++; // skip opening quote
            std::string res;
            while (pos < src.size()) {
                char c = src[pos++];
                if (c == '"') {
                    auto node = new AbsoluteJsonNode();
                    node->type = JSON_STRING;
                    node->stringValue = res;
                    return node;
                }
                if (c == '\\') {
                    if (pos >= src.size()) {
                        error = "Unterminated escape sequence in string";
                        return nullptr;
                    }
                    char esc = src[pos++];
                    switch (esc) {
                    case '"': res += '"'; break;
                    case '\\': res += '\\'; break;
                    case '/': res += '/'; break;
                    case 'b': res += '\b'; break;
                    case 'f': res += '\f'; break;
                    case 'n': res += '\n'; break;
                    case 'r': res += '\r'; break;
                    case 't': res += '\t'; break;
                    case 'u': {
                        if (pos + 4 > src.size()) {
                            error = "Incomplete \\uXXXX escape";
                            return nullptr;
                        }
                        std::string hexStr = src.substr(pos, 4);
                        // Rejected rather than converted: std::stoul throws on
                        // "\uZZZZ", and the exception left the parser through
                        // an abort instead of through its own error channel.
                        if (hexStr.find_first_not_of("0123456789abcdefABCDEF") !=
                            std::string::npos) {
                            error = "Invalid \\uXXXX escape";
                            return nullptr;
                        }
                        pos += 4;
                        uint32_t code = static_cast<uint32_t>(
                            std::strtoul(hexStr.c_str(), nullptr, 16));
                        if (code <= 0x7F) {
                            res += static_cast<char>(code);
                        } else if (code <= 0x7FF) {
                            res += static_cast<char>(0xC0 | ((code >> 6) & 0x1F));
                            res += static_cast<char>(0x80 | (code & 0x3F));
                        } else {
                            res += static_cast<char>(0xE0 | ((code >> 12) & 0x0F));
                            res += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                            res += static_cast<char>(0x80 | (code & 0x3F));
                        }
                        break;
                    }
                    default:
                        res += esc;
                        break;
                    }
                } else {
                    res += c;
                }
            }
            error = "Unterminated string literal";
            return nullptr;
        }

        AbsoluteJsonNode* parseArray() {
            pos++; // skip [
            auto node = new AbsoluteJsonNode();
            node->type = JSON_ARRAY;
            skipWhitespace();
            if (pos < src.size() && src[pos] == ']') {
                pos++;
                return node;
            }
            while (pos < src.size()) {
                AbsoluteJsonNode* elem = parseValue();
                if (!elem) {
                    delete node;
                    return nullptr;
                }
                node->arrayElements.push_back(elem);
                skipWhitespace();
                if (pos < src.size() && src[pos] == ']') {
                    pos++;
                    return node;
                }
                if (pos < src.size() && src[pos] == ',') {
                    pos++;
                } else {
                    error = "Expected ',' or ']' in array";
                    delete node;
                    return nullptr;
                }
            }
            error = "Unterminated array";
            delete node;
            return nullptr;
        }

        AbsoluteJsonNode* parseObject() {
            pos++; // skip {
            auto node = new AbsoluteJsonNode();
            node->type = JSON_OBJECT;
            skipWhitespace();
            if (pos < src.size() && src[pos] == '}') {
                pos++;
                return node;
            }
            while (pos < src.size()) {
                skipWhitespace();
                if (src[pos] != '"') {
                    error = "Expected property key string";
                    delete node;
                    return nullptr;
                }
                AbsoluteJsonNode* keyNode = parseString();
                if (!keyNode) {
                    delete node;
                    return nullptr;
                }
                std::string key = keyNode->stringValue;
                delete keyNode;

                skipWhitespace();
                if (pos >= src.size() || src[pos] != ':') {
                    error = "Expected ':' after property key";
                    delete node;
                    return nullptr;
                }
                pos++; // skip :

                AbsoluteJsonNode* valNode = parseValue();
                if (!valNode) {
                    delete node;
                    return nullptr;
                }
                // A second "a" is a replacement, not a second name. Overwriting
                // the pointer leaked the first value; wasm already freed it.
                auto existing = node->objectProperties.find(key);
                if (existing != node->objectProperties.end()) {
                    delete existing->second;
                }
                node->objectProperties[key] = valNode;

                skipWhitespace();
                if (pos < src.size() && src[pos] == '}') {
                    pos++;
                    return node;
                }
                if (pos < src.size() && src[pos] == ',') {
                    pos++;
                } else {
                    error = "Expected ',' or '}' in object";
                    delete node;
                    return nullptr;
                }
            }
            error = "Unterminated object";
            delete node;
            return nullptr;
        }

        std::string getError() const { return error; }
    };

    void writeJsonString(std::ostringstream& ss, const std::string& text) {
        static const char hex[] = "0123456789abcdef";
        ss << '"';
        for (char raw : text) {
            const unsigned char c = static_cast<unsigned char>(raw);
            if (c == '"') ss << "\\\"";
            else if (c == '\\') ss << "\\\\";
            else if (c == '\b') ss << "\\b";
            else if (c == '\f') ss << "\\f";
            else if (c == '\n') ss << "\\n";
            else if (c == '\r') ss << "\\r";
            else if (c == '\t') ss << "\\t";
            else if (c < 0x20) ss << "\\u00" << hex[c >> 4] << hex[c & 15];
            else ss << raw;
        }
        ss << '"';
    }

    void stringifyHelper(const AbsoluteJsonNode* node, std::ostringstream& ss, bool pretty = false, int indent = 2, int currentIndent = 0) {
        if (!node) {
            ss << "null";
            return;
        }
        switch (node->type) {
        case JSON_NULL: ss << "null"; break;
        case JSON_BOOL: ss << (node->boolValue ? "true" : "false"); break;
        case JSON_NUMBER: {
            double d = node->numberValue;
            if (d == static_cast<int64_t>(d)) {
                ss << static_cast<int64_t>(d);
            } else {
                ss << d;
            }
            break;
        }
        case JSON_STRING:
            writeJsonString(ss, node->stringValue);
            break;
        case JSON_ARRAY: {
            ss << '[';
            for (size_t idx = 0; idx < node->arrayElements.size(); ++idx) {
                if (idx > 0) ss << ',';
                if (pretty) {
                    ss << '\n' << std::string(static_cast<size_t>(currentIndent + indent), ' ');
                }
                stringifyHelper(node->arrayElements[idx], ss, pretty, indent, currentIndent + indent);
            }
            if (pretty && !node->arrayElements.empty()) {
                ss << '\n' << std::string(static_cast<size_t>(currentIndent), ' ');
            }
            ss << ']';
            break;
        }
        case JSON_OBJECT: {
            ss << '{';
            size_t idx = 0;
            for (const auto& pair : node->objectProperties) {
                if (idx > 0) ss << ',';
                if (pretty) {
                    ss << '\n' << std::string(static_cast<size_t>(currentIndent + indent), ' ');
                }
                // A key is a JSON string. Writing it raw meant a quote or a
                // backslash in the name produced a document that was not JSON;
                // wasm already ran the same escaper it uses for values.
                writeJsonString(ss, pair.first);
                ss << ':';
                if (pretty) ss << ' ';
                stringifyHelper(pair.second, ss, pretty, indent, currentIndent + indent);
                idx++;
            }
            if (pretty && !node->objectProperties.empty()) {
                ss << '\n' << std::string(static_cast<size_t>(currentIndent), ' ');
            }
            ss << '}';
            break;
        }
        }
    }
}

extern "C" {
    void* absolute_json_create_null() {
        auto node = new AbsoluteJsonNode();
        node->type = JSON_NULL;
        return node;
    }

    void* absolute_json_create_bool(int32_t val) {
        auto node = new AbsoluteJsonNode();
        node->type = JSON_BOOL;
        node->boolValue = (val != 0);
        return node;
    }

    void* absolute_json_create_number(double val) {
        auto node = new AbsoluteJsonNode();
        node->type = JSON_NUMBER;
        node->numberValue = val;
        return node;
    }

    void* absolute_json_create_string(const char* val) {
        auto node = new AbsoluteJsonNode();
        node->type = JSON_STRING;
        node->stringValue = val ? val : "";
        return node;
    }

    void* absolute_json_create_array() {
        auto node = new AbsoluteJsonNode();
        node->type = JSON_ARRAY;
        return node;
    }

    void* absolute_json_create_object() {
        auto node = new AbsoluteJsonNode();
        node->type = JSON_OBJECT;
        return node;
    }

    void absolute_json_free(void* handle) {
        if (handle) {
            delete static_cast<AbsoluteJsonNode*>(handle);
        }
    }

    int32_t absolute_json_get_type(const void* handle) {
        if (!handle) return JSON_NULL;
        return static_cast<int32_t>(static_cast<const AbsoluteJsonNode*>(handle)->type);
    }

    int32_t absolute_json_as_bool(const void* handle) {
        if (!handle) return 0;
        return static_cast<const AbsoluteJsonNode*>(handle)->boolValue ? 1 : 0;
    }

    double absolute_json_as_number(const void* handle) {
        if (!handle) return 0.0;
        return static_cast<const AbsoluteJsonNode*>(handle)->numberValue;
    }

    const char* absolute_json_as_string(const void* handle) {
        if (!handle) return "";
        lastJsonResult = static_cast<const AbsoluteJsonNode*>(handle)->stringValue;
        return lastJsonResult.c_str();
    }

    int32_t absolute_json_size(const void* handle) {
        if (!handle) return 0;
        auto node = static_cast<const AbsoluteJsonNode*>(handle);
        if (node->type == JSON_ARRAY) return static_cast<int32_t>(node->arrayElements.size());
        if (node->type == JSON_OBJECT) return static_cast<int32_t>(node->objectProperties.size());
        return 0;
    }

    void* absolute_json_get_array_elem(const void* handle, int32_t index) {
        if (!handle) return nullptr;
        auto node = static_cast<const AbsoluteJsonNode*>(handle);
        if (node->type != JSON_ARRAY || index < 0 || index >= static_cast<int32_t>(node->arrayElements.size())) {
            return nullptr;
        }
        return node->arrayElements[index];
    }

    int32_t absolute_json_has_property(const void* handle, const char* key) {
        if (!handle || !key) return 0;
        auto node = static_cast<const AbsoluteJsonNode*>(handle);
        if (node->type != JSON_OBJECT) return 0;
        return node->objectProperties.find(key) != node->objectProperties.end() ? 1 : 0;
    }

    void* absolute_json_get_property(const void* handle, const char* key) {
        if (!handle || !key) return nullptr;
        auto node = static_cast<const AbsoluteJsonNode*>(handle);
        if (node->type != JSON_OBJECT) return nullptr;
        auto it = node->objectProperties.find(key);
        if (it == node->objectProperties.end()) return nullptr;
        return it->second;
    }

    void absolute_json_array_add(void* handle, void* item) {
        if (!handle || !item) return;
        auto node = static_cast<AbsoluteJsonNode*>(handle);
        if (node->type == JSON_ARRAY) {
            node->arrayElements.push_back(static_cast<AbsoluteJsonNode*>(item));
        }
    }

    void absolute_json_object_set(void* handle, const char* key, void* item) {
        if (!handle || !key || !item) return;
        auto node = static_cast<AbsoluteJsonNode*>(handle);
        if (node->type == JSON_OBJECT) {
            auto it = node->objectProperties.find(key);
            if (it != node->objectProperties.end()) {
                delete it->second;
            }
            node->objectProperties[key] = static_cast<AbsoluteJsonNode*>(item);
        }
    }

    void* absolute_json_parse(const char* jsonText) {
        lastJsonError.clear();
        if (!jsonText || !*jsonText) {
            lastJsonError = "Empty JSON text";
            return nullptr;
        }
        JsonParser parser(jsonText);
        AbsoluteJsonNode* root = parser.parseValue();
        if (!root) {
            lastJsonError = parser.getError();
            return nullptr;
        }
        return root;
    }

    const char* absolute_json_stringify(const void* handle, int32_t pretty) {
        if (!handle) return "null";
        std::ostringstream ss;
        stringifyHelper(static_cast<const AbsoluteJsonNode*>(handle), ss, pretty != 0);
        lastJsonResult = ss.str();
        return lastJsonResult.c_str();
    }

    const char* absolute_json_get_last_error() {
        return lastJsonError.c_str();
    }
}
