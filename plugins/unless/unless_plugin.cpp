#include "plugin_api.h"

#include <cstddef>
#include <string>

namespace {
    thread_local std::string replacement;
    thread_local std::string error;

    std::string Text(const AbsoluteSyntaxTokenV1& token) {
        return std::string(token.text, token.text_length);
    }

    bool Is(const AbsoluteSyntaxTokenV1& token, const char* value) {
        return Text(token) == value;
    }

    size_t FindMatching(const AbsoluteSyntaxTokenV1* tokens, size_t count, size_t opening,
        const char* open, const char* close) {
        size_t depth = 0;
        for (size_t index = opening; index < count; ++index) {
            if (Is(tokens[index], open)) ++depth;
            else if (Is(tokens[index], close) && --depth == 0) return index;
        }
        return count;
    }

    void AppendTokens(std::string& output, const AbsoluteSyntaxTokenV1* tokens,
        size_t begin, size_t end) {
        for (size_t index = begin; index < end; ++index) {
            if (!output.empty()) output.push_back(' ');
            output.append(tokens[index].text, tokens[index].text_length);
        }
    }

    int32_t ExpandUnless(void*, const AbsoluteSyntaxTokenV1* tokens, size_t count,
        AbsoluteSyntaxExpansionV1* expansion) {
        replacement.clear();
        error.clear();
        expansion->consumed_tokens = 0;
        expansion->replacement_source = nullptr;
        expansion->error_message = nullptr;

        if (count < 4 || !Is(tokens[0], "unless") || !Is(tokens[1], "(")) {
            error = "expected 'unless (condition) statement'";
            expansion->error_message = error.c_str();
            return 0;
        }

        const size_t conditionClose = FindMatching(tokens, count, 1, "(", ")");
        if (conditionClose == count || conditionClose == 2) {
            error = "unless requires a non-empty, closed condition";
            expansion->error_message = error.c_str();
            return 0;
        }
        const size_t bodyStart = conditionClose + 1;
        if (bodyStart >= count) {
            error = "unless requires a body";
            expansion->error_message = error.c_str();
            return 0;
        }

        size_t bodyEnd = count;
        const bool compoundBody = Is(tokens[bodyStart], "{");
        if (compoundBody) {
            const size_t closingBrace = FindMatching(tokens, count, bodyStart, "{", "}");
            if (closingBrace == count) {
                error = "unterminated unless body";
                expansion->error_message = error.c_str();
                return 0;
            }
            bodyEnd = closingBrace + 1;
        }
        else {
            size_t roundDepth = 0;
            size_t squareDepth = 0;
            size_t braceDepth = 0;
            for (size_t index = bodyStart; index < count; ++index) {
                if (Is(tokens[index], "(")) ++roundDepth;
                else if (Is(tokens[index], ")") && roundDepth != 0) --roundDepth;
                else if (Is(tokens[index], "[")) ++squareDepth;
                else if (Is(tokens[index], "]") && squareDepth != 0) --squareDepth;
                else if (Is(tokens[index], "{")) ++braceDepth;
                else if (Is(tokens[index], "}") && braceDepth != 0) --braceDepth;
                else if (Is(tokens[index], ";") && roundDepth == 0 && squareDepth == 0 && braceDepth == 0) {
                    bodyEnd = index + 1;
                    break;
                }
            }
            if (bodyEnd == count && !Is(tokens[count - 1], ";")) {
                error = "unterminated single-statement unless body";
                expansion->error_message = error.c_str();
                return 0;
            }
        }

        replacement = "if ( ! (";
        AppendTokens(replacement, tokens, 2, conditionClose);
        replacement += ") )";
        if (!compoundBody) replacement += " {";
        AppendTokens(replacement, tokens, bodyStart, bodyEnd);
        if (!compoundBody) replacement += " }";
        expansion->consumed_tokens = bodyEnd;
        expansion->replacement_source = replacement.c_str();
        return 1;
    }

    const AbsoluteSyntaxRuleV1 rules[] = {
        {"unless", &ExpandUnless, nullptr}
    };

    const AbsoluteSyntaxPluginV1 plugin = {
        ABSOLUTE_SYNTAX_PLUGIN_ABI_VERSION,
        "absolute.unless",
        1,
        rules
    };

    const AbsoluteResourceDescriptorV1 resources[] = {
        {sizeof(AbsoluteResourceDescriptorV1), "TestResource", true, "test_resource_destroy", nullptr, nullptr, nullptr, nullptr},
        {sizeof(AbsoluteResourceDescriptorV1), "TransferrableResource", true, "test_resource_destroy", nullptr, "test_resource_copy", nullptr, nullptr}
    };
    
    const AbsoluteResourceTableV1 resourceTable = {
        2,
        resources
    };
}

extern "C" ABSOLUTE_PLUGIN_EXPORT const AbsoluteSyntaxPluginV1* absolute_syntax_plugin_init_v1() {
    return &plugin;
}

extern "C" ABSOLUTE_PLUGIN_EXPORT const AbsoluteResourceTableV1* absolute_syntax_plugin_resources_v1() {
    return &resourceTable;
}
