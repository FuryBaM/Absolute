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

typedef size_t (*AbsoluteParserCheckpointV1)(void* context);
typedef int32_t (*AbsoluteParserRestoreV1)(void* context, size_t checkpoint_id);
typedef int32_t (*AbsoluteParserCommitV1)(void* context, size_t checkpoint_id);
typedef const char* (*AbsoluteParserSourceSliceV1)(void* context, size_t start_token, size_t end_token);
typedef const char* (*AbsoluteParserCaptureUntilV1)(void* context, const char* end_delimiter, size_t* out_consumed);

struct AbsoluteParserCursorV1 {
    uint32_t abi_version;
    void* context;
    AbsoluteParserRemainingV1 remaining;
    AbsoluteParserPeekV1 peek;
    /* UINT32_MAX accepts any token kind; a null expected_text accepts any text. */
    AbsoluteParserConsumeV1 consume;
    AbsoluteParserCheckpointV1 checkpoint;
    AbsoluteParserRestoreV1 restore;
    AbsoluteParserCommitV1 commit;
    AbsoluteParserSourceSliceV1 source_slice;
    AbsoluteParserCaptureUntilV1 capture_until;
};

typedef enum AbsoluteDiagnosticSeverityV1 {
    ABSOLUTE_DIAGNOSTIC_NOTE = 0,
    ABSOLUTE_DIAGNOSTIC_WARNING = 1,
    ABSOLUTE_DIAGNOSTIC_ERROR = 2
} AbsoluteDiagnosticSeverityV1;

typedef struct AbsoluteDiagnosticV1 {
    uint32_t struct_size;
    uint32_t severity;
    const char* code;
    const char* message;
    uint32_t line;
    uint32_t column;
    uint32_t length;
    const char* note;
} AbsoluteDiagnosticV1;

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
typedef void* (*AbsoluteOpaqueCloneV1)(void* payload);
typedef int32_t (*AbsoluteOpaqueVisitChildrenV1)(void* payload, void* visitor, void (*callback)(void* child, void* visitor));
typedef const char* (*AbsoluteOpaqueLowerV1)(void* payload, const char** error_message);

typedef struct AbsoluteOpaqueAstVTableV1 {
    uint32_t abi_version;
    AbsoluteOpaqueDestroyV1 destroy;
    AbsoluteOpaqueDebugStringV1 debug_string;
    AbsoluteOpaqueValidateV1 validate;
    AbsoluteOpaqueEmitLlvmV1 emit_llvm;
    AbsoluteOpaqueCloneV1 clone;
    AbsoluteOpaqueVisitChildrenV1 visit_children;
    AbsoluteOpaqueLowerV1 lower;
} AbsoluteOpaqueAstVTableV1;

typedef struct AbsoluteOpaqueAstNodeV1 {
    /* The host calls vtable->destroy exactly once while the plugin is loaded. */
    void* payload;
    const AbsoluteOpaqueAstVTableV1* vtable;
} AbsoluteOpaqueAstNodeV1;

typedef struct AbsoluteOpaqueParseResultV1 {
    AbsoluteOpaqueAstNodeV1 node;
    const char* error_message;
    size_t diagnostic_count;
    const AbsoluteDiagnosticV1* diagnostics;
} AbsoluteOpaqueParseResultV1;

typedef int32_t (*AbsoluteOpaqueParseV1)(
    void* user_data, AbsoluteParserCursorV1* parser, AbsoluteOpaqueParseResultV1* result);

typedef struct AbsoluteSourceSpanV1 {
    const char* file_path;
    uint32_t start_line;
    uint32_t start_column;
    uint32_t end_line;
    uint32_t end_column;
} AbsoluteSourceSpanV1;

typedef struct AbsoluteSourceMapEntryV1 {
    AbsoluteSourceSpanV1 host_span;
    AbsoluteSourceSpanV1 generated_span;
    const char* foreign_language;
} AbsoluteSourceMapEntryV1;

typedef void (*AbsoluteMapGeneratedSpanV1)(
    void* map_context, const AbsoluteSourceSpanV1* host_span, const AbsoluteSourceSpanV1* generated_span);
typedef void (*AbsoluteMapForeignSpanV1)(
    void* map_context, const char* foreign_file, const AbsoluteSourceSpanV1* foreign_span, const AbsoluteSourceSpanV1* host_span);
typedef void (*AbsoluteMapIrInstructionV1)(
    void* map_context, void* llvm_instruction, const AbsoluteSourceSpanV1* host_span);

typedef struct AbsoluteSourceMapperV1 {
    uint32_t abi_version;
    void* context;
    AbsoluteMapGeneratedSpanV1 map_generated_span;
    AbsoluteMapForeignSpanV1 map_foreign_span;
    AbsoluteMapIrInstructionV1 map_ir_instruction;
} AbsoluteSourceMapperV1;

typedef struct AbsoluteOpaqueSyntaxRuleV1 {
    const char* keyword;
    const char* rule_namespace;
    AbsoluteOpaqueParseV1 parse;
    void* user_data;
} AbsoluteOpaqueSyntaxRuleV1;

typedef struct AbsoluteOpaqueSyntaxTableV1 {
    size_t rule_count;
    const AbsoluteOpaqueSyntaxRuleV1* rules;
} AbsoluteOpaqueSyntaxTableV1;

typedef const AbsoluteOpaqueSyntaxTableV1* (*AbsoluteSyntaxPluginOpaqueRulesV1)(void);

typedef struct AbsoluteResourceDescriptorV1 {
    uint32_t struct_size;
    const char* type_name;
    bool is_resource;
    const char* destroy_function;
    const char* move_into_function;
    const char* copy_message_function;
    const char* make_immutable_function;
    const char* rehome_function;
} AbsoluteResourceDescriptorV1;

typedef struct AbsoluteResourceTableV1 {
    size_t descriptor_count;
    const AbsoluteResourceDescriptorV1* descriptors;
} AbsoluteResourceTableV1;

typedef const AbsoluteResourceTableV1* (*AbsoluteSyntaxPluginResourcesV1)(void);

typedef struct AbsoluteSemanticContextV1 {
    uint32_t abi_version;
    const char* module_name;
    const char* namespace_name;
    const char* function_name;
    const char* target_platform;
    uint32_t pointer_mode; /* 0 = managed, 1 = raw */
    void* context;
    const char* (*resolve_symbol)(void* context, const char* symbol_name);
    const char* (*resolve_type)(void* context, const char* type_name);
    int32_t (*declare_symbol)(void* context, const char* name, const char* type_name);
    int32_t (*declare_type)(void* context, const char* name, size_t size, size_t alignment);
    int32_t (*check_conversion)(void* context, const char* from_type, const char* to_type);
    int32_t (*infer_type)(void* context, const char* expr_string, const char** out_type);
    void (*report_diagnostic)(void* context, const AbsoluteDiagnosticV1* diagnostic);
} AbsoluteSemanticContextV1;

typedef struct AbsoluteTypeDescriptorV1 {
    uint32_t struct_size;
    const char* name;
    size_t size_bytes;
    size_t alignment;
    bool is_resource;
    bool is_opaque;
    bool is_primitive;
    const char* copy_function;
    const char* move_function;
    const char* destroy_function;
} AbsoluteTypeDescriptorV1;

typedef struct AbsoluteOwnershipQueryV1 {
    uint32_t abi_version;
    void* context;
    uint32_t (*query_pointer_mode)(void* context, const char* var_name);
    int32_t (*require_raw)(void* context, const char* var_name);
    int32_t (*require_managed)(void* context, const char* var_name);
    int32_t (*transfer_ownership)(void* context, const char* var_name, const char* target_var);
    int32_t (*register_resource)(void* context, const AbsoluteResourceDescriptorV1* resource);
    void (*mark_escape)(void* context, const char* var_name);
} AbsoluteOwnershipQueryV1;

typedef struct AbsoluteVirtualModuleV1 {
    const char* module_name;
    const char* module_source;
} AbsoluteVirtualModuleV1;

typedef struct AbsoluteVirtualModuleTableV1 {
    size_t module_count;
    const AbsoluteVirtualModuleV1* modules;
} AbsoluteVirtualModuleTableV1;

typedef const AbsoluteVirtualModuleTableV1* (*AbsolutePluginVirtualModulesV1)(void);

typedef enum AbsoluteArtifactKindV1 {
    ABSOLUTE_ARTIFACT_LLVM_IR = 0,
    ABSOLUTE_ARTIFACT_LLVM_BITCODE = 1,
    ABSOLUTE_ARTIFACT_OBJECT = 2,
    ABSOLUTE_ARTIFACT_STATIC_LIB = 3,
    ABSOLUTE_ARTIFACT_SHARED_LIB = 4,
    ABSOLUTE_ARTIFACT_SPIRV = 5,
    ABSOLUTE_ARTIFACT_DXIL = 6,
    ABSOLUTE_ARTIFACT_PTX = 7,
    ABSOLUTE_ARTIFACT_CUBIN = 8,
    ABSOLUTE_ARTIFACT_AMDGPU = 9,
    ABSOLUTE_ARTIFACT_SOURCE_TEXT = 10,
    ABSOLUTE_ARTIFACT_CUSTOM_BINARY = 11
} AbsoluteArtifactKindV1;

typedef struct AbsoluteArtifactV1 {
    uint32_t struct_size;
    uint32_t kind;
    const char* file_path;
    const char* target_triple;
    const uint8_t* data;
    size_t data_size;
} AbsoluteArtifactV1;

typedef struct AbsoluteBackendDescriptorV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const char* target_name;
    int32_t (*supports_target)(const char* target_triple);
    int32_t (*emit_artifact)(void* context, uint32_t artifact_kind, const AbsoluteArtifactV1** out_artifact);
    int32_t (*link_artifact)(void* context, const AbsoluteArtifactV1* artifact, const char* output_file);
    const char* (*query_target_features)(const char* target_triple);
} AbsoluteBackendDescriptorV1;

typedef struct AbsoluteBuildGraphV1 {
    uint32_t abi_version;
    void* context;
    int32_t (*add_dependency)(void* context, const char* source, const char* depends_on);
    int32_t (*add_include_path)(void* context, const char* path);
    int32_t (*add_link_library)(void* context, const char* library);
    int32_t (*register_generated_file)(void* context, const char* file_path);
} AbsoluteBuildGraphV1;

typedef struct AbsoluteCacheKeyV1 {
    const char* plugin_name;
    const char* plugin_version;
    uint64_t schema_hash;
    const char* artifact_hash;
} AbsoluteCacheKeyV1;

#define ABSOLUTE_CAPABILITY_PARSER    (1ULL << 0)
#define ABSOLUTE_CAPABILITY_SEMANTIC  (1ULL << 1)
#define ABSOLUTE_CAPABILITY_IR        (1ULL << 2)
#define ABSOLUTE_CAPABILITY_BACKEND   (1ULL << 3)
#define ABSOLUTE_CAPABILITY_IDE       (1ULL << 4)

typedef struct AbsoluteCompilerPluginV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t required_host_version;
    uint64_t capability_flags;
    const char* name;
    const char* version;
    int32_t (*initialize)(void* compiler_context);
    int32_t (*begin_compilation)(void* compiler_context, const char* module_name);
    int32_t (*begin_module)(void* compiler_context, const char* module_name, const char* source_path);
    int32_t (*end_module)(void* compiler_context, const char* module_name);
    int32_t (*end_compilation)(void* compiler_context);
    void (*shutdown)(void* compiler_context);
    const AbsoluteBackendDescriptorV1* backend_descriptor;
    uint64_t reserved[3];
} AbsoluteCompilerPluginV1;

typedef struct AbsoluteLanguagePluginV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    size_t rule_count;
    const AbsoluteSyntaxRuleV1* rules;
    size_t opaque_rule_count;
    const AbsoluteOpaqueSyntaxRuleV1* opaque_rules;
    const AbsoluteBinaryOperatorTableV1* binary_operator_table;
    const AbsoluteResourceTableV1* resource_table;
    const AbsoluteVirtualModuleTableV1* virtual_module_table;
    uint64_t reserved[4];
} AbsoluteLanguagePluginV1;

typedef enum AbsoluteIsolationModeV1 {
    ABSOLUTE_ISOLATION_TRUSTED_IN_PROCESS = 0,
    ABSOLUTE_ISOLATION_ISOLATED_PROCESS = 1,
    ABSOLUTE_ISOLATION_SANDBOX = 2,
    ABSOLUTE_ISOLATION_WASM = 3
} AbsoluteIsolationModeV1;

typedef struct AbsoluteIdeCapabilitiesV1 {
    uint32_t struct_size;
    const char* (*get_completions_json)(const char* file_path, uint32_t line, uint32_t column);
    const char* (*get_definition_json)(const char* file_path, uint32_t line, uint32_t column);
    const char* (*get_references_json)(const char* file_path, uint32_t line, uint32_t column);
    const char* (*get_semantic_tokens_json)(const char* file_path);
    const char* (*format_source)(const char* source_code);
    const char* (*get_code_actions_json)(const char* file_path, uint32_t line, uint32_t column);
    const char* (*rename_symbol)(const char* file_path, uint32_t line, uint32_t column, const char* new_name);
    const char* (*get_embedded_language)(const char* block_tag);
} AbsoluteIdeCapabilitiesV1;

typedef struct AbsoluteDebuggerHooksV1 {
    uint32_t struct_size;
    const char* (*render_runtime_value)(const char* type_name, const void* value_ptr);
    const char* (*get_debug_info_json)(const char* symbol_name);
    void (*on_profiler_event)(const char* event_name, uint64_t timestamp_ns);
} AbsoluteDebuggerHooksV1;

typedef struct AbsolutePermissionPolicyV1 {
    uint32_t struct_size;
    uint32_t allow_filesystem_read;
    uint32_t allow_filesystem_write;
    uint32_t allow_network;
    uint32_t allow_environment;
    uint32_t allow_toolchain_exec;
    uint32_t allow_native_libraries;
} AbsolutePermissionPolicyV1;

typedef struct AbsoluteEditorPluginV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const char* (*get_keywords_json)(void);
    const char* (*get_snippets_json)(void);
    const char* (*get_hover_info)(const char* symbol_name);
    const AbsoluteIdeCapabilitiesV1* ide_capabilities;
    const AbsoluteDebuggerHooksV1* debugger_hooks;
    uint64_t reserved[2];
} AbsoluteEditorPluginV1;

typedef struct AbsoluteRuntimePluginV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    int32_t (*init_runtime)(void);
    void (*cleanup_runtime)(void);
    uint64_t reserved[4];
} AbsoluteRuntimePluginV1;

typedef const AbsoluteCompilerPluginV1* (*AbsoluteCompilerPluginInitV1)(void);
typedef const AbsoluteLanguagePluginV1* (*AbsoluteLanguagePluginInitV1)(void);
typedef const AbsoluteEditorPluginV1* (*AbsoluteEditorPluginInitV1)(void);
typedef const AbsoluteRuntimePluginV1* (*AbsoluteRuntimePluginInitV1)(void);

#ifdef __cplusplus
}
#endif
