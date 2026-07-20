#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

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
typedef const char* (*AbsoluteSyntaxPluginPreludeV1)(void);

typedef struct AbsoluteBinaryOperatorRuleV1 {
    const char* left_type;
    const char* operator_text;
    const char* right_type;
    const char* function_name;
    const char* result_type;
} AbsoluteBinaryOperatorRuleV1;

typedef struct AbsoluteBinaryOperatorTableV1 {
    size_t rule_count;
    const AbsoluteBinaryOperatorRuleV1* rules;
} AbsoluteBinaryOperatorTableV1;

typedef const AbsoluteBinaryOperatorTableV1* (*AbsoluteSyntaxPluginBinaryOperatorsV1)(void);

/* Opaque syntax rules keep their AST payload inside the plugin. The host only
   owns the payload through this versioned C vtable. */
typedef struct AbsoluteParserCursorV1 AbsoluteParserCursorV1;

typedef enum AbsoluteAttributeValueKindV1 {
    ABSOLUTE_ATTRIBUTE_IDENTIFIER = 0,
    ABSOLUTE_ATTRIBUTE_STRING = 1,
    ABSOLUTE_ATTRIBUTE_NUMBER = 2,
    ABSOLUTE_ATTRIBUTE_CHARACTER = 3,
    ABSOLUTE_ATTRIBUTE_BOOLEAN = 4
} AbsoluteAttributeValueKindV1;

typedef struct AbsoluteAttributeArgumentV1 {
    /* name is null for a positional argument. */
    const char* name;
    size_t name_length;
    uint32_t value_kind;
    const char* value;
    size_t value_length;
} AbsoluteAttributeArgumentV1;

typedef struct AbsoluteAttributeV1 {
    const char* name;
    size_t name_length;
    size_t argument_count;
    const AbsoluteAttributeArgumentV1* arguments;
} AbsoluteAttributeV1;

typedef size_t (*AbsoluteParserRemainingV1)(void* context);
typedef const AbsoluteSyntaxTokenV1* (*AbsoluteParserPeekV1)(void* context, size_t offset);
typedef int32_t (*AbsoluteParserConsumeV1)(
    void* context, uint32_t expected_kind, const char* expected_text);

struct AbsoluteParserCursorV1 {
    uint32_t abi_version;
    void* context;
    AbsoluteParserRemainingV1 remaining;
    AbsoluteParserPeekV1 peek;
    /* UINT32_MAX accepts any token kind; a null expected_text accepts any text. */
    AbsoluteParserConsumeV1 consume;
};

typedef struct AbsoluteOpaqueValidationContextV1 {
    uint32_t abi_version;
    uint32_t function_depth;
    const char* namespace_name;
    size_t attribute_count;
    const AbsoluteAttributeV1* attributes;
} AbsoluteOpaqueValidationContextV1;

typedef struct AbsoluteOpaqueLlvmContextV1 {
    uint32_t abi_version;
    const char* module_name;
    const char* target_triple;
    const char* data_layout;
    size_t attribute_count;
    const AbsoluteAttributeV1* attributes;
} AbsoluteOpaqueLlvmContextV1;

typedef int32_t (*AbsoluteOpaqueValidateV1)(
    void* payload, const AbsoluteOpaqueValidationContextV1* context, const char** error_message);
/* module_ir must contain one complete LLVM IR module. Callback-owned strings
   must remain valid until this callback is invoked again for the same node. */
typedef int32_t (*AbsoluteOpaqueEmitLlvmV1)(
    void* payload, const AbsoluteOpaqueLlvmContextV1* context,
    const char** module_ir, const char** error_message);
typedef const char* (*AbsoluteOpaqueDebugStringV1)(void* payload);
typedef void (*AbsoluteOpaqueDestroyV1)(void* payload);

typedef struct AbsoluteOpaqueAstVTableV1 {
    uint32_t abi_version;
    AbsoluteOpaqueDestroyV1 destroy;
    AbsoluteOpaqueDebugStringV1 debug_string;
    AbsoluteOpaqueValidateV1 validate;
    AbsoluteOpaqueEmitLlvmV1 emit_llvm;
} AbsoluteOpaqueAstVTableV1;

typedef struct AbsoluteOpaqueAstNodeV1 {
    /* The host calls vtable->destroy exactly once while the plugin is loaded. */
    void* payload;
    const AbsoluteOpaqueAstVTableV1* vtable;
} AbsoluteOpaqueAstNodeV1;

typedef struct AbsoluteOpaqueParseResultV1 {
    AbsoluteOpaqueAstNodeV1 node;
    const char* error_message;
} AbsoluteOpaqueParseResultV1;

typedef int32_t (*AbsoluteOpaqueParseV1)(
    void* user_data, AbsoluteParserCursorV1* parser, AbsoluteOpaqueParseResultV1* result);

typedef struct AbsoluteOpaqueSyntaxRuleV1 {
    const char* keyword;
    AbsoluteOpaqueParseV1 parse;
    void* user_data;
} AbsoluteOpaqueSyntaxRuleV1;

typedef struct AbsoluteOpaqueSyntaxTableV1 {
    size_t rule_count;
    const AbsoluteOpaqueSyntaxRuleV1* rules;
} AbsoluteOpaqueSyntaxTableV1;

typedef const AbsoluteOpaqueSyntaxTableV1* (*AbsoluteSyntaxPluginOpaqueRulesV1)(void);

/* Plugins define and export: absolute_syntax_plugin_init_v1. */
/* They may also export: absolute_syntax_plugin_prelude_v1. */
/* They may also export: absolute_syntax_plugin_binary_operators_v1. */
/* They may also export: absolute_syntax_plugin_opaque_rules_v1. */

typedef struct AbsoluteResourceDescriptorV1 {
    uint32_t struct_size;
    const char* type_name;
    bool is_resource;
    const char* destroy_function;
    const char* move_into_function;
} AbsoluteResourceDescriptorV1;

typedef struct AbsoluteResourceTableV1 {
    size_t descriptor_count;
    const AbsoluteResourceDescriptorV1* descriptors;
} AbsoluteResourceTableV1;

/* They may also export: absolute_syntax_plugin_resources_v1. */
typedef const AbsoluteResourceTableV1* (*AbsoluteSyntaxPluginResourcesV1)(void);

#ifdef __cplusplus
}
#endif
