#pragma once

#include <string>

namespace Absolute {
    inline bool IsCanonicalValueReferenceType(const std::string& type) {
        return !type.empty() && type.ends_with("&");
    }

    inline bool IsCanonicalConstValueReferenceType(const std::string& type) {
        return IsCanonicalValueReferenceType(type) && type.starts_with("const ");
    }

    inline std::string CanonicalValueReferenceBaseType(const std::string& type) {
        std::string base = type;
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

}
