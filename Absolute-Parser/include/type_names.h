#pragma once

#include <string>

namespace Absolute {
    inline bool IsCanonicalConsumeParameterType(const std::string& type) {
        return type.starts_with("consume ");
    }

    inline std::string CanonicalConsumeParameterBaseType(const std::string& type) {
        return IsCanonicalConsumeParameterType(type) ? type.substr(8) : type;
    }

    inline bool IsCanonicalValueReferenceType(const std::string& type) {
        return !type.empty() && type.ends_with("&");
    }

    inline bool IsCanonicalConstValueReferenceType(const std::string& type) {
        return IsCanonicalValueReferenceType(type) && type.starts_with("const ");
    }

    inline std::string CanonicalValueReferenceBaseType(const std::string& type) {
        std::string base = CanonicalConsumeParameterBaseType(type);
        if (!IsCanonicalValueReferenceType(base)) return base;
        base.resize(base.size() - 1);
        if (base.starts_with("const ")) base.erase(0, 6);
        return base;
    }

    inline std::string CanonicalValueReferenceType(
        const std::string& type, bool isConst, bool isReference) {
        if (!isReference) return type;
        return std::string(isConst ? "const " : "") + type + "&";
    }

    inline std::string CanonicalConsumeParameterType(
        const std::string& type, bool isConsume) {
        return isConsume ? "consume " + CanonicalValueReferenceBaseType(type) : type;
    }
}
