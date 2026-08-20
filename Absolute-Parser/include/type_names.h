#pragma once

#include <string>
#include <string_view>

namespace Absolute {
    // The lifetime relation a handle value has to what it points at.
    //
    // It belongs to the type rather than to the variable. Two handles reaching
    // the same object with different relations are two different things, and a
    // container that stores one must not quietly store the other: that is the
    // hole an array of `T*` fell into -- it could not tell whether it held the
    // owner or a copy of it, so it could neither release the elements nor leave
    // them alone without being wrong in the other case.
    //
    // This is the one definition. The analyzer and the backend each had their
    // own set of prefix tests, and a qualifier one of them did not know about
    // was silently dropped rather than refused.
    enum class OwnershipKind {
        None,    // not a handle at all
        Unique,  // T*         the one owner; releasing it destroys the object
        Sub,     // sub T*     borrowed, bound to an owner's lifetime
        Weak,    // weak T*    observer that outlives the object and says so
        Raw      // raw T*     unmanaged; the explicit unsafe escape
    };

    inline std::string_view CanonicalOwnershipPrefix(OwnershipKind kind) {
        switch (kind) {
        case OwnershipKind::Raw: return "raw ";
        case OwnershipKind::Weak: return "weak ";
        case OwnershipKind::Sub: return "sub ";
        case OwnershipKind::Unique:
        case OwnershipKind::None: break;
        }
        return "";
    }

    inline OwnershipKind CanonicalOwnership(const std::string& type) {
        if (type.empty() || !type.ends_with("*")) return OwnershipKind::None;
        if (type.starts_with("raw ")) return OwnershipKind::Raw;
        if (type.starts_with("weak ")) return OwnershipKind::Weak;
        if (type.starts_with("sub ")) return OwnershipKind::Sub;
        return OwnershipKind::Unique;
    }

    inline std::string CanonicalPointeeName(const std::string& type) {
        const OwnershipKind kind = CanonicalOwnership(type);
        if (kind == OwnershipKind::None) return type;
        std::string pointee = type.substr(CanonicalOwnershipPrefix(kind).size());
        pointee.pop_back();
        return pointee;
    }

    inline std::string CanonicalPointerName(
        const std::string& pointee, OwnershipKind kind) {
        return std::string(CanonicalOwnershipPrefix(kind)) + pointee + "*";
    }

    // A qualifier written in front of a name that is not a pointer yet --
    // `sub T`, where `T` is a generic parameter. It is not a type on its own:
    // it is an instruction about whatever `T` turns out to be, carried until
    // there is something to apply it to.
    inline OwnershipKind CanonicalOpenOwnership(const std::string& type) {
        if (type.empty() || type.ends_with("*")) return OwnershipKind::None;
        if (type.starts_with("raw ")) return OwnershipKind::Raw;
        if (type.starts_with("weak ")) return OwnershipKind::Weak;
        if (type.starts_with("sub ")) return OwnershipKind::Sub;
        return OwnershipKind::None;
    }

    inline std::string CanonicalOpenBaseName(const std::string& type) {
        const OwnershipKind kind = CanonicalOpenOwnership(type);
        if (kind == OwnershipKind::None) return type;
        return type.substr(CanonicalOwnershipPrefix(kind).size());
    }

    // Applying a qualifier to a complete type. On a handle it replaces the
    // kind, which is the whole point -- `sub T` at `T = Node*` is `sub Node*`.
    // On anything else it is nothing: a qualifier says what a value's relation
    // to an object's lifetime is, and a value with no object has no relation
    // to weaken.
    inline std::string CanonicalWithOwnership(
        const std::string& type, OwnershipKind kind) {
        if (type.ends_with("[]"))
            return CanonicalWithOwnership(
                type.substr(0, type.size() - 2), kind) + "[]";
        if (CanonicalOwnership(type) == OwnershipKind::None) return type;
        return CanonicalPointerName(CanonicalPointeeName(type), kind);
    }

    // What copying, moving and destroying a handle of each kind mean. Pure: it
    // depends on the kind and nothing else, which is what lets an aggregate or
    // an array work out its own answer by asking its parts.
    struct HandleSemantics {
        bool copyable = true;
        bool movable = true;
        bool needsDrop = false;
    };

    // How a value of some type is released. The kind is worked out once, from
    // the type, and everything that releases anything obeys it -- instead of
    // each site asking the type's shape again and arriving at its own answer.
    enum class DropKind {
        None,
        Closure,         // release the closure environment
        ManagedOwner,    // destroy what it points at, then the handle
        ArrayStorage,    // free the buffer
        StringStorage,   // one name fewer holds the bytes
        TupleValue,      // walk the elements; a tuple has no destructor to call
        ClassObject,     // run the class destructor
        StructObject,    // run the struct destructor
        PluginResource   // hand it back to the plugin that made it
    };

    // What copying, moving and destroying a value of a type mean.
    //
    // A container asks this and nothing else. It must not ask "is this a
    // managed pointer?", because `T*`, `sub T*` and `weak T*` all are, and
    // destroying them means three different things.
    struct TypeSemantics {
        bool copyable = true;
        bool movable = true;
        bool needsDrop = false;
        DropKind dropKind = DropKind::None;
    };

    inline HandleSemantics CanonicalHandleSemantics(OwnershipKind kind) {
        switch (kind) {
        // Copying an owner would make two of them; that is what `move` is for.
        case OwnershipKind::Unique: return {false, true, true};
        // Neither of these releases anything: a subscriber's owner does, and a
        // weak observer holds a generation rather than the object.
        case OwnershipKind::Sub:
        case OwnershipKind::Weak:
        case OwnershipKind::Raw:
        case OwnershipKind::None: break;
        }
        return {true, true, false};
    }

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
