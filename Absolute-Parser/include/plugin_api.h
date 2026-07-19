#pragma once

#include <stddef.h>
#include <stdint.h>

#define ABSOLUTE_SYNTAX_PLUGIN_ABI_VERSION 1u

#if defined(_WIN32)
#define ABSOLUTE_PLUGIN_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define ABSOLUTE_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define ABSOLUTE_PLUGIN_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AbsoluteSyntaxTokenKindV1 {
    ABSOLUTE_SYNTAX_TOKEN_NUMBER = 0,
    ABSOLUTE_SYNTAX_TOKEN_KEYWORD = 1,
    ABSOLUTE_SYNTAX_TOKEN_IDENTIFIER = 2,
    ABSOLUTE_SYNTAX_TOKEN_OPERATOR = 3,
    ABSOLUTE_SYNTAX_TOKEN_DELIMITER = 4,
    ABSOLUTE_SYNTAX_TOKEN_STRING = 5,
    ABSOLUTE_SYNTAX_TOKEN_CHAR = 6,
    ABSOLUTE_SYNTAX_TOKEN_COMMENT = 7,
    ABSOLUTE_SYNTAX_TOKEN_BRACKET = 8,
    ABSOLUTE_SYNTAX_TOKEN_WHITESPACE = 9
} AbsoluteSyntaxTokenKindV1;

typedef struct AbsoluteSyntaxTokenV1 {
    uint32_t kind;
    const char* text;
    size_t text_length;
    uint32_t line;
    uint32_t column;
} AbsoluteSyntaxTokenV1;

typedef struct AbsoluteSyntaxExpansionV1 {
    size_t consumed_tokens;
    /* These strings must remain valid until this adapter is called again. */
    const char* replacement_source;
    const char* error_message;
} AbsoluteSyntaxExpansionV1;

/* Adapters consume plugin syntax and return ordinary Absolute source. */
typedef int32_t (*AbsoluteSyntaxExpandV1)(
    void* user_data,
    const AbsoluteSyntaxTokenV1* tokens,
    size_t token_count,
    AbsoluteSyntaxExpansionV1* expansion);

typedef struct AbsoluteSyntaxRuleV1 {
    const char* keyword;
    AbsoluteSyntaxExpandV1 expand;
    void* user_data;
} AbsoluteSyntaxRuleV1;

typedef struct AbsoluteSyntaxPluginV1 {
    uint32_t abi_version;
    /* The descriptor, name, rules and keywords live until the plugin unloads. */
    const char* name;
    size_t rule_count;
    const AbsoluteSyntaxRuleV1* rules;
} AbsoluteSyntaxPluginV1;

typedef const AbsoluteSyntaxPluginV1* (*AbsoluteSyntaxPluginInitV1)(void);

/* Plugins define and export: absolute_syntax_plugin_init_v1. */

#ifdef __cplusplus
}
#endif
