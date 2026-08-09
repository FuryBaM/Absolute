#include "plugin_api.h"

#include <cstdio>
#include <string>

namespace {
    int32_t ExpandTypeAlias(void* userData, const AbsoluteSyntaxTokenV1*, size_t tokenCount,
        AbsoluteSyntaxExpansionV1* expansion) {
        if (tokenCount == 0) return 0;
        expansion->consumed_tokens = 1;
        expansion->replacement_source = static_cast<const char*>(userData);
        expansion->error_message = nullptr;
        return 1;
    }

    const AbsoluteSyntaxRuleV1 typeAliases[] = {
        {"vec2", &ExpandTypeAlias, const_cast<char*>("Math.__vec2")},
        {"vec3", &ExpandTypeAlias, const_cast<char*>("Math.__vec3")},
        {"vec4", &ExpandTypeAlias, const_cast<char*>("Math.__vec4")},
        {"mat3", &ExpandTypeAlias, const_cast<char*>("Math.__mat3")},
        {"mat4", &ExpandTypeAlias, const_cast<char*>("Math.__mat4")}
    };

    const AbsoluteSyntaxPluginV1 syntaxPlugin = {
        ABSOLUTE_SYNTAX_PLUGIN_ABI_VERSION,
        "absolute.math",
        sizeof(typeAliases) / sizeof(typeAliases[0]),
        typeAliases
    };

    const AbsoluteBinaryOperatorRuleV1 binaryOperatorRules[] = {
        {"Math.__vec2*", "+", "Math.__vec2*", "Math.__vec2AddOwned", "Math.__vec2*"},
        {"Math.__vec2*", "-", "Math.__vec2*", "Math.__vec2SubtractOwned", "Math.__vec2*"},
        {"Math.__vec2*", "*", "double", "Math.__vec2ScaleOwned", "Math.__vec2*"},
        {"double", "*", "Math.__vec2*", "Math.__vec2ScaleReverseOwned", "Math.__vec2*"},
        {"Math.__vec2*", "/", "double", "Math.__vec2DivideOwned", "Math.__vec2*"},
        {"Math.__vec3*", "+", "Math.__vec3*", "Math.__vec3AddOwned", "Math.__vec3*"},
        {"Math.__vec3*", "-", "Math.__vec3*", "Math.__vec3SubtractOwned", "Math.__vec3*"},
        {"Math.__vec3*", "*", "double", "Math.__vec3ScaleOwned", "Math.__vec3*"},
        {"double", "*", "Math.__vec3*", "Math.__vec3ScaleReverseOwned", "Math.__vec3*"},
        {"Math.__vec3*", "/", "double", "Math.__vec3DivideOwned", "Math.__vec3*"},
        {"Math.__vec4*", "+", "Math.__vec4*", "Math.__vec4AddOwned", "Math.__vec4*"},
        {"Math.__vec4*", "-", "Math.__vec4*", "Math.__vec4SubtractOwned", "Math.__vec4*"},
        {"Math.__vec4*", "*", "double", "Math.__vec4ScaleOwned", "Math.__vec4*"},
        {"double", "*", "Math.__vec4*", "Math.__vec4ScaleReverseOwned", "Math.__vec4*"},
        {"Math.__vec4*", "/", "double", "Math.__vec4DivideOwned", "Math.__vec4*"},
        {"Math.__mat3*", "*", "Math.__mat3*", "Math.__mat3MultiplyOwned", "Math.__mat3*"},
        {"Math.__mat4*", "*", "Math.__mat4*", "Math.__mat4MultiplyOwned", "Math.__mat4*"},
        {"Math.__mat4*", "*", "Math.__vec4*", "Math.__mat4TransformOwned", "Math.__vec4*"}
    };

    const AbsoluteBinaryOperatorTableV1 binaryOperators = {
        sizeof(binaryOperatorRules) / sizeof(binaryOperatorRules[0]),
        binaryOperatorRules
    };

    const AbsoluteVirtualModuleV1 virtualModules[] = {
        {"MathVirtualModule",
            "namespace MathVirtualModule { int32 vmodule_add(int32 a, int32 b) { return a + b; } }\n"}
    };

    const AbsoluteVirtualModuleTableV1 virtualModuleTable = {
        sizeof(virtualModules) / sizeof(virtualModules[0]),
        virtualModules
    };

    const AbsoluteCompilerPluginV1 compilerPlugin = {
        sizeof(AbsoluteCompilerPluginV1),
        ABSOLUTE_SYNTAX_PLUGIN_ABI_VERSION,
        1,
        ABSOLUTE_CAPABILITY_PARSER | ABSOLUTE_CAPABILITY_SEMANTIC |
            ABSOLUTE_CAPABILITY_BACKEND | ABSOLUTE_CAPABILITY_IDE,
        "absolute.math",
        "2.0.0",
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr,
        {0, 0, 0}
    };

    const char* GetCompletionsJson(const char*, uint32_t, uint32_t) {
        return R"JSON([
{"label":"vec2","detail":"2D vector (double CPU precision, float packing available)"},
{"label":"vec3","detail":"3D vector (double CPU precision, float packing available)"},
{"label":"vec4","detail":"4D vector / RGBA color"},
{"label":"mat3","detail":"row-major 3x3 matrix"},
{"label":"mat4","detail":"row-major 4x4 matrix"},
{"label":"Math.clamp","detail":"double Math.clamp(double value, double min, double max)"},
{"label":"Math.lerp","detail":"double Math.lerp(double from, double to, double t)"},
{"label":"Math.radians","detail":"double Math.radians(double degrees)"},
{"label":"Math.perspectiveRH","detail":"mat4* Math.perspectiveRH(double fovY, double aspect, double near, double far)"},
{"label":"Math.lookAtRH","detail":"mat4* Math.lookAtRH(vec3* eye, vec3* target, vec3* up)"}
])JSON";
    }

    const char* GetDefinitionJson(const char*, uint32_t, uint32_t) {
        return R"({"file":"absolute-math.prelude.abs","line":1,"column":1})";
    }

    const char* GetHoverInfo(const char* symbolName) {
        if (!symbolName) return "absolute.math 2.0 — portable scalar, vector and matrix math";
        const std::string name(symbolName);
        if (name == "vec2" || name == "Math.__vec2")
            return "vec2 { double x, y } — arithmetic, dot/length/normalize, float[] packing";
        if (name == "vec3" || name == "Math.__vec3")
            return "vec3 { double x, y, z } — arithmetic, dot/cross/normalize, float[] packing";
        if (name == "vec4" || name == "Math.__vec4")
            return "vec4 { double x, y, z, w } — vector or RGBA color, float[] packing";
        if (name == "mat3" || name == "Math.__mat3")
            return "mat3 — row-major 3x3 matrix; toFloatArray() packs it for GPU buffers";
        if (name == "mat4" || name == "Math.__mat4")
            return "mat4 — row-major 4x4 matrix with row/column-major float packing";
        if (name == "Math.perspectiveRH")
            return "mat4* Math.perspectiveRH(fovYRadians, aspect, near, far) — depth range -1..1";
        if (name == "Math.perspectiveRHZeroToOne")
            return "mat4* Math.perspectiveRHZeroToOne(...) — D3D/Vulkan depth range 0..1";
        return "absolute.math symbol — see absolute-math.prelude.abs";
    }

    const char* GetEmbeddedLanguage(const char* blockTag) {
        if (!blockTag) return nullptr;
        const std::string tag(blockTag);
        if (tag == "glsl") return "glsl";
        if (tag == "hlsl") return "hlsl";
        if (tag == "msl") return "metal";
        return nullptr;
    }

    const char* RenderRuntimeValue(const char* typeName, const void* valuePtr) {
        if (!typeName || !valuePtr) return "null";
        const std::string name(typeName);
        const double* values = static_cast<const double*>(valuePtr);
        static thread_local char buffer[256];
        if (name == "Math.__vec2" || name == "vec2") {
            std::snprintf(buffer, sizeof(buffer), "vec2(x=%.4g, y=%.4g)", values[0], values[1]);
            return buffer;
        }
        if (name == "Math.__vec3" || name == "vec3") {
            std::snprintf(buffer, sizeof(buffer), "vec3(x=%.4g, y=%.4g, z=%.4g)",
                values[0], values[1], values[2]);
            return buffer;
        }
        if (name == "Math.__vec4" || name == "vec4") {
            std::snprintf(buffer, sizeof(buffer), "vec4(x=%.4g, y=%.4g, z=%.4g, w=%.4g)",
                values[0], values[1], values[2], values[3]);
            return buffer;
        }
        return "<MathValue>";
    }

    const char* GetDebugInfoJson(const char* symbolName) {
        static thread_local std::string json;
        json = std::string("{\"type\":\"math\",\"symbol\":\"") +
            (symbolName ? symbolName : "") + "\",\"portable\":true,\"version\":\"2.0.0\"}";
        return json.c_str();
    }

    const AbsoluteIdeCapabilitiesV1 ideCapabilities = {
        sizeof(AbsoluteIdeCapabilitiesV1),
        GetCompletionsJson,
        GetDefinitionJson,
        nullptr, nullptr, nullptr, nullptr, nullptr,
        GetEmbeddedLanguage,
        nullptr, nullptr, nullptr, nullptr
    };

    const AbsoluteDebuggerHooksV1 debuggerHooks = {
        sizeof(AbsoluteDebuggerHooksV1),
        RenderRuntimeValue,
        GetDebugInfoJson,
        nullptr, nullptr, nullptr
    };

    const AbsoluteEditorPluginV1 editorPlugin = {
        sizeof(AbsoluteEditorPluginV1),
        ABSOLUTE_SYNTAX_PLUGIN_ABI_VERSION,
        nullptr, nullptr,
        GetHoverInfo,
        &ideCapabilities,
        &debuggerHooks,
        {0, 0}
    };
}

extern "C" ABSOLUTE_PLUGIN_EXPORT const AbsoluteSyntaxPluginV1*
absolute_syntax_plugin_init_v1() {
    return &syntaxPlugin;
}

extern "C" ABSOLUTE_PLUGIN_EXPORT const AbsoluteBinaryOperatorTableV1*
absolute_syntax_plugin_binary_operators_v1() {
    return &binaryOperators;
}

extern "C" ABSOLUTE_PLUGIN_EXPORT const AbsoluteCompilerPluginV1*
absolute_compiler_plugin_v1() {
    return &compilerPlugin;
}

extern "C" ABSOLUTE_PLUGIN_EXPORT const AbsoluteVirtualModuleTableV1*
absolute_plugin_virtual_modules_v1() {
    return &virtualModuleTable;
}

extern "C" ABSOLUTE_PLUGIN_EXPORT const AbsoluteEditorPluginV1*
absolute_editor_plugin_v1() {
    return &editorPlugin;
}
