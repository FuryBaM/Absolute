#pragma once
#include "analyzer_build_pch.h"
#include "analyzer_pch.h"
#include "syntax_plugins.h"

namespace Absolute {
    namespace {
        template <typename T, typename Visitor>
        void AcceptIfPresent(const std::unique_ptr<T>& node, Visitor& visitor) {
            if (node) node->Accept(visitor);
        }

        template <typename T, typename Visitor>
        void AcceptAll(const std::vector<std::unique_ptr<T>>& nodes, Visitor& visitor) {
            for (const auto& node : nodes) AcceptIfPresent(node, visitor);
        }

        inline bool IsConditionType(const std::string& type) {
            return type == "bool" || type == "dynamic" || type == "error" ||
                type.starts_with("int") || type.starts_with("uint") || type.ends_with("*");
        }

        inline bool IsRawPointerType(const std::string& type) {
            return type.starts_with("raw ") && type.ends_with("*");
        }

        inline bool IsManagedPointerType(const std::string& type) {
            return !IsRawPointerType(type) && type.ends_with("*");
        }

        inline bool IsPointerType(const std::string& type) {
            return IsRawPointerType(type) || IsManagedPointerType(type);
        }

        inline std::string PointerPointee(std::string type) {
            if (IsRawPointerType(type)) type.erase(0, 4);
            if (!type.empty() && type.back() == '*') type.pop_back();
            return type;
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

        inline std::string TaskValueType(const std::string& type) {
            return IsTaskType(type) ? type.substr(5, type.size() - 6) : "error";
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
                name == "toString" || name == "assert";
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
