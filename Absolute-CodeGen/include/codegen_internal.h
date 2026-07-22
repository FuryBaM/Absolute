#pragma once
#include "codegen_pch.h"
#include "expression_visitor.h"
#include "analyzer.h"
#include "codegen.h"
#include "syntax_plugins.h"

namespace Absolute {
    extern std::string g_last_context;
    namespace {

        class PrimitiveTypeNameVisitor final : public BaseIdentifierVisitor {
        public:
            std::string name;

            void Visit(PrimitiveTypeExpr* expr) override {
                name = expr->type;
            }

            void Visit(UserTypeExpr* expr) override {
                (void)expr;
            }

            void Visit(PointerTypeExpr* expr) override {
                name.clear();
                if (expr->pointee) expr->pointee->Accept(*this);
                if (!name.empty()) name = (expr->raw ? "raw " : "") + name + "*";
            }

            void Visit(ArrayTypeExpr* expr) override {
                name.clear();
                if (expr->element) expr->element->Accept(*this);
                if (!name.empty()) name += "[]";
            }
        };

        class StringLiteralProbe final : public BaseIdentifierVisitor {
        public:
            const StringLiteralExpr* literal = nullptr;

            void Visit(StringLiteralExpr* expr) override { literal = expr; }
        };

        inline std::string IdentifierName(Expression* expression) {
            if (!expression) return {};
            BaseIdentifierVisitor visitor;
            expression->Accept(visitor);
            return visitor.identifierExpr ? visitor.identifierExpr->name : std::string{};
        }

        inline bool IsRawPointerTypeName(const std::string& name) {
            return name.starts_with("raw ") && name.ends_with("*");
        }

        inline bool IsManagedPointerTypeName(const std::string& name) {
            return !IsRawPointerTypeName(name) && name.ends_with("*");
        }

        inline bool IsPointerTypeName(const std::string& name) {
            return IsRawPointerTypeName(name) || IsManagedPointerTypeName(name);
        }

        inline std::string PointerPointeeName(std::string name) {
            if (IsRawPointerTypeName(name)) name.erase(0, 4);
            if (!name.empty() && name.back() == '*') name.pop_back();
            return name;
        }

        inline std::string EncodeLinkComponent(const std::string& value) {
            constexpr char digits[] = "0123456789ABCDEF";
            std::string result;
            for (const unsigned char character : value) {
                if (std::isalnum(character) || character == '_') result.push_back(static_cast<char>(character));
                else {
                    result.push_back('_');
                    result.push_back(digits[character >> 4]);
                    result.push_back(digits[character & 15]);
                }
            }
            return result;
        }

        inline std::string CallableKey(const std::string& name, const std::vector<std::string>& parameterTypes) {
            std::string result = name;
            for (const std::string& type : parameterTypes) result += "$" + EncodeLinkComponent(type);
            return result;
        }

        inline bool IsTaskTypeName(const std::string& name) {
            return name.size() > 6 && name.starts_with("task<") && name.ends_with(">");
        }

        inline bool HasModifier(const Statement& statement, const std::string& name) {
            return std::any_of(statement.modifiers.begin(), statement.modifiers.end(),
                [&](const Token& modifier) { return modifier.value == name; });
        }

        inline void ApplyCallableAttributes(llvm::Function& function, const Statement& statement) {
            if (statement.FindAttribute("inline"))
                function.addFnAttr(llvm::Attribute::AlwaysInline);
            if (statement.FindAttribute("noinline"))
                function.addFnAttr(llvm::Attribute::NoInline);
        }

        inline size_t ArrayRankName(std::string type) {
            size_t rank = 0;
            while (type.ends_with("[]")) {
                type.resize(type.size() - 2);
                ++rank;
            }
            return rank;
        }

        inline std::string ArrayElementTypeName(std::string type, size_t dimensions = 1) {
            while (dimensions-- > 0 && type.ends_with("[]")) type.resize(type.size() - 2);
            return type;
        }

        inline std::string TaskValueTypeName(const std::string& name) {
            return IsTaskTypeName(name) ? name.substr(5, name.size() - 6) : std::string{};
        }

        inline std::string SubstituteCodegenType(std::string type,
            const std::unordered_map<std::string, std::string>& substitutions) {
            size_t startPos = type.find_first_not_of(" \t");
            if (startPos != std::string::npos) {
                size_t endPos = type.find_last_not_of(" \t");
                type = type.substr(startPos, endPos - startPos + 1);
            }
            if (const auto found = substitutions.find(type); found != substitutions.end())
                return found->second;

            if (type.ends_with("[]"))
                return SubstituteCodegenType(type.substr(0, type.size() - 2), substitutions) + "[]";
            if (IsPointerTypeName(type)) {
                const std::string prefix = IsRawPointerTypeName(type) ? "raw " : "";
                return prefix + SubstituteCodegenType(PointerPointeeName(type), substitutions) + "*";
            }
            const size_t open = type.find('<');
            if (open == std::string::npos || type.empty() || type.back() != '>') return type;
            std::string result = type.substr(0, open + 1);
            size_t depth = 0;
            size_t start = open + 1;
            bool first = true;
            for (size_t index = start; index + 1 < type.size(); ++index) {
                if (type[index] == '<') ++depth;
                else if (type[index] == '>') --depth;
                else if (type[index] == ',' && depth == 0) {
                    if (!first) result += ",";
                    result += SubstituteCodegenType(type.substr(start, index - start), substitutions);
                    first = false;
                    start = index + 1;
                }
            }
            if (!first) result += ",";
            result += SubstituteCodegenType(type.substr(start, type.size() - start - 1), substitutions);
            return result + ">";
        }

        inline bool ParseCodegenGenericType(
            const std::string& type, std::string& base, std::vector<std::string>& arguments) {
            const size_t open = type.find('<');
            if (open == std::string::npos || type.empty() || type.back() != '>') return false;
            base = type.substr(0, open);
            arguments.clear();
            size_t depth = 0;
            size_t start = open + 1;
            for (size_t index = start; index + 1 < type.size(); ++index) {
                if (type[index] == '<') ++depth;
                else if (type[index] == '>') --depth;
                else if (type[index] == ',' && depth == 0) {
                    arguments.push_back(type.substr(start, index - start));
                    start = index + 1;
                }
            }
            arguments.push_back(type.substr(start, type.size() - start - 1));
            return true;
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

        inline void FlattenArrayValues(ArrayExpr& array, std::vector<Expression*>& output) {
            for (const auto& value : array.values) {
                if (auto* nested = dynamic_cast<ArrayExpr*>(value.get())) FlattenArrayValues(*nested, output);
                else output.push_back(value.get());
            }
        }

        class FunctionCallProbe final : public BaseIdentifierVisitor {
        public:
            FunctionCallExpr* call = nullptr;

            void Visit(FunctionCallExpr* expr) override { call = expr; }
        };
    }

    struct CodeGenerator::Impl {
#include "internal/codegen_state.inc"
#include "internal/codegen_types.inc"
#include "internal/codegen_abi.inc"
#include "internal/codegen_runtime.inc"
#include "internal/codegen_module.inc"
    };

}
