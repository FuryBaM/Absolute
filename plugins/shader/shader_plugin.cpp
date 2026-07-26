// absolute.shader — opaque DSL with typed I/O, uniforms, reflection metadata,
// and multi-target GPU IR:
//   GLSL 330 (OpenGL), HLSL (D3D/Vulkan), MSL (Metal source),
//   optional SPIR-V / DXIL binaries via portable DXC when available.
// Metal AIR (ABSOLUTE_ARTIFACT_METAL_IR) is reserved; MSL source is always emitted.

#include "plugin_api.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

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
        std::vector<ShaderVar> storages;
        int32_t workgroupX = 1;
        int32_t workgroupY = 1;
        int32_t workgroupZ = 1;
        std::string userGlsl; // optional full GLSL from `code { ... }`
        std::string userHlsl; // optional compute body from `hlsl { ... }`
        std::string debugText;
        std::string moduleIr;
        std::string glslSource;
        std::string hlslSource;
        std::string mslSource;
        std::vector<uint8_t> spirvBytes;
        std::vector<uint8_t> dxilBytes;
        // Metal IR (AIR) — empty unless an external metallib tool is wired later.
        std::vector<uint8_t> metalIrBytes;
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

    const char* HlslType(const ShaderVar& var) {
        switch (var.kind) {
        case ShaderTypeKind::Float1: return "float";
        case ShaderTypeKind::Float2: return "float2";
        case ShaderTypeKind::Float3: return "float3";
        case ShaderTypeKind::Float4: return "float4";
        case ShaderTypeKind::Int1: return "int";
        default: return "float3";
        }
    }

    // Metal Shading Language scalar/vector names match HLSL-style floatN.
    const char* MslType(const ShaderVar& var) { return HlslType(var); }

    bool IsVertexStage(const std::string& stage) {
        return stage == "Vertex" || stage == "vertex" || stage == "VS" || stage == "vs";
    }

    bool IsFragmentStage(const std::string& stage) {
        return stage == "Fragment" || stage == "fragment" || stage == "FS" || stage == "fs"
            || stage == "Pixel" || stage == "pixel" || stage == "PS" || stage == "ps";
    }

    bool IsComputeStage(const std::string& stage) {
        return stage == "Compute" || stage == "compute" || stage == "CS" || stage == "cs";
    }

    // D3D/Vulkan HLSL: location 0 → POSITION; location n → TEXCOORDn-1.
    void HlslSemanticForLocation(int32_t location, std::string& semantic) {
        if (location <= 0) {
            semantic = "POSITION";
            return;
        }
        semantic = "TEXCOORD" + std::to_string(location - 1);
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

    bool ParseRawBlock(
        AbsoluteParserCursorV1* parser,
        AbsoluteOpaqueParseResultV1* result,
        std::string& output,
        const char* declaration) {
        if (!Consume(parser, ABSOLUTE_SYNTAX_TOKEN_BRACKET, "{"))
            return Fail(result, std::string("expected '{' after ") + declaration);
        int depth = 1;
        std::ostringstream body;
        bool needSpace = false;
        while (depth > 0) {
            const AbsoluteSyntaxTokenV1* token = parser->peek(parser->context, 0);
            if (!token)
                return Fail(result, std::string("unterminated ") + declaration + " block");
            const std::string text = TokenText(token);
            if (token->kind == ABSOLUTE_SYNTAX_TOKEN_BRACKET && text == "{") {
                ++depth;
                if (needSpace) body << ' ';
                body << '{';
                needSpace = false;
                Consume(parser, ABSOLUTE_SYNTAX_TOKEN_BRACKET, nullptr);
                continue;
            }
            if (token->kind == ABSOLUTE_SYNTAX_TOKEN_BRACKET && text == "}") {
                --depth;
                if (depth == 0) {
                    Consume(parser, ABSOLUTE_SYNTAX_TOKEN_BRACKET, nullptr);
                    break;
                }
                body << '}';
                needSpace = true;
                Consume(parser, ABSOLUTE_SYNTAX_TOKEN_BRACKET, nullptr);
                continue;
            }
            if (token->kind == ABSOLUTE_SYNTAX_TOKEN_STRING) {
                if (needSpace) body << ' ';
                body << '"' << text << '"';
                needSpace = true;
            }
            else if (token->kind == ABSOLUTE_SYNTAX_TOKEN_DELIMITER &&
                (text == ";" || text == "," || text == ".")) {
                body << text;
                needSpace = text == ";" || text == ",";
            }
            else if (token->kind == ABSOLUTE_SYNTAX_TOKEN_OPERATOR ||
                token->kind == ABSOLUTE_SYNTAX_TOKEN_BRACKET) {
                if (text == "(" || text == "[" || text == "<") {
                    body << text;
                    needSpace = false;
                }
                else if (text == ")" || text == "]" || text == ">") {
                    body << text;
                    needSpace = true;
                }
                else {
                    if (needSpace) body << ' ';
                    body << text;
                    needSpace = true;
                }
            }
            else {
                if (needSpace) body << ' ';
                body << text;
                needSpace = true;
            }
            parser->consume(parser->context, 0xFFFFFFFFu, nullptr);
        }
        output = body.str();
        return true;
    }

    bool ParseWorkgroup(
        AbsoluteParserCursorV1* parser,
        AbsoluteOpaqueParseResultV1* result,
        ShaderNode& node) {
        int32_t values[3] = {};
        for (int index = 0; index < 3; ++index) {
            const AbsoluteSyntaxTokenV1* token = parser->peek(parser->context, 0);
            if (!token || token->kind != ABSOLUTE_SYNTAX_TOKEN_NUMBER)
                return Fail(result, "workgroup requires three positive integer sizes");
            const std::string text = TokenText(token);
            if (text.empty() || text.find('.') != std::string::npos)
                return Fail(result, "workgroup sizes must be integers");
            char* end = nullptr;
            const long parsed = std::strtol(text.c_str(), &end, 10);
            if (!end || *end != '\0' || parsed > INT32_MAX) {
                return Fail(result, "invalid workgroup size");
            }
            values[index] = static_cast<int32_t>(parsed);
            if (values[index] <= 0)
                return Fail(result, "workgroup sizes must be positive");
            Consume(parser, ABSOLUTE_SYNTAX_TOKEN_NUMBER, nullptr);
            if (index < 2 && !Consume(parser, ABSOLUTE_SYNTAX_TOKEN_DELIMITER, ","))
                return Fail(result, "workgroup sizes must be separated by commas");
        }
        if (!Consume(parser, ABSOLUTE_SYNTAX_TOKEN_DELIMITER, ";"))
            return Fail(result, "workgroup declaration requires ';'");
        node.workgroupX = values[0];
        node.workgroupY = values[1];
        node.workgroupZ = values[2];
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
                return Fail(result, "expected a shader declaration or '}'");

            // `code {}` is a complete GLSL source. For compute, `hlsl {}`
            // contains the body of main() and gets storage/workgroup declarations.
            if (declaration == "code" || declaration == "hlsl") {
                Consume(parser, token->kind, nullptr);
                std::string body;
                if (!ParseRawBlock(parser, result, body, declaration.c_str())) return 0;
                if (declaration == "code") node->userGlsl = std::move(body);
                else node->userHlsl = std::move(body);
                continue;
            }

            if (declaration == "workgroup") {
                Consume(parser, token->kind, nullptr);
                if (!ParseWorkgroup(parser, result, *node)) return 0;
                continue;
            }

            if (declaration != "input" && declaration != "output" &&
                declaration != "uniform" && declaration != "storage")
                return Fail(result, "expected 'input', 'output', 'uniform', 'storage', "
                    "'workgroup', 'code', 'hlsl', or '}' in shader block");

            Consume(parser, token->kind, nullptr);
            ShaderVar var{};
            if (!ParseVarDecl(parser, result, var))
                return 0;
            if (declaration == "input") node->inputs.push_back(std::move(var));
            else if (declaration == "output") node->outputs.push_back(std::move(var));
            else if (declaration == "uniform") node->uniforms.push_back(std::move(var));
            else {
                var.location = static_cast<int32_t>(node->storages.size());
                node->storages.push_back(std::move(var));
            }
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
            std::to_string(shader.uniforms.size()) + ", storages=" +
            std::to_string(shader.storages.size()) +
            ((!shader.userGlsl.empty() || !shader.userHlsl.empty())
                ? ", codegen=custom" : ", codegen=auto") + ")";
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
        else if (!IsComputeStage(shader.stage) && shader.outputs.empty()) {
            lastError = "shader requires at least one output";
        }
        else if (IsComputeStage(shader.stage) && shader.storages.empty()) {
            lastError = "compute shader requires at least one storage declaration";
        }
        else if (IsComputeStage(shader.stage) && shader.storages.size() > 8) {
            lastError = "desktop compute supports at most 8 storage buffers (u0..u7)";
        }
        else if (IsComputeStage(shader.stage) && !shader.uniforms.empty()) {
            lastError = "desktop compute uniforms are not implemented yet; use storage buffers";
        }
        else if (IsComputeStage(shader.stage) &&
            (shader.workgroupX > 1024 || shader.workgroupY > 1024 ||
                shader.workgroupZ > 64 ||
                static_cast<int64_t>(shader.workgroupX) * shader.workgroupY *
                    shader.workgroupZ > 1024)) {
            lastError = "compute workgroup exceeds D3D11 limits "
                "(x/y <= 1024, z <= 64, total <= 1024)";
        }
        else if (IsComputeStage(shader.stage) &&
            std::any_of(shader.storages.begin(), shader.storages.end(),
                [](const ShaderVar& storage) {
                    return storage.kind != ShaderTypeKind::Float1;
                })) {
            lastError = "desktop compute storage currently supports only 'storage float name;'";
        }
        else if (!IsComputeStage(shader.stage) && !shader.userHlsl.empty()) {
            lastError = "hlsl { ... } bodies are currently supported only for compute shaders";
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
        const bool isCompute = IsComputeStage(shader.stage);
        // Custom body from `code { ... }` is treated as complete stage source.
        if (!shader.userGlsl.empty()) {
            std::string g = shader.userGlsl;
            // Trim leading whitespace for version check.
            std::size_t i = 0;
            while (i < g.size() && (g[i] == ' ' || g[i] == '\n' || g[i] == '\r' || g[i] == '\t')) ++i;
            if (g.compare(i, 8, "#version") != 0) {
                g = std::string(isCompute ? "#version 430 core\n" : "#version 330 core\n") + g;
            }
            if (g.empty() || g.back() != '\n') g.push_back('\n');
            return g;
        }

        std::ostringstream g;
        g << (isCompute ? "#version 430 core\n" : "#version 330 core\n");
        const bool isVertex = IsVertexStage(shader.stage);
        const bool isFragment = IsFragmentStage(shader.stage);

        if (isCompute) {
            g << "layout(local_size_x=" << shader.workgroupX
              << ", local_size_y=" << shader.workgroupY
              << ", local_size_z=" << shader.workgroupZ << ") in;\n";
            for (const ShaderVar& storage : shader.storages) {
                g << "layout(std430, binding=" << storage.location << ") buffer AbsoluteStorage"
                  << storage.location << " { float " << storage.name << "[]; };\n";
            }
            g << "void main() {}\n";
            return g.str();
        }

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

        // Unknown stage: empty main.
        for (const ShaderVar& u : shader.uniforms) {
            g << "uniform " << GlslType(u) << " " << u.name << ";\n";
        }
        g << "void main() {}\n";
        return g.str();
    }

    std::string GenerateHlsl(const ShaderNode& shader) {
        if (IsComputeStage(shader.stage)) {
            std::ostringstream h;
            for (const ShaderVar& storage : shader.storages) {
                h << "RWStructuredBuffer<float> " << storage.name
                  << " : register(u" << storage.location << ");\n";
            }
            h << "[numthreads(" << shader.workgroupX << ", " << shader.workgroupY
              << ", " << shader.workgroupZ << ")]\n";
            h << "void main(uint3 id : SV_DispatchThreadID) {\n";
            if (!shader.userHlsl.empty()) h << "  " << shader.userHlsl << "\n";
            h << "}\n";
            return h.str();
        }

        // `code { }` is GLSL-oriented; for HLSL emit a note stub so D3D paths still link.
        if (!shader.userGlsl.empty()) {
            return std::string(
                "// absolute.shader: custom code { } is GLSL; HLSL auto-gen skipped\n"
                "float4 main() : SV_TARGET { return float4(1,0,1,1); }\n");
        }

        std::ostringstream h;
        const bool isVertex = IsVertexStage(shader.stage);
        const bool isFragment = IsFragmentStage(shader.stage);

        if (!shader.uniforms.empty()) {
            h << "cbuffer Frame : register(b0) {\n";
            for (const ShaderVar& u : shader.uniforms) {
                h << "  " << HlslType(u) << " " << u.name << ";\n";
            }
            h << "};\n";
        }

        if (isVertex) {
            h << "struct VSIn {\n";
            for (const ShaderVar& in : shader.inputs) {
                std::string sem;
                HlslSemanticForLocation(in.location, sem);
                h << "  " << HlslType(in) << " " << in.name << " : " << sem << ";\n";
            }
            h << "};\nstruct VSOut {\n";
            int tex = 0;
            for (const ShaderVar& out : shader.outputs) {
                if (out.name == "clipPosition" || out.name == "gl_Position") {
                    h << "  float4 " << out.name << " : SV_POSITION;\n";
                } else {
                    h << "  " << HlslType(out) << " " << out.name << " : TEXCOORD" << tex++ << ";\n";
                }
            }
            if (shader.outputs.empty()) {
                h << "  float4 pos : SV_POSITION;\n";
            }
            h << "};\nVSOut main(VSIn i) {\n  VSOut o;\n";
            const ShaderVar* pos = FindInput(shader, "position");
            if (!pos) pos = FindInput(shader, "aPos");
            if (!pos && !shader.inputs.empty()) pos = &shader.inputs[0];
            const bool hasTime = HasUniform(shader, "uTime");
            const bool hasScale = HasUniform(shader, "uScale");
            if (pos && pos->components >= 3) {
                h << "  float3 p = i." << pos->name << ";\n";
                if (hasScale) h << "  p *= uScale;\n";
                if (hasTime) {
                    h << "  float c = cos(uTime); float s = sin(uTime);\n";
                    h << "  float px = p.x * c + p.z * s; float pz = -p.x * s + p.z * c;\n";
                    h << "  p = float3(px, p.y, pz);\n";
                }
                h << "  float z = p.z + 2.8;\n";
                h << "  float4 clip = float4(p.x / z * 1.7, p.y / z * 1.7, p.z * 0.05, 1.0);\n";
            } else {
                h << "  float4 clip = float4(0,0,0,1);\n";
            }
            for (const ShaderVar& out : shader.outputs) {
                if (out.name == "clipPosition" || out.name == "gl_Position") {
                    h << "  o." << out.name << " = clip;\n";
                } else if (out.name == "vNormal" || out.name.find("Normal") != std::string::npos) {
                    const ShaderVar* nrm = FindInput(shader, "normal");
                    if (!nrm) nrm = FindInput(shader, "aNormal");
                    if (nrm) h << "  o." << out.name << " = i." << nrm->name << ";\n";
                    else h << "  o." << out.name << " = float3(0,1,0);\n";
                } else if (out.name == "vUv" || out.name.find("uv") != std::string::npos
                    || out.name.find("Uv") != std::string::npos) {
                    const ShaderVar* uv = FindInput(shader, "uv");
                    if (!uv) uv = FindInput(shader, "aUv");
                    if (uv) h << "  o." << out.name << " = i." << uv->name << ";\n";
                    else h << "  o." << out.name << " = float2(0,0);\n";
                } else if (out.components == 4) {
                    h << "  o." << out.name << " = float4(1,1,1,1);\n";
                } else if (out.components == 3) {
                    h << "  o." << out.name << " = float3(0,0,0);\n";
                } else if (out.components == 2) {
                    h << "  o." << out.name << " = float2(0,0);\n";
                } else {
                    h << "  o." << out.name << " = 0;\n";
                }
            }
            if (shader.outputs.empty()) h << "  o.pos = clip;\n";
            h << "  return o;\n}\n";
            return h.str();
        }

        if (isFragment) {
            h << "struct PSIn {\n  float4 pos : SV_POSITION;\n";
            int tex = 0;
            for (const ShaderVar& in : shader.inputs) {
                h << "  " << HlslType(in) << " " << in.name << " : TEXCOORD" << tex++ << ";\n";
            }
            h << "};\nfloat4 main(PSIn i) : SV_TARGET {\n";
            const ShaderVar* nrm = nullptr;
            const ShaderVar* uv = nullptr;
            for (const ShaderVar& in : shader.inputs) {
                if (in.name == "vNormal" || in.name.find("ormal") != std::string::npos) nrm = &in;
                if (in.name == "vUv" || in.name == "uv" || in.name.find("Uv") != std::string::npos) uv = &in;
            }
            h << "  float3 L = normalize(float3(0.35, 0.85, 0.4));\n";
            if (nrm) h << "  float ndl = max(dot(normalize(i." << nrm->name << "), L), 0.0);\n";
            else h << "  float ndl = 0.7;\n";
            h << "  float amb = 0.18;\n";
            if (uv) {
                h << "  float3 base = lerp(float3(0.35, 0.55, 0.95), float3(0.95, 0.75, 0.35), "
                  << "i." << uv->name << ".x * 0.35 + i." << uv->name << ".y * 0.25);\n";
            } else {
                h << "  float3 base = float3(0.55, 0.65, 0.9);\n";
            }
            h << "  float3 col = base * (amb + ndl * 0.9);\n";
            h << "  return float4(col, 1.0);\n}\n";
            return h.str();
        }

        h << "float4 main() : SV_TARGET { return float4(1,0,1,1); }\n";
        return h.str();
    }

    std::string GenerateMsl(const ShaderNode& shader) {
        if (IsComputeStage(shader.stage)) {
            std::ostringstream m;
            m << "#include <metal_stdlib>\nusing namespace metal;\n";
            m << "kernel void main(";
            for (size_t index = 0; index < shader.storages.size(); ++index) {
                if (index > 0) m << ", ";
                const ShaderVar& storage = shader.storages[index];
                m << "device float* " << storage.name << " [[buffer("
                  << storage.location << ")]]";
            }
            if (!shader.storages.empty()) m << ", ";
            m << "uint3 id [[thread_position_in_grid]]) {\n";
            m << "  // HLSL compute body is preserved in the HLSL artifact.\n";
            m << "}\n";
            return m.str();
        }

        if (!shader.userGlsl.empty()) {
            return std::string(
                "// absolute.shader: custom code { } is GLSL; MSL auto-gen skipped\n"
                "#include <metal_stdlib>\nusing namespace metal;\n"
                "fragment float4 main() { return float4(1,0,1,1); }\n");
        }
        std::ostringstream m;
        m << "#include <metal_stdlib>\nusing namespace metal;\n";
        const bool isVertex = IsVertexStage(shader.stage);
        const bool isFragment = IsFragmentStage(shader.stage);

        if (isVertex) {
            m << "struct VSIn {\n";
            for (const ShaderVar& in : shader.inputs) {
                m << "  " << MslType(in) << " " << in.name << " [[attribute(" << in.location << ")]];\n";
            }
            m << "};\nstruct VSOut {\n  float4 position [[position]];\n";
            for (const ShaderVar& out : shader.outputs) {
                if (out.name == "clipPosition" || out.name == "gl_Position") continue;
                m << "  " << MslType(out) << " " << out.name << ";\n";
            }
            m << "};\n";
            if (!shader.uniforms.empty()) {
                m << "struct Frame {\n";
                for (const ShaderVar& u : shader.uniforms) {
                    m << "  " << MslType(u) << " " << u.name << ";\n";
                }
                m << "};\n";
            }
            m << "vertex VSOut main(VSIn i [[stage_in]]";
            if (!shader.uniforms.empty()) m << ", constant Frame& frame [[buffer(0)]]";
            m << ") {\n  VSOut o;\n";
            const ShaderVar* pos = FindInput(shader, "position");
            if (!pos && !shader.inputs.empty()) pos = &shader.inputs[0];
            if (pos) {
                m << "  float3 p = i." << pos->name << ";\n";
                if (HasUniform(shader, "uScale")) m << "  p *= frame.uScale;\n";
                if (HasUniform(shader, "uTime")) {
                    m << "  float c = cos(frame.uTime); float s = sin(frame.uTime);\n";
                    m << "  float px = p.x * c + p.z * s; float pz = -p.x * s + p.z * c;\n";
                    m << "  p = float3(px, p.y, pz);\n";
                }
                m << "  float z = p.z + 2.8;\n";
                m << "  o.position = float4(p.x / z * 1.7, p.y / z * 1.7, p.z * 0.05, 1.0);\n";
            } else {
                m << "  o.position = float4(0,0,0,1);\n";
            }
            for (const ShaderVar& out : shader.outputs) {
                if (out.name == "clipPosition" || out.name == "gl_Position") continue;
                if (out.name.find("ormal") != std::string::npos) {
                    const ShaderVar* nrm = FindInput(shader, "normal");
                    if (nrm) m << "  o." << out.name << " = i." << nrm->name << ";\n";
                    else m << "  o." << out.name << " = float3(0,1,0);\n";
                } else if (out.name.find("uv") != std::string::npos || out.name.find("Uv") != std::string::npos) {
                    const ShaderVar* uv = FindInput(shader, "uv");
                    if (uv) m << "  o." << out.name << " = i." << uv->name << ";\n";
                    else m << "  o." << out.name << " = float2(0,0);\n";
                } else if (out.components == 3) {
                    m << "  o." << out.name << " = float3(0,0,0);\n";
                } else if (out.components == 2) {
                    m << "  o." << out.name << " = float2(0,0);\n";
                } else {
                    m << "  o." << out.name << " = 0;\n";
                }
            }
            m << "  return o;\n}\n";
            return m.str();
        }

        if (isFragment) {
            m << "struct FSIn {\n  float4 position [[position]];\n";
            for (const ShaderVar& in : shader.inputs) {
                m << "  " << MslType(in) << " " << in.name << ";\n";
            }
            m << "};\nfragment float4 main(FSIn i [[stage_in]]) {\n";
            m << "  float3 L = normalize(float3(0.35, 0.85, 0.4));\n";
            const ShaderVar* nrm = nullptr;
            const ShaderVar* uv = nullptr;
            for (const ShaderVar& in : shader.inputs) {
                if (in.name.find("ormal") != std::string::npos) nrm = &in;
                if (in.name.find("uv") != std::string::npos || in.name.find("Uv") != std::string::npos) uv = &in;
            }
            if (nrm) m << "  float ndl = max(dot(normalize(i." << nrm->name << "), L), 0.0);\n";
            else m << "  float ndl = 0.7;\n";
            m << "  float3 base = float3(0.55, 0.65, 0.9);\n";
            if (uv) {
                m << "  base = mix(float3(0.35, 0.55, 0.95), float3(0.95, 0.75, 0.35), "
                  << "i." << uv->name << ".x * 0.35 + i." << uv->name << ".y * 0.25);\n";
            }
            m << "  return float4(base * (0.18 + ndl * 0.9), 1.0);\n}\n";
            return m.str();
        }

        m << "fragment float4 main() { return float4(1,0,1,1); }\n";
        return m.str();
    }

    std::string MangleStage(const std::string& stage) {
        // Keep alnum only for symbol safety.
        std::string out;
        for (char c : stage) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') out.push_back(c);
        }
        return out.empty() ? "Stage" : out;
    }

    std::string FindDxcPath() {
        if (const char* env = std::getenv("ABSOLUTE_DXC")) {
#if defined(_WIN32)
            if (GetFileAttributesA(env) != INVALID_FILE_ATTRIBUTES) return env;
#else
            if (env[0]) return env;
#endif
        }
#if defined(_WIN32)
        char modulePath[MAX_PATH]{};
        HMODULE hm = nullptr;
        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&FindDxcPath),
            &hm);
        if (hm) GetModuleFileNameA(hm, modulePath, MAX_PATH);
        std::string dir = modulePath;
        for (int up = 0; up < 10; ++up) {
            const size_t slash = dir.find_last_of("\\/");
            if (slash == std::string::npos) break;
            dir = dir.substr(0, slash);
            const std::string dxc = dir + "\\.absolute\\toolchains\\dxc-spirv\\bin\\x64\\dxc.exe";
            if (GetFileAttributesA(dxc.c_str()) != INVALID_FILE_ATTRIBUTES) return dxc;
        }
#endif
        return {};
    }

    bool CompileHlslWithDxc(
        const std::string& hlsl,
        const char* profile,
        bool spirv,
        std::vector<uint8_t>& outBytes) {
        outBytes.clear();
        if (hlsl.empty()) return false;
        const std::string dxc = FindDxcPath();
        if (dxc.empty()) return false;

#if defined(_WIN32)
        char tempPath[MAX_PATH]{};
        GetTempPathA(MAX_PATH, tempPath);
        char hlslFile[MAX_PATH]{};
        GetTempFileNameA(tempPath, "ash", 0, hlslFile);
        const std::string outFile = std::string(hlslFile) + (spirv ? ".spv" : ".dxil");
        {
            std::ofstream f(hlslFile, std::ios::binary);
            f.write(hlsl.data(), static_cast<std::streamsize>(hlsl.size()));
        }
        std::string cmd = "\"";
        cmd += dxc;
        cmd += "\" -T ";
        cmd += profile;
        cmd += " -E main ";
        if (spirv) {
            cmd += "-spirv -fspv-target-env=vulkan1.1 -fvk-use-dx-layout "
                   "-fvk-b-shift 0 0 -fvk-t-shift 1 0 -fvk-s-shift 2 0 ";
        }
        cmd += "-Fo \"";
        cmd += outFile;
        cmd += "\" \"";
        cmd += hlslFile;
        cmd += "\"";

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::vector<char> cmdline(cmd.begin(), cmd.end());
        cmdline.push_back('\0');
        if (!CreateProcessA(
                nullptr, cmdline.data(), nullptr, nullptr, FALSE,
                CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            DeleteFileA(hlslFile);
            return false;
        }
        WaitForSingleObject(pi.hProcess, 60000);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        DeleteFileA(hlslFile);
        if (code != 0) {
            DeleteFileA(outFile.c_str());
            return false;
        }
        std::ifstream in(outFile, std::ios::binary);
        if (!in) {
            DeleteFileA(outFile.c_str());
            return false;
        }
        in.seekg(0, std::ios::end);
        const auto n = static_cast<size_t>(in.tellg());
        in.seekg(0, std::ios::beg);
        outBytes.resize(n);
        if (n) in.read(reinterpret_cast<char*>(outBytes.data()), static_cast<std::streamsize>(n));
        DeleteFileA(outFile.c_str());
        return !outBytes.empty();
#else
        (void)profile;
        (void)spirv;
        return false;
#endif
    }

    std::string EscapeLlvmBytes(const std::vector<uint8_t>& bytes) {
        std::string out;
        out.reserve(bytes.size() * 4);
        for (uint8_t c : bytes) {
            char buf[5];
            std::snprintf(buf, sizeof(buf), "\\%02X", static_cast<unsigned>(c));
            out += buf;
        }
        return out;
    }

    void EmitStringConstant(
        std::ostringstream& ir,
        const std::string& stage,
        const char* field,
        const std::string& text) {
        const size_t n = text.size() + 1;
        ir << "@absolute.shader." << stage << "." << field << " = private constant [" << n
           << " x i8] c\"" << EscapeLlvmString(text) << "\\00\"\n";
        ir << "define i8* @absolute_shader_" << field << "_" << stage << "() {\n";
        ir << "entry:\n  %p = getelementptr inbounds [" << n << " x i8], [" << n
           << " x i8]* @absolute.shader." << stage << "." << field << ", i64 0, i64 0\n";
        ir << "  ret i8* %p\n}\n";
        ir << "define i32 @absolute_shader_" << field << "_size_" << stage << "() {\n";
        ir << "entry:\n  ret i32 " << static_cast<int32_t>(text.size()) << "\n}\n";
    }

    void EmitBinaryConstant(
        std::ostringstream& ir,
        const std::string& stage,
        const char* field,
        const std::vector<uint8_t>& bytes) {
        // Always emit a least a 1-byte zero blob so symbols exist when IR is unavailable.
        std::vector<uint8_t> payload = bytes;
        if (payload.empty()) payload.push_back(0);
        const size_t n = payload.size();
        ir << "@absolute.shader." << stage << "." << field << " = private constant [" << n
           << " x i8] c\"" << EscapeLlvmBytes(payload) << "\"\n";
        ir << "define i8* @absolute_shader_" << field << "_" << stage << "() {\n";
        ir << "entry:\n  %p = getelementptr inbounds [" << n << " x i8], [" << n
           << " x i8]* @absolute.shader." << stage << "." << field << ", i64 0, i64 0\n";
        ir << "  ret i8* %p\n}\n";
        ir << "define i32 @absolute_shader_" << field << "_size_" << stage << "() {\n";
        ir << "entry:\n  ret i32 " << static_cast<int32_t>(bytes.size()) << "\n}\n";
        ir << "define i32 @absolute_shader_has_" << field << "_" << stage << "() {\n";
        ir << "entry:\n  ret i32 " << (bytes.empty() ? 0 : 1) << "\n}\n";
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
        shader.hlslSource = GenerateHlsl(shader);
        shader.mslSource = GenerateMsl(shader);

        const bool isVertex = IsVertexStage(shader.stage);
        const bool isFragment = IsFragmentStage(shader.stage);
        const bool isCompute = IsComputeStage(shader.stage);
        const char* profile = isVertex ? "vs_6_0"
            : (isFragment ? "ps_6_0" : (isCompute ? "cs_6_0" : "ps_6_0"));
        if (!shader.userGlsl.empty()) {
            // Custom GLSL body is not HLSL — skip DXC.
            shader.spirvBytes.clear();
            shader.dxilBytes.clear();
        } else {
            CompileHlslWithDxc(shader.hlslSource, profile, true, shader.spirvBytes);
            CompileHlslWithDxc(shader.hlslSource, profile, false, shader.dxilBytes);
        }
        shader.metalIrBytes.clear(); // Metal AIR requires metallib; MSL source is the portable form.

        const std::string stage = MangleStage(shader.stage);

        std::ostringstream ir;
        ir << "; ModuleID = 'absolute.shader." << stage << "'\n";
        ir << "source_filename = \"absolute.shader." << stage << "\"\n";
        ir << "; Multi-target GPU IR: GLSL/HLSL/MSL source + optional SPIR-V/DXIL (DXC)\n";

        // Legacy reflection counts (tests + tooling).
        ir << "@absolute.shader." << stage << ".input_count = constant i32 "
           << shader.inputs.size() << "\n";
        ir << "@absolute.shader." << stage << ".output_count = constant i32 "
           << shader.outputs.size() << "\n";
        ir << "@absolute.shader." << stage << ".uniform_count = constant i32 "
           << shader.uniforms.size() << "\n";
        ir << "@absolute.shader." << stage << ".storage_count = constant i32 "
           << shader.storages.size() << "\n";
        ir << "@absolute.shader." << stage << ".workgroup = constant [3 x i32] [i32 "
           << shader.workgroupX << ", i32 " << shader.workgroupY << ", i32 "
           << shader.workgroupZ << "]\n";
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
        for (size_t i = 0; i < shader.storages.size(); ++i) {
            const ShaderVar& storage = shader.storages[i];
            ir << "@absolute.shader." << stage << ".storage." << i
               << ".binding = constant i32 " << storage.location << "\n";
            const std::string escapedName = EscapeLlvmString(storage.name);
            ir << "@absolute.shader." << stage << ".storage." << i
               << ".name = private constant [" << (storage.name.size() + 1)
               << " x i8] c\"" << escapedName << "\\00\"\n";
        }

        // First-class source IR blobs (null-terminated strings).
        EmitStringConstant(ir, stage, "glsl", shader.glslSource);
        EmitStringConstant(ir, stage, "hlsl", shader.hlslSource);
        EmitStringConstant(ir, stage, "msl", shader.mslSource);

        if (shader.stage == "Compute") {
            ir << "define i8* @generatedComputeHlsl() {\n";
            ir << "entry:\n  %p = call i8* @absolute_shader_hlsl_" << stage << "()\n";
            ir << "  ret i8* %p\n}\n";
            ir << "define i8* @generatedComputeGlsl() {\n";
            ir << "entry:\n  %p = call i8* @absolute_shader_glsl_" << stage << "()\n";
            ir << "  ret i8* %p\n}\n";
        }

        // First-class binary IR blobs (SPIR-V, DXIL, Metal IR placeholder).
        EmitBinaryConstant(ir, stage, "spirv", shader.spirvBytes);
        EmitBinaryConstant(ir, stage, "dxil", shader.dxilBytes);
        EmitBinaryConstant(ir, stage, "metal_ir", shader.metalIrBytes);

        // Stub entry (legacy).
        ir << "define void @absolute.shader." << stage << "() {\n";
        ir << "entry:\n  ret void\n}\n";

        ir << "define i32 @absolute_shader_input_count_" << stage << "() {\n";
        ir << "entry:\n  ret i32 " << shader.inputs.size() << "\n}\n";

        ir << "define i32 @absolute_shader_uniform_count_" << stage << "() {\n";
        ir << "entry:\n  ret i32 " << shader.uniforms.size() << "\n}\n";

        ir << "define i32 @absolute_shader_storage_count_" << stage << "() {\n";
        ir << "entry:\n  ret i32 " << shader.storages.size() << "\n}\n";

        ir << "define i32 @absolute_shader_workgroup_x_" << stage << "() {\n";
        ir << "entry:\n  ret i32 " << shader.workgroupX << "\n}\n";
        ir << "define i32 @absolute_shader_workgroup_y_" << stage << "() {\n";
        ir << "entry:\n  ret i32 " << shader.workgroupY << "\n}\n";
        ir << "define i32 @absolute_shader_workgroup_z_" << stage << "() {\n";
        ir << "entry:\n  ret i32 " << shader.workgroupZ << "\n}\n";

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

        // Stride in floats for sequential tightly-packed float attributes.
        int32_t strideFloats = 0;
        for (const ShaderVar& in : shader.inputs) strideFloats += in.components;
        ir << "define i32 @absolute_shader_vertex_stride_floats_" << stage << "() {\n";
        ir << "entry:\n  ret i32 " << strideFloats << "\n}\n";

        // Artifact kind constants once per module (avoid multi-stage ODR clashes).
        static thread_local bool emittedArtifactKinds = false;
        if (!emittedArtifactKinds) {
            ir << "define i32 @absolute_shader_artifact_kind_spirv() {\nentry:\n  ret i32 "
               << static_cast<int>(ABSOLUTE_ARTIFACT_SPIRV) << "\n}\n";
            ir << "define i32 @absolute_shader_artifact_kind_dxil() {\nentry:\n  ret i32 "
               << static_cast<int>(ABSOLUTE_ARTIFACT_DXIL) << "\n}\n";
            ir << "define i32 @absolute_shader_artifact_kind_metal_ir() {\nentry:\n  ret i32 "
               << static_cast<int>(ABSOLUTE_ARTIFACT_METAL_IR) << "\n}\n";
            ir << "define i32 @absolute_shader_artifact_kind_source_text() {\nentry:\n  ret i32 "
               << static_cast<int>(ABSOLUTE_ARTIFACT_SOURCE_TEXT) << "\n}\n";
            emittedArtifactKinds = true;
        }

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

    constexpr const char* prelude = R"ABSOLUTE(
namespace Shader {
    // Available when the module contains the canonical `shader Compute` block.
    extern "C" string generatedComputeHlsl();
    extern "C" string generatedComputeGlsl();
}
)ABSOLUTE";
}

extern "C" ABSOLUTE_PLUGIN_EXPORT const AbsoluteSyntaxPluginV1* absolute_syntax_plugin_init_v1() {
    return &plugin;
}

extern "C" ABSOLUTE_PLUGIN_EXPORT const AbsoluteOpaqueSyntaxTableV1*
absolute_syntax_plugin_opaque_rules_v1() {
    return &opaqueTable;
}

extern "C" ABSOLUTE_PLUGIN_EXPORT const char* absolute_syntax_plugin_prelude_v1() {
    return prelude;
}
