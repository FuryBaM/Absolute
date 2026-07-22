#pragma once
#include "analyzer_build_pch.h"
#include "analyzer_pch.h"
#include "expression_visitor.h"
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
            if (const auto found = substitutions.find(type); found != substitutions.end())
                return found->second;
            if (type.ends_with("[]"))
                return SubstituteGenericType(type.substr(0, type.size() - 2), substitutions) + "[]";
            if (IsPointerType(type)) {
                const std::string prefix = IsRawPointerType(type) ? "raw " : "";
                return prefix + SubstituteGenericType(PointerPointee(type), substitutions) + "*";
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

        inline bool UnifyGenericType(const std::string& pattern, const std::string& actual,
            const std::unordered_set<std::string>& parameters,
            std::unordered_map<std::string, std::string>& substitutions) {
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
            if (IsPointerType(pattern) && IsPointerType(actual) &&
                IsRawPointerType(pattern) == IsRawPointerType(actual))
                return UnifyGenericType(PointerPointee(pattern), PointerPointee(actual),
                    parameters, substitutions);
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
                name == "toString" || name == "assert" || name == "copy" || name == "move" ||
                name == "load";
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
