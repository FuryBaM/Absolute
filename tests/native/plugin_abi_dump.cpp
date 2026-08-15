// Generated layout dump for the V1 plugin ABI.
//
// A plugin compiled against one release embeds these layouts in its own
// object code, so changing a size, an alignment or a field offset breaks
// every already-built plugin without any compiler diagnosing it. The
// checked-in golden file is what makes such a change visible in review
// rather than in a user's crash.

#include "plugin_api.h"

#include <cstddef>
#include <cstdio>

int main() {
    std::printf("# V1 plugin ABI layout\n");
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteSyntaxTokenKindV1", sizeof(AbsoluteSyntaxTokenKindV1), alignof(AbsoluteSyntaxTokenKindV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteSyntaxTokenV1", sizeof(AbsoluteSyntaxTokenV1), alignof(AbsoluteSyntaxTokenV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteSyntaxExpansionV1", sizeof(AbsoluteSyntaxExpansionV1), alignof(AbsoluteSyntaxExpansionV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteSyntaxRuleV1", sizeof(AbsoluteSyntaxRuleV1), alignof(AbsoluteSyntaxRuleV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteSyntaxPluginV1", sizeof(AbsoluteSyntaxPluginV1), alignof(AbsoluteSyntaxPluginV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteBinaryOperatorRuleV1", sizeof(AbsoluteBinaryOperatorRuleV1), alignof(AbsoluteBinaryOperatorRuleV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteBinaryOperatorTableV1", sizeof(AbsoluteBinaryOperatorTableV1), alignof(AbsoluteBinaryOperatorTableV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteAttributeValueKindV1", sizeof(AbsoluteAttributeValueKindV1), alignof(AbsoluteAttributeValueKindV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteAttributeArgumentV1", sizeof(AbsoluteAttributeArgumentV1), alignof(AbsoluteAttributeArgumentV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteAttributeV1", sizeof(AbsoluteAttributeV1), alignof(AbsoluteAttributeV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteDiagnosticSeverityV1", sizeof(AbsoluteDiagnosticSeverityV1), alignof(AbsoluteDiagnosticSeverityV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteDiagnosticV1", sizeof(AbsoluteDiagnosticV1), alignof(AbsoluteDiagnosticV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteOpaqueValidationContextV1", sizeof(AbsoluteOpaqueValidationContextV1), alignof(AbsoluteOpaqueValidationContextV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteOpaqueLlvmContextV1", sizeof(AbsoluteOpaqueLlvmContextV1), alignof(AbsoluteOpaqueLlvmContextV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteOpaqueAstVTableV1", sizeof(AbsoluteOpaqueAstVTableV1), alignof(AbsoluteOpaqueAstVTableV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteOpaqueAstNodeV1", sizeof(AbsoluteOpaqueAstNodeV1), alignof(AbsoluteOpaqueAstNodeV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteOpaqueParseResultV1", sizeof(AbsoluteOpaqueParseResultV1), alignof(AbsoluteOpaqueParseResultV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteSourceSpanV1", sizeof(AbsoluteSourceSpanV1), alignof(AbsoluteSourceSpanV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteSourceMapEntryV1", sizeof(AbsoluteSourceMapEntryV1), alignof(AbsoluteSourceMapEntryV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteSourceMapperV1", sizeof(AbsoluteSourceMapperV1), alignof(AbsoluteSourceMapperV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteOpaqueSyntaxRuleV1", sizeof(AbsoluteOpaqueSyntaxRuleV1), alignof(AbsoluteOpaqueSyntaxRuleV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteOpaqueSyntaxTableV1", sizeof(AbsoluteOpaqueSyntaxTableV1), alignof(AbsoluteOpaqueSyntaxTableV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteResourceDescriptorV1", sizeof(AbsoluteResourceDescriptorV1), alignof(AbsoluteResourceDescriptorV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteResourceTableV1", sizeof(AbsoluteResourceTableV1), alignof(AbsoluteResourceTableV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteSemanticContextV1", sizeof(AbsoluteSemanticContextV1), alignof(AbsoluteSemanticContextV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteTypeDescriptorV1", sizeof(AbsoluteTypeDescriptorV1), alignof(AbsoluteTypeDescriptorV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteOwnershipQueryV1", sizeof(AbsoluteOwnershipQueryV1), alignof(AbsoluteOwnershipQueryV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteVirtualModuleV1", sizeof(AbsoluteVirtualModuleV1), alignof(AbsoluteVirtualModuleV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteVirtualModuleTableV1", sizeof(AbsoluteVirtualModuleTableV1), alignof(AbsoluteVirtualModuleTableV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteArtifactKindV1", sizeof(AbsoluteArtifactKindV1), alignof(AbsoluteArtifactKindV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteArtifactV1", sizeof(AbsoluteArtifactV1), alignof(AbsoluteArtifactV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteBackendDescriptorV1", sizeof(AbsoluteBackendDescriptorV1), alignof(AbsoluteBackendDescriptorV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteBuildGraphV1", sizeof(AbsoluteBuildGraphV1), alignof(AbsoluteBuildGraphV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteCacheKeyV1", sizeof(AbsoluteCacheKeyV1), alignof(AbsoluteCacheKeyV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteCompilerPluginV1", sizeof(AbsoluteCompilerPluginV1), alignof(AbsoluteCompilerPluginV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteLanguagePluginV1", sizeof(AbsoluteLanguagePluginV1), alignof(AbsoluteLanguagePluginV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteIsolationModeV1", sizeof(AbsoluteIsolationModeV1), alignof(AbsoluteIsolationModeV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteIdeCapabilitiesV1", sizeof(AbsoluteIdeCapabilitiesV1), alignof(AbsoluteIdeCapabilitiesV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteDebuggerHooksV1", sizeof(AbsoluteDebuggerHooksV1), alignof(AbsoluteDebuggerHooksV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsolutePermissionPolicyV1", sizeof(AbsolutePermissionPolicyV1), alignof(AbsolutePermissionPolicyV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteEditorPluginV1", sizeof(AbsoluteEditorPluginV1), alignof(AbsoluteEditorPluginV1));
    std::printf("type %s size=%zu align=%zu\n", "AbsoluteRuntimePluginV1", sizeof(AbsoluteRuntimePluginV1), alignof(AbsoluteRuntimePluginV1));
    std::printf("# field offsets for plugin-authored structs\n");
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteSyntaxRuleV1", "keyword", offsetof(AbsoluteSyntaxRuleV1, keyword), sizeof(((AbsoluteSyntaxRuleV1*)0)->keyword));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteSyntaxRuleV1", "expand", offsetof(AbsoluteSyntaxRuleV1, expand), sizeof(((AbsoluteSyntaxRuleV1*)0)->expand));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteSyntaxRuleV1", "user_data", offsetof(AbsoluteSyntaxRuleV1, user_data), sizeof(((AbsoluteSyntaxRuleV1*)0)->user_data));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteSyntaxPluginV1", "abi_version", offsetof(AbsoluteSyntaxPluginV1, abi_version), sizeof(((AbsoluteSyntaxPluginV1*)0)->abi_version));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteSyntaxPluginV1", "name", offsetof(AbsoluteSyntaxPluginV1, name), sizeof(((AbsoluteSyntaxPluginV1*)0)->name));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteSyntaxPluginV1", "rule_count", offsetof(AbsoluteSyntaxPluginV1, rule_count), sizeof(((AbsoluteSyntaxPluginV1*)0)->rule_count));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteSyntaxPluginV1", "rules", offsetof(AbsoluteSyntaxPluginV1, rules), sizeof(((AbsoluteSyntaxPluginV1*)0)->rules));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteResourceDescriptorV1", "struct_size", offsetof(AbsoluteResourceDescriptorV1, struct_size), sizeof(((AbsoluteResourceDescriptorV1*)0)->struct_size));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteResourceDescriptorV1", "type_name", offsetof(AbsoluteResourceDescriptorV1, type_name), sizeof(((AbsoluteResourceDescriptorV1*)0)->type_name));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteResourceDescriptorV1", "is_resource", offsetof(AbsoluteResourceDescriptorV1, is_resource), sizeof(((AbsoluteResourceDescriptorV1*)0)->is_resource));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteResourceDescriptorV1", "destroy_function", offsetof(AbsoluteResourceDescriptorV1, destroy_function), sizeof(((AbsoluteResourceDescriptorV1*)0)->destroy_function));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteResourceDescriptorV1", "move_into_function", offsetof(AbsoluteResourceDescriptorV1, move_into_function), sizeof(((AbsoluteResourceDescriptorV1*)0)->move_into_function));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteResourceDescriptorV1", "copy_message_function", offsetof(AbsoluteResourceDescriptorV1, copy_message_function), sizeof(((AbsoluteResourceDescriptorV1*)0)->copy_message_function));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteResourceDescriptorV1", "make_immutable_function", offsetof(AbsoluteResourceDescriptorV1, make_immutable_function), sizeof(((AbsoluteResourceDescriptorV1*)0)->make_immutable_function));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteResourceDescriptorV1", "rehome_function", offsetof(AbsoluteResourceDescriptorV1, rehome_function), sizeof(((AbsoluteResourceDescriptorV1*)0)->rehome_function));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteCompilerPluginV1", "struct_size", offsetof(AbsoluteCompilerPluginV1, struct_size), sizeof(((AbsoluteCompilerPluginV1*)0)->struct_size));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteCompilerPluginV1", "abi_version", offsetof(AbsoluteCompilerPluginV1, abi_version), sizeof(((AbsoluteCompilerPluginV1*)0)->abi_version));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteCompilerPluginV1", "required_host_version", offsetof(AbsoluteCompilerPluginV1, required_host_version), sizeof(((AbsoluteCompilerPluginV1*)0)->required_host_version));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteCompilerPluginV1", "capability_flags", offsetof(AbsoluteCompilerPluginV1, capability_flags), sizeof(((AbsoluteCompilerPluginV1*)0)->capability_flags));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteCompilerPluginV1", "name", offsetof(AbsoluteCompilerPluginV1, name), sizeof(((AbsoluteCompilerPluginV1*)0)->name));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteCompilerPluginV1", "version", offsetof(AbsoluteCompilerPluginV1, version), sizeof(((AbsoluteCompilerPluginV1*)0)->version));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteCompilerPluginV1", "backend_descriptor", offsetof(AbsoluteCompilerPluginV1, backend_descriptor), sizeof(((AbsoluteCompilerPluginV1*)0)->backend_descriptor));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteCompilerPluginV1", "reserved", offsetof(AbsoluteCompilerPluginV1, reserved), sizeof(((AbsoluteCompilerPluginV1*)0)->reserved));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteLanguagePluginV1", "struct_size", offsetof(AbsoluteLanguagePluginV1, struct_size), sizeof(((AbsoluteLanguagePluginV1*)0)->struct_size));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteLanguagePluginV1", "abi_version", offsetof(AbsoluteLanguagePluginV1, abi_version), sizeof(((AbsoluteLanguagePluginV1*)0)->abi_version));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteLanguagePluginV1", "rule_count", offsetof(AbsoluteLanguagePluginV1, rule_count), sizeof(((AbsoluteLanguagePluginV1*)0)->rule_count));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteLanguagePluginV1", "rules", offsetof(AbsoluteLanguagePluginV1, rules), sizeof(((AbsoluteLanguagePluginV1*)0)->rules));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteLanguagePluginV1", "opaque_rule_count", offsetof(AbsoluteLanguagePluginV1, opaque_rule_count), sizeof(((AbsoluteLanguagePluginV1*)0)->opaque_rule_count));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteLanguagePluginV1", "opaque_rules", offsetof(AbsoluteLanguagePluginV1, opaque_rules), sizeof(((AbsoluteLanguagePluginV1*)0)->opaque_rules));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteLanguagePluginV1", "binary_operator_table", offsetof(AbsoluteLanguagePluginV1, binary_operator_table), sizeof(((AbsoluteLanguagePluginV1*)0)->binary_operator_table));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteLanguagePluginV1", "resource_table", offsetof(AbsoluteLanguagePluginV1, resource_table), sizeof(((AbsoluteLanguagePluginV1*)0)->resource_table));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteLanguagePluginV1", "virtual_module_table", offsetof(AbsoluteLanguagePluginV1, virtual_module_table), sizeof(((AbsoluteLanguagePluginV1*)0)->virtual_module_table));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteLanguagePluginV1", "reserved", offsetof(AbsoluteLanguagePluginV1, reserved), sizeof(((AbsoluteLanguagePluginV1*)0)->reserved));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteDiagnosticV1", "struct_size", offsetof(AbsoluteDiagnosticV1, struct_size), sizeof(((AbsoluteDiagnosticV1*)0)->struct_size));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteDiagnosticV1", "severity", offsetof(AbsoluteDiagnosticV1, severity), sizeof(((AbsoluteDiagnosticV1*)0)->severity));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteDiagnosticV1", "code", offsetof(AbsoluteDiagnosticV1, code), sizeof(((AbsoluteDiagnosticV1*)0)->code));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteDiagnosticV1", "message", offsetof(AbsoluteDiagnosticV1, message), sizeof(((AbsoluteDiagnosticV1*)0)->message));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteDiagnosticV1", "line", offsetof(AbsoluteDiagnosticV1, line), sizeof(((AbsoluteDiagnosticV1*)0)->line));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteDiagnosticV1", "column", offsetof(AbsoluteDiagnosticV1, column), sizeof(((AbsoluteDiagnosticV1*)0)->column));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteDiagnosticV1", "length", offsetof(AbsoluteDiagnosticV1, length), sizeof(((AbsoluteDiagnosticV1*)0)->length));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteDiagnosticV1", "note", offsetof(AbsoluteDiagnosticV1, note), sizeof(((AbsoluteDiagnosticV1*)0)->note));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsolutePermissionPolicyV1", "struct_size", offsetof(AbsolutePermissionPolicyV1, struct_size), sizeof(((AbsolutePermissionPolicyV1*)0)->struct_size));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsolutePermissionPolicyV1", "allow_filesystem_read", offsetof(AbsolutePermissionPolicyV1, allow_filesystem_read), sizeof(((AbsolutePermissionPolicyV1*)0)->allow_filesystem_read));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsolutePermissionPolicyV1", "allow_filesystem_write", offsetof(AbsolutePermissionPolicyV1, allow_filesystem_write), sizeof(((AbsolutePermissionPolicyV1*)0)->allow_filesystem_write));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsolutePermissionPolicyV1", "allow_network", offsetof(AbsolutePermissionPolicyV1, allow_network), sizeof(((AbsolutePermissionPolicyV1*)0)->allow_network));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsolutePermissionPolicyV1", "allow_environment", offsetof(AbsolutePermissionPolicyV1, allow_environment), sizeof(((AbsolutePermissionPolicyV1*)0)->allow_environment));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsolutePermissionPolicyV1", "allow_toolchain_exec", offsetof(AbsolutePermissionPolicyV1, allow_toolchain_exec), sizeof(((AbsolutePermissionPolicyV1*)0)->allow_toolchain_exec));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsolutePermissionPolicyV1", "allow_native_libraries", offsetof(AbsolutePermissionPolicyV1, allow_native_libraries), sizeof(((AbsolutePermissionPolicyV1*)0)->allow_native_libraries));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteTypeDescriptorV1", "struct_size", offsetof(AbsoluteTypeDescriptorV1, struct_size), sizeof(((AbsoluteTypeDescriptorV1*)0)->struct_size));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteTypeDescriptorV1", "name", offsetof(AbsoluteTypeDescriptorV1, name), sizeof(((AbsoluteTypeDescriptorV1*)0)->name));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteTypeDescriptorV1", "size_bytes", offsetof(AbsoluteTypeDescriptorV1, size_bytes), sizeof(((AbsoluteTypeDescriptorV1*)0)->size_bytes));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteTypeDescriptorV1", "alignment", offsetof(AbsoluteTypeDescriptorV1, alignment), sizeof(((AbsoluteTypeDescriptorV1*)0)->alignment));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteTypeDescriptorV1", "is_resource", offsetof(AbsoluteTypeDescriptorV1, is_resource), sizeof(((AbsoluteTypeDescriptorV1*)0)->is_resource));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteTypeDescriptorV1", "is_opaque", offsetof(AbsoluteTypeDescriptorV1, is_opaque), sizeof(((AbsoluteTypeDescriptorV1*)0)->is_opaque));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteTypeDescriptorV1", "is_primitive", offsetof(AbsoluteTypeDescriptorV1, is_primitive), sizeof(((AbsoluteTypeDescriptorV1*)0)->is_primitive));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteTypeDescriptorV1", "copy_function", offsetof(AbsoluteTypeDescriptorV1, copy_function), sizeof(((AbsoluteTypeDescriptorV1*)0)->copy_function));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteTypeDescriptorV1", "move_function", offsetof(AbsoluteTypeDescriptorV1, move_function), sizeof(((AbsoluteTypeDescriptorV1*)0)->move_function));
    std::printf("field %s.%s offset=%zu size=%zu\n", "AbsoluteTypeDescriptorV1", "destroy_function", offsetof(AbsoluteTypeDescriptorV1, destroy_function), sizeof(((AbsoluteTypeDescriptorV1*)0)->destroy_function));
    return 0;
}
