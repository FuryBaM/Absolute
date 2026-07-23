#include "plugin_api.h"

#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {
    struct ShaderNode {
        std::string stage;
        std::vector<std::string> inputs;
        std::vector<std::string> outputs;
        std::string debugText;
        std::string moduleIr;
    };

    thread_local std::string lastError;
    extern const AbsoluteOpaqueAstVTableV1 shaderNodeVTable;

    std::string TokenText(const AbsoluteSyntaxTokenV1* token) {
        return token ? std::string(token->text, token->text_length) : std::string{};
    }

    bool Consume(AbsoluteParserCursorV1* parser, uint32_t kind, const char* text) {
        return parser && parser->consume && parser->consume(parser->context, kind, text) != 0;
    }

    bool Fail(AbsoluteOpaqueParseResultV1* result, std::string message) {
        lastError = std::move(message);
        result->error_message = lastError.c_str();
        return false;
    }

    int32_t ParseShader(void*, AbsoluteParserCursorV1* parser, AbsoluteOpaqueParseResultV1* result) {
        if (!parser || !result || parser->abi_version != ABSOLUTE_SYNTAX_PLUGIN_ABI_VERSION)
            return 0;
        *result = {};
        if (!Consume(parser, ABSOLUTE_SYNTAX_TOKEN_KEYWORD, "shader"))
            return Fail(result, "expected 'shader'");

        const AbsoluteSyntaxTokenV1* stage = parser->peek(parser->context, 0);
        if (!stage || stage->kind != ABSOLUTE_SYNTAX_TOKEN_IDENTIFIER)
            return Fail(result, "expected a shader stage name after 'shader'");
        auto node = std::make_unique<ShaderNode>();
        node->stage = TokenText(stage);
        if (!Consume(parser, ABSOLUTE_SYNTAX_TOKEN_IDENTIFIER, nullptr) ||
            !Consume(parser, ABSOLUTE_SYNTAX_TOKEN_BRACKET, "{"))
            return Fail(result, "expected '{' after the shader stage name");

        while (const AbsoluteSyntaxTokenV1* token = parser->peek(parser->context, 0)) {
            const std::string declaration = TokenText(token);
            if (token->kind == ABSOLUTE_SYNTAX_TOKEN_BRACKET && declaration == "}") {
                Consume(parser, ABSOLUTE_SYNTAX_TOKEN_BRACKET, "}");
                result->node.payload = node.release();
                result->node.vtable = &shaderNodeVTable;
                return 1;
            }
            if (token->kind != ABSOLUTE_SYNTAX_TOKEN_IDENTIFIER ||
                (declaration != "input" && declaration != "output"))
                return Fail(result, "expected 'input', 'output', or '}' in shader block");
            Consume(parser, ABSOLUTE_SYNTAX_TOKEN_IDENTIFIER, declaration.c_str());
            const AbsoluteSyntaxTokenV1* variable = parser->peek(parser->context, 0);
            if (!variable || variable->kind != ABSOLUTE_SYNTAX_TOKEN_IDENTIFIER)
                return Fail(result, "shader input/output requires an identifier");
            const std::string variableName = TokenText(variable);
            Consume(parser, ABSOLUTE_SYNTAX_TOKEN_IDENTIFIER, nullptr);
            if (!Consume(parser, ABSOLUTE_SYNTAX_TOKEN_DELIMITER, ";"))
                return Fail(result, "shader declaration requires ';'");
            (declaration == "input" ? node->inputs : node->outputs).push_back(variableName);
        }
        return Fail(result, "unterminated shader block");
    }

    void DestroyShader(void* payload) {
        delete static_cast<ShaderNode*>(payload);
    }

    const char* DebugShader(void* payload) {
        auto& shader = *static_cast<ShaderNode*>(payload);
        shader.debugText = "shader " + shader.stage + " (inputs=" +
            std::to_string(shader.inputs.size()) + ", outputs=" +
            std::to_string(shader.outputs.size()) + ")";
        return shader.debugText.c_str();
    }

    int32_t ValidateShader(void* payload, const AbsoluteOpaqueValidationContextV1* context,
        const char** errorMessage) {
        auto& shader = *static_cast<ShaderNode*>(payload);
        if (!context || context->abi_version != ABSOLUTE_SYNTAX_PLUGIN_ABI_VERSION) {
            lastError = "unsupported validation context";
        }
        else if (context->function_depth != 0) {
            lastError = "shader declarations are allowed only at module or namespace scope";
        }
        else if (shader.outputs.empty()) {
            lastError = "shader requires at least one output";
        }
        else {
            for (size_t index = 0; index < context->attribute_count; ++index) {
                const AbsoluteAttributeV1& attribute = context->attributes[index];
                const std::string name(attribute.name, attribute.name_length);
                if (name != "shader.stage") continue;
                if (attribute.argument_count != 1 || !attribute.arguments) {
                    lastError = "@shader.stage requires one stage name";
                    if (errorMessage) *errorMessage = lastError.c_str();
                    return 0;
                }
                const AbsoluteAttributeArgumentV1& argument = attribute.arguments[0];
                const std::string stage(argument.value, argument.value_length);
                if (argument.value_kind != ABSOLUTE_ATTRIBUTE_IDENTIFIER || stage != shader.stage) {
                    lastError = "@shader.stage must match the shader block stage";
                    if (errorMessage) *errorMessage = lastError.c_str();
                    return 0;
                }
            }
            if (errorMessage) *errorMessage = nullptr;
            return 1;
        }
        if (errorMessage) *errorMessage = lastError.c_str();
        return 0;
    }

    int32_t EmitShaderLlvm(void* payload, const AbsoluteOpaqueLlvmContextV1* context,
        const char** moduleIr, const char** errorMessage) {
        auto& shader = *static_cast<ShaderNode*>(payload);
        if (!context || context->abi_version != ABSOLUTE_SYNTAX_PLUGIN_ABI_VERSION) {
            lastError = "unsupported LLVM emission context";
            if (errorMessage) *errorMessage = lastError.c_str();
            return 0;
        }
        std::ostringstream ir;
        ir << "; ModuleID = 'absolute.shader." << shader.stage << "'\n";
        ir << "source_filename = \"absolute.shader." << shader.stage << "\"\n";
        ir << "@absolute.shader." << shader.stage << ".input_count = constant i32 "
           << shader.inputs.size() << "\n";
        ir << "@absolute.shader." << shader.stage << ".output_count = constant i32 "
           << shader.outputs.size() << "\n";
        ir << "@absolute.shader." << shader.stage << ".metadata_count = constant i32 "
           << context->attribute_count << "\n";
        ir << "define void @absolute.shader." << shader.stage << "() {\n";
        ir << "entry:\n  ret void\n}\n";
        shader.moduleIr = ir.str();
        if (moduleIr) *moduleIr = shader.moduleIr.c_str();
        if (errorMessage) *errorMessage = nullptr;
        return 1;
    }

    const AbsoluteOpaqueAstVTableV1 shaderNodeVTable = {
        ABSOLUTE_SYNTAX_PLUGIN_ABI_VERSION,
        &DestroyShader,
        &DebugShader,
        &ValidateShader,
        &EmitShaderLlvm
    };

    const AbsoluteOpaqueSyntaxRuleV1 opaqueRules[] = {
        {"shader", nullptr, &ParseShader, nullptr}
    };

    const AbsoluteOpaqueSyntaxTableV1 opaqueTable = {
        sizeof(opaqueRules) / sizeof(opaqueRules[0]),
        opaqueRules
    };

    const AbsoluteSyntaxPluginV1 plugin = {
        ABSOLUTE_SYNTAX_PLUGIN_ABI_VERSION,
        "absolute.shader",
        0,
        nullptr
    };
}

extern "C" ABSOLUTE_PLUGIN_EXPORT const AbsoluteSyntaxPluginV1* absolute_syntax_plugin_init_v1() {
    return &plugin;
}

extern "C" ABSOLUTE_PLUGIN_EXPORT const AbsoluteOpaqueSyntaxTableV1*
absolute_syntax_plugin_opaque_rules_v1() {
    return &opaqueTable;
}
