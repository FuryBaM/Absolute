#pragma once
#include "analyzer_build_pch.h"
#include "analyzer_pch.h"
#include "expression_visitor.h"
#include "syntax_plugins.h"
#include "type_names.h"

namespace Absolute {
    inline bool IsValueReferenceType(const std::string& type) {
        return IsCanonicalValueReferenceType(type);
    }

    inline bool IsConstValueReferenceType(const std::string& type) {
        return IsCanonicalConstValueReferenceType(type);
    }

    inline std::string ValueReferenceBaseType(const std::string& type) {
        return CanonicalValueReferenceBaseType(type);
    }

    inline std::string ValueReferenceType(
        const std::string& type, bool isConst, bool isReference) {
        return CanonicalValueReferenceType(type, isConst, isReference);
    }

    namespace {
        template <typename T, typename Visitor>
        void AcceptIfPresent(const std::unique_ptr<T>& node, Visitor& visitor) {
            if (!node) return;
            if constexpr (requires {
                visitor.PushDiagnosticNode(
                    static_cast<const ASTNode*>(node.get()));
                visitor.PopDiagnosticNode();
            }) {
                visitor.PushDiagnosticNode(node.get());
                node->Accept(visitor);
                visitor.PopDiagnosticNode();
            }
            else {
                node->Accept(visitor);
            }
        }

        template <typename T, typename Visitor>
        void AcceptAll(const std::vector<std::unique_ptr<T>>& nodes, Visitor& visitor) {
            for (const auto& node : nodes) AcceptIfPresent(node, visitor);
        }

        inline bool IsConditionType(const std::string& type) {
            return type == "bool" || type == "dynamic" || type == "error" ||
                type.starts_with("int") || type.starts_with("uint") || type.ends_with("*");
        }

        // All of these read the one ownership answer rather than testing
        // prefixes again; the second set of prefix tests is where `shared` was
        // understood in one place and dropped in another.
        inline bool IsRawPointerType(const std::string& type) {
            return CanonicalOwnership(type) == OwnershipKind::Raw;
        }

        inline bool IsWeakPointerType(const std::string& type) {
            return CanonicalOwnership(type) == OwnershipKind::Weak;
        }

        inline bool IsSubscriberPointerType(const std::string& type) {
            return CanonicalOwnership(type) == OwnershipKind::Sub;
        }

        inline bool IsManagedPointerType(const std::string& type) {
            const OwnershipKind kind = CanonicalOwnership(type);
            return kind != OwnershipKind::None && kind != OwnershipKind::Raw;
        }

        // "Strong" here means it holds the object up. A subscriber and a weak
        // observer do not.
        inline bool IsStrongManagedPointerType(const std::string& type) {
            return CanonicalOwnership(type) == OwnershipKind::Unique;
        }

        inline bool IsPointerType(const std::string& type) {
            return CanonicalOwnership(type) != OwnershipKind::None;
        }

        inline std::string PointerPointee(std::string type) {
            return CanonicalPointeeName(type);
        }

        inline bool IsTaskType(const std::string& type) {
            return type.size() > 6 && type.starts_with("task<") && type.ends_with(">");
        }

        inline size_t ArrayRank(std::string type) {
            size_t rank = 0;
            while (type.ends_with("[]")) {
                type.resize(type.size() - 2);
                ++rank;
            }
            return rank;
        }

        inline std::string ArrayElementType(std::string type, size_t dimensions = 1) {
            while (dimensions-- > 0 && type.ends_with("[]")) type.resize(type.size() - 2);
            return type;
        }

        inline std::string ArrayType(std::string elementType, size_t rank) {
            while (rank-- > 0) elementType += "[]";
            return elementType;
        }

        inline std::optional<std::vector<size_t>> InferArrayShape(const ArrayExpr& array) {
            std::vector<size_t> childShape;
            bool hasChildShape = false;
            for (const auto& value : array.values) {
                std::vector<size_t> currentShape;
                if (const auto* nested = dynamic_cast<const ArrayExpr*>(value.get())) {
                    const auto nestedShape = InferArrayShape(*nested);
                    if (!nestedShape) return std::nullopt;
                    currentShape = *nestedShape;
                }
                if (!hasChildShape) {
                    childShape = std::move(currentShape);
                    hasChildShape = true;
                }
                else if (childShape != currentShape) return std::nullopt;
            }
            childShape.insert(childShape.begin(), array.values.size());
            return childShape;
        }

        inline std::optional<std::vector<size_t>> InferArrayStorageShape(
            const ArrayExpr& array, size_t declaredRank) {
            auto shape = InferArrayShape(array);
            if (!shape || declaredRank != 1 || shape->size() <= 1) return shape;
            size_t count = 1;
            for (size_t dimension : *shape) {
                if (dimension != 0 && count > std::numeric_limits<size_t>::max() / dimension)
                    return std::nullopt;
                count *= dimension;
            }
            return std::vector<size_t>{count};
        }

        inline std::string TaskValueType(const std::string& type) {
            return IsTaskType(type) ? type.substr(5, type.size() - 6) : "error";
        }

        inline bool ParseGenericTypeName(
            const std::string& type, std::string& base, std::vector<std::string>& arguments) {
            const size_t open = type.find('<');
            if (open == std::string::npos || type.empty() || type.back() != '>') return false;
            base = type.substr(0, open);
            arguments.clear();
            size_t depth = 0;
            size_t start = open + 1;
            for (size_t index = start; index + 1 < type.size(); ++index) {
                if (type[index] == '<') ++depth;
                else if (type[index] == '>') {
                    if (depth == 0) return false;
                    --depth;
                }
                else if (type[index] == ',' && depth == 0) {
                    arguments.push_back(type.substr(start, index - start));
                    start = index + 1;
                }
            }
            arguments.push_back(type.substr(start, type.size() - start - 1));
            return !base.empty() && std::none_of(arguments.begin(), arguments.end(),
                [](const std::string& argument) { return argument.empty(); });
        }

        inline std::string SubstituteGenericType(const std::string& type,
            const std::unordered_map<std::string, std::string>& substitutions) {
            if (IsValueReferenceType(type))
                return ValueReferenceType(
                    SubstituteGenericType(ValueReferenceBaseType(type), substitutions),
                    IsConstValueReferenceType(type), true);
            if (const auto found = substitutions.find(type); found != substitutions.end())
                return found->second;
            if (type.ends_with("[]"))
                return SubstituteGenericType(type.substr(0, type.size() - 2), substitutions) + "[]";
            // `sub T`: the qualifier was written with nothing to apply it to
            // yet. This is where there is something -- whatever `T` became.
            if (const OwnershipKind open = CanonicalOpenOwnership(type);
                open != OwnershipKind::None) {
                const std::string base = CanonicalOpenBaseName(type);
                const std::string substituted =
                    SubstituteGenericType(base, substitutions);
                // Unless `T` became `T`. Substituting a type variable for
                // itself leaves the qualifier with nothing to apply to, the
                // same as before, so it stays written down and waits.
                // CanonicalWithOwnership drops a qualifier on anything that is
                // not a handle -- right for `T = int32`, wrong for a variable
                // that is still a variable -- and dropping it here is how
                // `sub T` became `T` while a generic body was being checked.
                // `T` does not take a `sub T`, because the arrow runs one way,
                // so a method of a generic class could not be called with what
                // another method of the same class handed back.
                if (substituted == base)
                    return std::string(CanonicalOwnershipPrefix(open)) + substituted;
                return CanonicalWithOwnership(substituted, open);
            }
            // The qualifier survives substitution; see the same fix in
            // SubstituteCodegenType.
            if (const OwnershipKind kind = CanonicalOwnership(type);
                kind != OwnershipKind::None) {
                return CanonicalPointerName(
                    SubstituteGenericType(CanonicalPointeeName(type), substitutions),
                    kind);
            }
            std::string base;
            std::vector<std::string> arguments;
            if (!ParseGenericTypeName(type, base, arguments)) return type;
            std::string result = base + "<";
            for (size_t index = 0; index < arguments.size(); ++index) {
                if (index) result += ",";
                result += SubstituteGenericType(arguments[index], substitutions);
            }
            return result + ">";
        }

        inline bool ParseFunctionType(const std::string& type,
            std::string& returnType, std::vector<std::string>& parameterTypes) {
            std::string base;
            std::vector<std::string> arguments;
            if (!ParseGenericTypeName(type, base, arguments) || base != "func" ||
                arguments.empty()) return false;
            returnType = arguments.front();
            parameterTypes.assign(arguments.begin() + 1, arguments.end());
            return true;
        }

        inline std::string FunctionTypeName(const std::string& returnType,
            const std::vector<std::string>& parameterTypes) {
            std::string result = "func<" + returnType;
            for (const std::string& parameter : parameterTypes) result += "," + parameter;
            return result + ">";
        }

        // C ABI function pointer: cfunc<Return, Param0, Param1, ...> (raw ptr, no captures).
        inline bool ParseCFunctionType(const std::string& type,
            std::string& returnType, std::vector<std::string>& parameterTypes) {
            std::string base;
            std::vector<std::string> arguments;
            if (!ParseGenericTypeName(type, base, arguments) || base != "cfunc" ||
                arguments.empty()) return false;
            returnType = arguments.front();
            parameterTypes.assign(arguments.begin() + 1, arguments.end());
            return true;
        }

        inline std::string CFunctionTypeName(const std::string& returnType,
            const std::vector<std::string>& parameterTypes) {
            std::string result = "cfunc<" + returnType;
            for (const std::string& parameter : parameterTypes) result += "," + parameter;
            return result + ">";
        }

        // Whether a type variable appears anywhere inside a pattern -- as the
        // pattern itself, an element type, a pointee, or a type argument.
        inline bool MentionsGenericParameter(const std::string& pattern,
            const std::unordered_set<std::string>& parameters) {
            if (parameters.contains(pattern)) return true;
            if (pattern.ends_with("[]"))
                return MentionsGenericParameter(
                    pattern.substr(0, pattern.size() - 2), parameters);
            if (IsPointerType(pattern))
                return MentionsGenericParameter(PointerPointee(pattern), parameters);
            std::string base;
            std::vector<std::string> arguments;
            if (ParseGenericTypeName(pattern, base, arguments)) {
                for (const std::string& argument : arguments)
                    if (MentionsGenericParameter(argument, parameters)) return true;
            }
            return false;
        }

        inline bool UnifyGenericType(const std::string& pattern, const std::string& actual,
            const std::unordered_set<std::string>& parameters,
            std::unordered_map<std::string, std::string>& substitutions) {
            // A pattern with no type variable in it binds nothing, so there is
            // nothing here to decide: whether the argument fits the parameter is
            // a conversion question, and answering it with an equality test is
            // what refused `Box<int64>.put(7)`. A method of a generic class
            // carries the class's type parameter in its symbol but has its own
            // parameters already substituted, so the pattern was the concrete
            // `int64`, unification demanded exactly `int64`, and an `int32` that
            // any ordinary call widens was rejected as a failed unification
            // rather than considered as a conversion.
            if (!MentionsGenericParameter(pattern, parameters)) return true;
            if (parameters.contains(pattern)) {
                const auto found = substitutions.find(pattern);
                if (found == substitutions.end()) {
                    substitutions.emplace(pattern, actual);
                    return true;
                }
                return found->second == actual;
            }
            if (pattern.ends_with("[]") && actual.ends_with("[]"))
                return UnifyGenericType(pattern.substr(0, pattern.size() - 2),
                    actual.substr(0, actual.size() - 2), parameters, substitutions);
            // Two handles unify only when they are the same kind of handle.
            // Comparing raw-ness and weak-ness separately let a `shared T*`
            // unify with a plain `T*`, which is the qualifier going missing by
            // another route.
            if (const OwnershipKind patternKind = CanonicalOwnership(pattern);
                patternKind != OwnershipKind::None &&
                patternKind == CanonicalOwnership(actual))
                return UnifyGenericType(CanonicalPointeeName(pattern),
                    CanonicalPointeeName(actual), parameters, substitutions);
            std::string patternBase;
            std::string actualBase;
            std::vector<std::string> patternArguments;
            std::vector<std::string> actualArguments;
            if (ParseGenericTypeName(pattern, patternBase, patternArguments) &&
                ParseGenericTypeName(actual, actualBase, actualArguments)) {
                if (patternBase != actualBase || patternArguments.size() != actualArguments.size())
                    return false;
                for (size_t index = 0; index < patternArguments.size(); ++index)
                    if (!UnifyGenericType(patternArguments[index], actualArguments[index],
                        parameters, substitutions)) return false;
                return true;
            }
            return pattern == actual;
        }

        inline bool HasModifier(const Statement& statement, const std::string& name) {
            return std::any_of(statement.modifiers.begin(), statement.modifiers.end(),
                [&](const Token& modifier) { return modifier.value == name; });
        }

        class CallTargetProbe final : public BaseIdentifierVisitor {
        public:
            bool isMember = false;

            void Visit(MemberAccessExpr* expr) override {
                isMember = true;
                BaseIdentifierVisitor::Visit(expr);
            }
        };

        inline bool IsExplicitArrayCopy(Expression* expression) {
            while (expression) {
                if (auto* slice = dynamic_cast<SliceExpr*>(expression)) {
                    expression = slice->base.get();
                    continue;
                }
                if (auto* access = dynamic_cast<ArrayAccessExpr*>(expression);
                    access && access->indexes.size() == 1 && !access->indexes.front()) {
                    expression = access->base.get();
                    continue;
                }
                break;
            }
            if (dynamic_cast<ConstructorCallExpr*>(expression)) return true;
            auto* call = dynamic_cast<FunctionCallExpr*>(expression);
            if (!call || !call->base) return false;
            CallTargetProbe probe;
            call->base->Accept(probe);
            return !probe.isMember && probe.identifierExpr && probe.identifierExpr->name == "copy";
        }

        inline bool IsExplicitMove(Expression* expression) {
            auto* call = dynamic_cast<FunctionCallExpr*>(expression);
            if (!call || !call->base) return false;
            CallTargetProbe probe;
            call->base->Accept(probe);
            return !probe.isMember && probe.identifierExpr && probe.identifierExpr->name == "move";
        }

        class StringLiteralProbe final : public BaseIdentifierVisitor {
        public:
            const StringLiteralExpr* literal = nullptr;

            void Visit(StringLiteralExpr* expr) override { literal = expr; }
        };

        class QualifiedNameVisitor final : public BaseIdentifierVisitor {
        public:
            std::string name;

            void Visit(IdentifierExpr* expr) override { name = expr->name; }

            void Visit(MemberAccessExpr* expr) override {
                if (expr->base) expr->base->Accept(*this);
                if (!name.empty()) name += ".";
                name += expr->member;
            }

            void Visit(UserTypeExpr* expr) override {
                if (expr->typeExpr) expr->typeExpr->Accept(*this);
            }

            void Visit(TemplateExpr* expr) override {
                if (expr->base) expr->base->Accept(*this);
            }
        };

        inline bool IsBuiltinFunction(const std::string& name) {
            return name == "print" || name == "println" || name == "format" ||
                name == "toString" || name == "assert" || name == "copy" ||
                name == "move" || name == "isOwner" || name == "debugBreak" ||
                name == "adoptRaw" || name == "retainRaw" || name == "borrowRaw" || name == "share" ||
                name == "unsafeArrayGet" || name == "unsafeArraySet" ||
                name == "unsafeArrayData" || name == "unsafeArrayCopy" ||
                name == "unsafeArrayMove" || name == "unsafeArrayTake" ||
                name == "unsafeArrayDrop" ||
                name == "seal" || name == "unseal" ||
                name == "load" || name == "isLoaded" || name == "loadError" ||
                name == "taskGroupAdd" || name == "tuple";
        }

        inline bool IsPrintableType(const std::string& type) {
            return type == "bool" || type == "string" || type == "char" || type == "null" ||
                type == "dynamic" || type == "error" || type == "float" || type == "double" ||
                type.starts_with("int") || type.starts_with("uint");
        }

        inline std::optional<size_t> CountFormatPlaceholders(const std::string& format) {
            size_t count = 0;
            for (size_t index = 0; index < format.size(); ++index) {
                if (format[index] == '{') {
                    if (index + 1 < format.size() && format[index + 1] == '{') {
                        ++index;
                    }
                    else if (index + 1 < format.size() && format[index + 1] == '}') {
                        ++count;
                        ++index;
                    }
                    else return std::nullopt;
                }
                else if (format[index] == '}') {
                    if (index + 1 < format.size() && format[index + 1] == '}') ++index;
                    else return std::nullopt;
                }
            }
            return count;
        }
    }

}
