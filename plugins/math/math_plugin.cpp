#include "plugin_api.h"

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
        {"mat3", &ExpandTypeAlias, const_cast<char*>("Math.__mat3")},
        {"mat4", &ExpandTypeAlias, const_cast<char*>("Math.__mat4")}
    };

    const AbsoluteSyntaxPluginV1 plugin = {
        ABSOLUTE_SYNTAX_PLUGIN_ABI_VERSION,
        "absolute.math",
        4,
        typeAliases
    };

    constexpr const char* prelude = R"ABSOLUTE(
namespace Math {
    int32 abs(int32 value) {
        if (value < 0) { return -value; }
        return value;
    }

    double abs(double value) {
        if (value < 0.0) { return -value; }
        return value;
    }

    double sqrt(double value) {
        if (value <= 0.0) { return 0.0; }
        double estimate = value > 1.0 ? value : 1.0;
        int32 iteration = 0;
        while (iteration < 16) {
            estimate = (estimate + value / estimate) * 0.5;
            iteration += 1;
        }
        return estimate;
    }

    double wrapRadians(double value) {
        double pi = 3.141592653589793;
        double twoPi = 6.283185307179586;
        while (value > pi) { value -= twoPi; }
        while (value < -pi) { value += twoPi; }
        return value;
    }

    double sin(double value) {
        double x = wrapRadians(value);
        double x2 = x * x;
        double x4 = x2 * x2;
        double x6 = x4 * x2;
        double x8 = x4 * x4;
        double x10 = x8 * x2;
        double x12 = x10 * x2;
        double x14 = x12 * x2;
        return x * (1.0 - x2 / 6.0 + x4 / 120.0 - x6 / 5040.0 + x8 / 362880.0
            - x10 / 39916800.0 + x12 / 6227020800.0 - x14 / 1307674368000.0);
    }

    double cos(double value) {
        double x = wrapRadians(value);
        double x2 = x * x;
        double x4 = x2 * x2;
        double x6 = x4 * x2;
        double x8 = x4 * x4;
        double x10 = x8 * x2;
        double x12 = x10 * x2;
        double x14 = x12 * x2;
        return 1.0 - x2 / 2.0 + x4 / 24.0 - x6 / 720.0 + x8 / 40320.0
            - x10 / 3628800.0 + x12 / 479001600.0 - x14 / 87178291200.0;
    }

    class __vec2 {
        public double x;
        public double y;

        public __vec2(double initialX, double initialY) {
            x = initialX;
            y = initialY;
        }
    }

    class __vec3 {
        public double x;
        public double y;
        public double z;

        public __vec3(double initialX, double initialY, double initialZ) {
            x = initialX;
            y = initialY;
            z = initialZ;
        }
    }

    class __mat3 {
        public double m00; public double m01; public double m02;
        public double m10; public double m11; public double m12;
        public double m20; public double m21; public double m22;
    }

    class __mat4 {
        public double m00; public double m01; public double m02; public double m03;
        public double m10; public double m11; public double m12; public double m13;
        public double m20; public double m21; public double m22; public double m23;
        public double m30; public double m31; public double m32; public double m33;
    }

    void vec2Set(__vec2* output, double x, double y) {
        output.x = x; output.y = y;
    }

    void vec2Add(__vec2* output, __vec2* left, __vec2* right) {
        output.x = left.x + right.x;
        output.y = left.y + right.y;
    }

    double vec2Dot(__vec2* left, __vec2* right) {
        return left.x * right.x + left.y * right.y;
    }

    double vec2Length(__vec2* value) {
        return sqrt(vec2Dot(value, value));
    }

    void vec3Set(__vec3* output, double x, double y, double z) {
        output.x = x; output.y = y; output.z = z;
    }

    void vec3Add(__vec3* output, __vec3* left, __vec3* right) {
        output.x = left.x + right.x;
        output.y = left.y + right.y;
        output.z = left.z + right.z;
    }

    void vec3Subtract(__vec3* output, __vec3* left, __vec3* right) {
        output.x = left.x - right.x;
        output.y = left.y - right.y;
        output.z = left.z - right.z;
    }

    double vec3Dot(__vec3* left, __vec3* right) {
        return left.x * right.x + left.y * right.y + left.z * right.z;
    }

    double vec3Length(__vec3* value) {
        return sqrt(vec3Dot(value, value));
    }

    void vec3Normalize(__vec3* output, __vec3* value) {
        double length = vec3Length(value);
        if (length == 0.0) {
            vec3Set(output, 0.0, 0.0, 0.0);
        }
        else {
            output.x = value.x / length;
            output.y = value.y / length;
            output.z = value.z / length;
        }
    }

    void vec3Cross(__vec3* output, __vec3* left, __vec3* right) {
        double x = left.y * right.z - left.z * right.y;
        double y = left.z * right.x - left.x * right.z;
        double z = left.x * right.y - left.y * right.x;
        vec3Set(output, x, y, z);
    }

    __vec2* __vec2AddOwned(__vec2* left, __vec2* right) {
        return new __vec2(left.x + right.x, left.y + right.y);
    }

    __vec2* __vec2SubtractOwned(__vec2* left, __vec2* right) {
        return new __vec2(left.x - right.x, left.y - right.y);
    }

    __vec2* __vec2ScaleOwned(__vec2* value, double scalar) {
        return new __vec2(value.x * scalar, value.y * scalar);
    }

    __vec2* __vec2ScaleReverseOwned(double scalar, __vec2* value) {
        return new __vec2(value.x * scalar, value.y * scalar);
    }

    __vec3* __vec3AddOwned(__vec3* left, __vec3* right) {
        return new __vec3(left.x + right.x, left.y + right.y, left.z + right.z);
    }

    __vec3* __vec3SubtractOwned(__vec3* left, __vec3* right) {
        return new __vec3(left.x - right.x, left.y - right.y, left.z - right.z);
    }

    __vec3* __vec3ScaleOwned(__vec3* value, double scalar) {
        return new __vec3(value.x * scalar, value.y * scalar, value.z * scalar);
    }

    __vec3* __vec3ScaleReverseOwned(double scalar, __vec3* value) {
        return new __vec3(value.x * scalar, value.y * scalar, value.z * scalar);
    }

    void mat3Identity(__mat3* output) {
        output.m00 = 1.0; output.m01 = 0.0; output.m02 = 0.0;
        output.m10 = 0.0; output.m11 = 1.0; output.m12 = 0.0;
        output.m20 = 0.0; output.m21 = 0.0; output.m22 = 1.0;
    }

    double mat3Determinant(__mat3* value) {
        return value.m00 * (value.m11 * value.m22 - value.m12 * value.m21)
            - value.m01 * (value.m10 * value.m22 - value.m12 * value.m20)
            + value.m02 * (value.m10 * value.m21 - value.m11 * value.m20);
    }

    __mat3* __mat3MultiplyOwned(__mat3* left, __mat3* right) {
        auto output = new __mat3();
        output.m00 = left.m00 * right.m00 + left.m01 * right.m10 + left.m02 * right.m20;
        output.m01 = left.m00 * right.m01 + left.m01 * right.m11 + left.m02 * right.m21;
        output.m02 = left.m00 * right.m02 + left.m01 * right.m12 + left.m02 * right.m22;
        output.m10 = left.m10 * right.m00 + left.m11 * right.m10 + left.m12 * right.m20;
        output.m11 = left.m10 * right.m01 + left.m11 * right.m11 + left.m12 * right.m21;
        output.m12 = left.m10 * right.m02 + left.m11 * right.m12 + left.m12 * right.m22;
        output.m20 = left.m20 * right.m00 + left.m21 * right.m10 + left.m22 * right.m20;
        output.m21 = left.m20 * right.m01 + left.m21 * right.m11 + left.m22 * right.m21;
        output.m22 = left.m20 * right.m02 + left.m21 * right.m12 + left.m22 * right.m22;
        return output;
    }

    void mat4Identity(__mat4* output) {
        output.m00 = 1.0; output.m01 = 0.0; output.m02 = 0.0; output.m03 = 0.0;
        output.m10 = 0.0; output.m11 = 1.0; output.m12 = 0.0; output.m13 = 0.0;
        output.m20 = 0.0; output.m21 = 0.0; output.m22 = 1.0; output.m23 = 0.0;
        output.m30 = 0.0; output.m31 = 0.0; output.m32 = 0.0; output.m33 = 1.0;
    }

    void mat4Multiply(__mat4* output, __mat4* left, __mat4* right) {
        double m00 = left.m00 * right.m00 + left.m01 * right.m10 + left.m02 * right.m20 + left.m03 * right.m30;
        double m01 = left.m00 * right.m01 + left.m01 * right.m11 + left.m02 * right.m21 + left.m03 * right.m31;
        double m02 = left.m00 * right.m02 + left.m01 * right.m12 + left.m02 * right.m22 + left.m03 * right.m32;
        double m03 = left.m00 * right.m03 + left.m01 * right.m13 + left.m02 * right.m23 + left.m03 * right.m33;
        double m10 = left.m10 * right.m00 + left.m11 * right.m10 + left.m12 * right.m20 + left.m13 * right.m30;
        double m11 = left.m10 * right.m01 + left.m11 * right.m11 + left.m12 * right.m21 + left.m13 * right.m31;
        double m12 = left.m10 * right.m02 + left.m11 * right.m12 + left.m12 * right.m22 + left.m13 * right.m32;
        double m13 = left.m10 * right.m03 + left.m11 * right.m13 + left.m12 * right.m23 + left.m13 * right.m33;
        double m20 = left.m20 * right.m00 + left.m21 * right.m10 + left.m22 * right.m20 + left.m23 * right.m30;
        double m21 = left.m20 * right.m01 + left.m21 * right.m11 + left.m22 * right.m21 + left.m23 * right.m31;
        double m22 = left.m20 * right.m02 + left.m21 * right.m12 + left.m22 * right.m22 + left.m23 * right.m32;
        double m23 = left.m20 * right.m03 + left.m21 * right.m13 + left.m22 * right.m23 + left.m23 * right.m33;
        double m30 = left.m30 * right.m00 + left.m31 * right.m10 + left.m32 * right.m20 + left.m33 * right.m30;
        double m31 = left.m30 * right.m01 + left.m31 * right.m11 + left.m32 * right.m21 + left.m33 * right.m31;
        double m32 = left.m30 * right.m02 + left.m31 * right.m12 + left.m32 * right.m22 + left.m33 * right.m32;
        double m33 = left.m30 * right.m03 + left.m31 * right.m13 + left.m32 * right.m23 + left.m33 * right.m33;
        output.m00 = m00; output.m01 = m01; output.m02 = m02; output.m03 = m03;
        output.m10 = m10; output.m11 = m11; output.m12 = m12; output.m13 = m13;
        output.m20 = m20; output.m21 = m21; output.m22 = m22; output.m23 = m23;
        output.m30 = m30; output.m31 = m31; output.m32 = m32; output.m33 = m33;
    }

    __mat4* __mat4MultiplyOwned(__mat4* left, __mat4* right) {
        auto output = new __mat4();
        mat4Multiply(output, left, right);
        return output;
    }

    void projection(__mat4* output, double fovYRadians, double aspect, double nearPlane, double farPlane) {
        double half = fovYRadians * 0.5;
        double scale = cos(half) / sin(half);
        output.m00 = scale / aspect; output.m01 = 0.0; output.m02 = 0.0; output.m03 = 0.0;
        output.m10 = 0.0; output.m11 = scale; output.m12 = 0.0; output.m13 = 0.0;
        output.m20 = 0.0; output.m21 = 0.0; output.m22 = (farPlane + nearPlane) / (nearPlane - farPlane);
        output.m23 = (2.0 * farPlane * nearPlane) / (nearPlane - farPlane);
        output.m30 = 0.0; output.m31 = 0.0; output.m32 = -1.0; output.m33 = 0.0;
    }

    void lookAt(__mat4* output, __vec3* eye, __vec3* center, __vec3* up) {
        auto forwardValue = new __vec3(0.0, 0.0, 0.0);
        auto forward = new __vec3(0.0, 0.0, 0.0);
        auto sideValue = new __vec3(0.0, 0.0, 0.0);
        auto side = new __vec3(0.0, 0.0, 0.0);
        auto adjustedUp = new __vec3(0.0, 0.0, 0.0);
        vec3Subtract(forwardValue, center, eye);
        vec3Normalize(forward, forwardValue);
        vec3Cross(sideValue, forward, up);
        vec3Normalize(side, sideValue);
        vec3Cross(adjustedUp, side, forward);
        output.m00 = side.x; output.m01 = side.y; output.m02 = side.z; output.m03 = -vec3Dot(side, eye);
        output.m10 = adjustedUp.x; output.m11 = adjustedUp.y; output.m12 = adjustedUp.z; output.m13 = -vec3Dot(adjustedUp, eye);
        output.m20 = -forward.x; output.m21 = -forward.y; output.m22 = -forward.z; output.m23 = vec3Dot(forward, eye);
        output.m30 = 0.0; output.m31 = 0.0; output.m32 = 0.0; output.m33 = 1.0;
    }
}

extension double length(Math.__vec2* value) {
    return Math.vec2Length(value);
}

extension double length(Math.__vec3* value) {
    return Math.vec3Length(value);
}
)ABSOLUTE";

    const AbsoluteBinaryOperatorRuleV1 binaryOperatorRules[] = {
        {"Math.__vec2*", "+", "Math.__vec2*", "Math.__vec2AddOwned", "Math.__vec2*"},
        {"Math.__vec2*", "-", "Math.__vec2*", "Math.__vec2SubtractOwned", "Math.__vec2*"},
        {"Math.__vec2*", "*", "double", "Math.__vec2ScaleOwned", "Math.__vec2*"},
        {"double", "*", "Math.__vec2*", "Math.__vec2ScaleReverseOwned", "Math.__vec2*"},
        {"Math.__vec3*", "+", "Math.__vec3*", "Math.__vec3AddOwned", "Math.__vec3*"},
        {"Math.__vec3*", "-", "Math.__vec3*", "Math.__vec3SubtractOwned", "Math.__vec3*"},
        {"Math.__vec3*", "*", "double", "Math.__vec3ScaleOwned", "Math.__vec3*"},
        {"double", "*", "Math.__vec3*", "Math.__vec3ScaleReverseOwned", "Math.__vec3*"},
        {"Math.__mat3*", "*", "Math.__mat3*", "Math.__mat3MultiplyOwned", "Math.__mat3*"},
        {"Math.__mat4*", "*", "Math.__mat4*", "Math.__mat4MultiplyOwned", "Math.__mat4*"}
    };

    const AbsoluteBinaryOperatorTableV1 binaryOperators = {
        sizeof(binaryOperatorRules) / sizeof(binaryOperatorRules[0]),
        binaryOperatorRules
    };
}

extern "C" ABSOLUTE_PLUGIN_EXPORT const AbsoluteSyntaxPluginV1* absolute_syntax_plugin_init_v1() {
    return &plugin;
}

extern "C" ABSOLUTE_PLUGIN_EXPORT const char* absolute_syntax_plugin_prelude_v1() {
    return prelude;
}

extern "C" ABSOLUTE_PLUGIN_EXPORT const AbsoluteBinaryOperatorTableV1*
absolute_syntax_plugin_binary_operators_v1() {
    return &binaryOperators;
}
