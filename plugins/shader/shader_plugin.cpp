// absolute.shader — opaque DSL with typed I/O, uniforms, reflection metadata,
// and generated GLSL 330 for Desktop.Gpu RHI bind (OpenGL backend).

#include "plugin_api.h"

#include <cctype>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {
    enum class ShaderTypeKind {
        Float1,
        Float2,
        Float3,
        Float4,
        Int1,
        Unknown
    };

    struct ShaderVar {
        std::string name;
        std::string typeName; // original token text
        ShaderTypeKind kind = ShaderTypeKind::Float3;
        int32_t components = 3;
        int32_t location = -1; // assigned for inputs
    };

    struct ShaderNode {
        std::string stage;
        std::vector<ShaderVar> inputs;
        std::vector<ShaderVar> outputs;
        std::vector<ShaderVar> uniforms;
        std::string debugText;
        std::string moduleIr;
        std::string glslSource;
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

    ShaderTypeKind ParseTypeName(const std::string& text, int32_t& components) {
        if (text == "float" || text == "float1") {
            components = 1;
            return ShaderTypeKind::Float1;
        }
        if (text == "float2" || text == "vec2") {
            components = 2;
            return ShaderTypeKind::Float2;
        }
        if (text == "float3" || text == "vec3") {
            components = 3;
            return ShaderTypeKind::Float3;
        }
        if (text == "float4" || text == "vec4") {
            components = 4;
            return ShaderTypeKind::Float4;
        }
        if (text == "int" || text == "int1") {
            components = 1;
            return ShaderTypeKind::Int1;
        }
        components = 0;
        return ShaderTypeKind::Unknown;
    }

    // Infer type when only a name is given (legacy `input position;`).
    void InferTypeFromName(ShaderVar& var) {
        const std::string& n = var.name;
        if (n == "uv" || n == "texcoord" || n == "texCoord" || n.find("uv") != std::string::npos) {
            var.kind = ShaderTypeKind::Float2;
            var.components = 2;
            var.typeName = "float2";
            return;
        }
        if (n == "color" || n == "clipPosition" || n == "position4" || n == "FragColor") {
            var.kind = ShaderTypeKind::Float4;
            var.components = 4;
            var.typeName = "float4";
            return;
        }
        // position, normal, tangent, etc.
        var.kind = ShaderTypeKind::Float3;
        var.components = 3;
        var.typeName = "float3";
    }

    const char* GlslType(const ShaderVar& var) {
        switch (var.kind) {
        case ShaderTypeKind::Float1: return "float";
        case ShaderTypeKind::Float2: return "vec2";
        case ShaderTypeKind::Float3: return "vec3";
        case ShaderTypeKind::Float4: return "vec4";
        case ShaderTypeKind::Int1: return "int";
        default: return "vec3";
        }
    }

    bool IsTypeToken(const std::string& text) {
        int32_t c = 0;
        return ParseTypeName(text, c) != ShaderTypeKind::Unknown;
    }

    bool ParseVarDecl(AbsoluteParserCursorV1* parser, AbsoluteOpaqueParseResultV1* result,
        ShaderVar& out) {
        out = {};
        const AbsoluteSyntaxTokenV1* first = parser->peek(parser->context, 0);
        if (!first)
            return Fail(result, "expected type or identifier in shader declaration");

        // Absolute lexes `float` as a keyword and `float3` as keyword `float` + number `3`.
        // Also accept identifier types: float2/vec2/float3/vec3/float4/vec4/int.
        std::string typeText;
        bool hasType = false;

        if (first->kind == ABSOLUTE_SYNTAX_TOKEN_KEYWORD || first->kind == ABSOLUTE_SYNTAX_TOKEN_IDENTIFIER) {
            const std::string firstText = TokenText(first);
            if (firstText == "float") {
                Consume(parser, first->kind, nullptr);
                typeText = "float";
                const AbsoluteSyntaxTokenV1* num = parser->peek(parser->context, 0);
                if (num && num->kind == ABSOLUTE_SYNTAX_TOKEN_NUMBER) {
                    const std::string n = TokenText(num);
                    if (n == "1" || n == "2" || n == "3" || n == "4") {
                        Consume(parser, ABSOLUTE_SYNTAX_TOKEN_NUMBER, nullptr);
                        typeText += n;
                    }
                }
                hasType = true;
            } else if (firstText == "int") {
                Consume(parser, first->kind, nullptr);
                typeText = "int";
                hasType = true;
            } else if (IsTypeToken(firstText)) {
                Consume(parser, first->kind, nullptr);
                typeText = firstText;
                hasType = true;
            }
        }

        if (hasType) {
            int32_t components = 0;
            out.kind = ParseTypeName(typeText, components);
            if (out.kind == ShaderTypeKind::Unknown)
                return Fail(result, "unsupported shader type");
            out.components = components;
            out.typeName = typeText;
            const AbsoluteSyntaxTokenV1* nameTok = parser->peek(parser->context, 0);
            if (!nameTok || nameTok->kind != ABSOLUTE_SYNTAX_TOKEN_IDENTIFIER)
                return Fail(result, "expected variable name after type");
            out.name = TokenText(nameTok);
            Consume(parser, ABSOLUTE_SYNTAX_TOKEN_IDENTIFIER, nullptr);
        } else if (first->kind == ABSOLUTE_SYNTAX_TOKEN_IDENTIFIER) {
            // Legacy: `input position;`
            out.name = TokenText(first);
            Consume(parser, ABSOLUTE_SYNTAX_TOKEN_IDENTIFIER, nullptr);
            InferTypeFromName(out);
        } else {
            return Fail(result, "expected type or identifier in shader declaration");
        }
        if (!Consume(parser, ABSOLUTE_SYNTAX_TOKEN_DELIMITER, ";"))
            return Fail(result, "shader declaration requires ';'");
        return true;
    }

    int32_t ParseShader(void*, AbsoluteParserCursorV1* parser, AbsoluteOpaqueParseResultV1* result) {
        if (!parser || !result || parser->abi_version != ABSOLUTE_SYNTAX_PLUGIN_ABI_VERSION)
            return 0;
        *result = {};
        if (!Consume(parser, ABSOLUTE_SYNTAX_TOKEN_KEYWORD, "shader") &&
            !Consume(parser, ABSOLUTE_SYNTAX_TOKEN_IDENTIFIER, "shader"))
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
                // Assign input locations in order.
                int32_t loc = 0;
                for (ShaderVar& in : node->inputs) {
                    in.location = loc++;
                }
                result->node.payload = node.release();
                result->node.vtable = &shaderNodeVTable;
                return 1;
            }
            if (token->kind != ABSOLUTE_SYNTAX_TOKEN_IDENTIFIER &&
                token->kind != ABSOLUTE_SYNTAX_TOKEN_KEYWORD)
                return Fail(result, "expected 'input', 'output', 'uniform', or '}'");

            if (declaration != "input" && declaration != "output" && declaration != "uniform")
                return Fail(result, "expected 'input', 'output', 'uniform', or '}' in shader block");

            Consume(parser, token->kind, nullptr);
            ShaderVar var{};
            if (!ParseVarDecl(parser, result, var))
                return 0;
            if (declaration == "input") node->inputs.push_back(std::move(var));
            else if (declaration == "output") node->outputs.push_back(std::move(var));
            else node->uniforms.push_back(std::move(var));
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
            std::to_string(shader.outputs.size()) + ", uniforms=" +
            std::to_string(shader.uniforms.size()) + ")";
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

    bool HasUniform(const ShaderNode& shader, const std::string& name) {
        for (const ShaderVar& u : shader.uniforms) {
            if (u.name == name) return true;
        }
        return false;
    }

    const ShaderVar* FindInput(const ShaderNode& shader, const std::string& name) {
        for (const ShaderVar& in : shader.inputs) {
            if (in.name == name) return &in;
        }
        return nullptr;
    }

    std::string EscapeLlvmString(const std::string& s) {
        std::string out;
        out.reserve(s.size() * 2);
        for (unsigned char c : s) {
            if (c == '\\') out += "\\\\";
            else if (c == '"') out += "\\22";
            else if (c == '\n') out += "\\0A";
            else if (c == '\r') out += "\\0D";
            else if (c == '\t') out += "\\09";
            else if (c < 32 || c >= 127) {
                char buf[5];
                std::snprintf(buf, sizeof(buf), "\\%02X", c);
                out += buf;
            } else out.push_back(static_cast<char>(c));
        }
        return out;
    }

    std::string GenerateGlsl(const ShaderNode& shader) {
        std::ostringstream g;
        g << "#version 330 core\n";
        const bool isVertex = shader.stage == "Vertex" || shader.stage == "vertex" || shader.stage == "VS";
        const bool isFragment = shader.stage == "Fragment" || shader.stage == "fragment" ||
            shader.stage == "FS" || shader.stage == "Pixel";

        if (isVertex) {
            for (const ShaderVar& in : shader.inputs) {
                g << "layout(location=" << in.location << ") in " << GlslType(in) << " " << in.name << ";\n";
            }
            for (const ShaderVar& u : shader.uniforms) {
                g << "uniform " << GlslType(u) << " " << u.name << ";\n";
            }
            for (const ShaderVar& out : shader.outputs) {
                if (out.name == "clipPosition" || out.name == "gl_Position") continue;
                g << "out " << GlslType(out) << " " << out.name << ";\n";
            }
            g << "void main() {\n";
            // Default body: optional Y-axis rotation via uTime, simple projection.
            const ShaderVar* pos = FindInput(shader, "position");
            if (!pos) pos = FindInput(shader, "aPos");
            if (!pos && !shader.inputs.empty()) pos = &shader.inputs[0];

            if (pos && pos->components >= 3) {
                const bool hasTime = HasUniform(shader, "uTime");
                const bool hasScale = HasUniform(shader, "uScale");
                g << "  vec3 p = " << pos->name << ";\n";
                if (hasScale) g << "  p *= uScale;\n";
                if (hasTime) {
                    g << "  float c = cos(uTime); float s = sin(uTime);\n";
                    g << "  float px = p.x * c + p.z * s; float pz = -p.x * s + p.z * c;\n";
                    g << "  p = vec3(px, p.y, pz);\n";
                }
                g << "  float z = p.z + 2.8;\n";
                g << "  gl_Position = vec4(p.x / z * 1.7, p.y / z * 1.7, p.z * 0.05, 1.0);\n";

                const ShaderVar* nrm = FindInput(shader, "normal");
                if (!nrm) nrm = FindInput(shader, "aNormal");
                for (const ShaderVar& out : shader.outputs) {
                    if (out.name == "clipPosition" || out.name == "gl_Position") continue;
                    if ((out.name == "vNormal" || out.name.find("Normal") != std::string::npos) && nrm) {
                        if (hasTime) {
                            g << "  float nx = " << nrm->name << ".x * c + " << nrm->name << ".z * s;\n";
                            g << "  float nz = -" << nrm->name << ".x * s + " << nrm->name << ".z * c;\n";
                            g << "  " << out.name << " = normalize(vec3(nx, " << nrm->name << ".y, nz));\n";
                        } else {
                            g << "  " << out.name << " = normalize(" << nrm->name << ");\n";
                        }
                    } else if (out.name == "vUv" || out.name.find("uv") != std::string::npos ||
                               out.name.find("Uv") != std::string::npos) {
                        const ShaderVar* uv = FindInput(shader, "uv");
                        if (!uv) uv = FindInput(shader, "aUv");
                        if (!uv) uv = FindInput(shader, "texcoord");
                        if (uv) g << "  " << out.name << " = " << uv->name << ";\n";
                        else g << "  " << out.name << " = vec2(0.0);\n";
                    } else if (out.components == 3 && nrm) {
                        g << "  " << out.name << " = " << nrm->name << ";\n";
                    } else if (out.components == 2) {
                        g << "  " << out.name << " = vec2(0.0);\n";
                    } else if (out.components == 4) {
                        g << "  " << out.name << " = vec4(1.0);\n";
                    } else {
                        g << "  " << out.name << " = " << (out.components == 1 ? "0.0" : "vec3(0.0)") << ";\n";
                    }
                }
            } else {
                g << "  gl_Position = vec4(0.0, 0.0, 0.0, 1.0);\n";
            }
            g << "}\n";
            return g.str();
        }

        if (isFragment) {
            for (const ShaderVar& in : shader.inputs) {
                // Fragment "inputs" are varyings from VS (as `in`).
                g << "in " << GlslType(in) << " " << in.name << ";\n";
            }
            for (const ShaderVar& u : shader.uniforms) {
                g << "uniform " << GlslType(u) << " " << u.name << ";\n";
            }
            bool hasFragColor = false;
            for (const ShaderVar& out : shader.outputs) {
                if (out.name == "FragColor" || out.name == "color" || out.name == "outColor") {
                    g << "out vec4 FragColor;\n";
                    hasFragColor = true;
                } else {
                    g << "out " << GlslType(out) << " " << out.name << ";\n";
                }
            }
            if (!hasFragColor && !shader.outputs.empty()) {
                // First float4 output is color target.
            }
            g << "void main() {\n";
            const ShaderVar* nrm = nullptr;
            const ShaderVar* uv = nullptr;
            for (const ShaderVar& in : shader.inputs) {
                if (in.name.find("ormal") != std::string::npos) nrm = &in;
                if (in.name.find("v") == 0 && in.name.find("v") != std::string::npos &&
                    (in.name.find("uv") != std::string::npos || in.name.find("Uv") != std::string::npos))
                    uv = &in;
                if (in.name == "vUv" || in.name == "uv") uv = &in;
                if (in.name == "vNormal" || in.name == "normal") nrm = &in;
            }
            g << "  vec3 L = normalize(vec3(0.35, 0.85, 0.4));\n";
            if (nrm) {
                g << "  float ndl = max(dot(normalize(" << nrm->name << "), L), 0.0);\n";
            } else {
                g << "  float ndl = 0.7;\n";
            }
            g << "  float amb = 0.18;\n";
            if (uv) {
                g << "  vec3 base = mix(vec3(0.35, 0.55, 0.95), vec3(0.95, 0.75, 0.35), "
                  << uv->name << ".x * 0.35 + " << uv->name << ".y * 0.25);\n";
            } else {
                g << "  vec3 base = vec3(0.55, 0.65, 0.9);\n";
            }
            g << "  vec3 col = base * (amb + ndl * 0.9);\n";
            g << "  FragColor = vec4(col, 1.0);\n";
            g << "}\n";
            return g.str();
        }

        // Generic compute/unknown stage: empty main.
        for (const ShaderVar& u : shader.uniforms) {
            g << "uniform " << GlslType(u) << " " << u.name << ";\n";
        }
        g << "void main() {}\n";
        return g.str();
    }

    std::string MangleStage(const std::string& stage) {
        // Keep alnum only for symbol safety.
        std::string out;
        for (char c : stage) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') out.push_back(c);
        }
        return out.empty() ? "Stage" : out;
    }

    int32_t EmitShaderLlvm(void* payload, const AbsoluteOpaqueLlvmContextV1* context,
        const char** moduleIr, const char** errorMessage) {
        auto& shader = *static_cast<ShaderNode*>(payload);
        if (!context || context->abi_version != ABSOLUTE_SYNTAX_PLUGIN_ABI_VERSION) {
            lastError = "unsupported LLVM emission context";
            if (errorMessage) *errorMessage = lastError.c_str();
            return 0;
        }

        shader.glslSource = GenerateGlsl(shader);
        const std::string stage = MangleStage(shader.stage);
        const std::string escapedGlsl = EscapeLlvmString(shader.glslSource);
        const size_t glslBytes = shader.glslSource.size() + 1;

        std::ostringstream ir;
        ir << "; ModuleID = 'absolute.shader." << stage << "'\n";
        ir << "source_filename = \"absolute.shader." << stage << "\"\n";

        // Legacy reflection counts (tests + tooling).
        ir << "@absolute.shader." << stage << ".input_count = constant i32 "
           << shader.inputs.size() << "\n";
        ir << "@absolute.shader." << stage << ".output_count = constant i32 "
           << shader.outputs.size() << "\n";
        ir << "@absolute.shader." << stage << ".uniform_count = constant i32 "
           << shader.uniforms.size() << "\n";
        ir << "@absolute.shader." << stage << ".metadata_count = constant i32 "
           << context->attribute_count << "\n";

        // Per-input location + components for RHI layout bind.
        for (size_t i = 0; i < shader.inputs.size(); ++i) {
            const ShaderVar& in = shader.inputs[i];
            ir << "@absolute.shader." << stage << ".input." << i << ".location = constant i32 "
               << in.location << "\n";
            ir << "@absolute.shader." << stage << ".input." << i << ".components = constant i32 "
               << in.components << "\n";
            const std::string en = EscapeLlvmString(in.name);
            ir << "@absolute.shader." << stage << ".input." << i << ".name = private constant ["
               << (in.name.size() + 1) << " x i8] c\"" << en << "\\00\"\n";
        }

        // GLSL source for Desktop.Gpu.createShader.
        ir << "@absolute.shader." << stage << ".glsl = private constant [" << glslBytes
           << " x i8] c\"" << escapedGlsl << "\\00\"\n";

        // Stub entry (legacy).
        ir << "define void @absolute.shader." << stage << "() {\n";
        ir << "entry:\n  ret void\n}\n";

        // C ABI getters for Absolute RHI bind.
        ir << "define i8* @absolute_shader_glsl_" << stage << "() {\n";
        ir << "entry:\n";
        ir << "  %p = getelementptr inbounds [" << glslBytes << " x i8], [" << glslBytes
           << " x i8]* @absolute.shader." << stage << ".glsl, i64 0, i64 0\n";
        ir << "  ret i8* %p\n}\n";

        ir << "define i32 @absolute_shader_input_count_" << stage << "() {\n";
        ir << "entry:\n  ret i32 " << shader.inputs.size() << "\n}\n";

        ir << "define i32 @absolute_shader_uniform_count_" << stage << "() {\n";
        ir << "entry:\n  ret i32 " << shader.uniforms.size() << "\n}\n";

        ir << "define i32 @absolute_shader_input_location_" << stage << "(i32 %index) {\n";
        ir << "entry:\n";
        for (size_t i = 0; i < shader.inputs.size(); ++i) {
            ir << "  %c" << i << " = icmp eq i32 %index, " << i << "\n";
            ir << "  br i1 %c" << i << ", label %l" << i << ", label %n" << i << "\n";
            ir << "l" << i << ":\n  ret i32 " << shader.inputs[i].location << "\n";
            ir << "n" << i << ":\n";
        }
        ir << "  ret i32 -1\n}\n";

        ir << "define i32 @absolute_shader_input_components_" << stage << "(i32 %index) {\n";
        ir << "entry:\n";
        for (size_t i = 0; i < shader.inputs.size(); ++i) {
            ir << "  %c" << i << " = icmp eq i32 %index, " << i << "\n";
            ir << "  br i1 %c" << i << ", label %l" << i << ", label %n" << i << "\n";
            ir << "l" << i << ":\n  ret i32 " << shader.inputs[i].components << "\n";
            ir << "n" << i << ":\n";
        }
        ir << "  ret i32 0\n}\n";

        // Stride in bytes for sequential tightly-packed float attributes.
        int32_t strideFloats = 0;
        for (const ShaderVar& in : shader.inputs) strideFloats += in.components;
        ir << "define i32 @absolute_shader_vertex_stride_floats_" << stage << "() {\n";
        ir << "entry:\n  ret i32 " << strideFloats << "\n}\n";

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
