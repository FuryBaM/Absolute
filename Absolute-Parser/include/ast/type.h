#pragma once

#include "type_names.h"

namespace Absolute {
    struct TypeExpr : public Expression {};  // Базовый класс для всех типов

    struct PrimitiveTypeExpr : TypeExpr {
        std::string type;

        PrimitiveTypeExpr(std::string type) : type(type) {}

        std::string ToString(int indent = 0) const override {
            std::string result = std::string(indent, ' ') + "Primitive type: " + type;
            return result;
        }

        void print(int indent = 0) override {
            std::cout << ToString(indent) << "\n";
        }

        void Accept(ExpressionVisitor& visitor) override;
    };

    struct UserTypeExpr : public TypeExpr {
        std::unique_ptr<Expression> typeExpr;  // Может быть сложное выражение (например, шаблон)

        UserTypeExpr(std::unique_ptr<Expression> expr) : typeExpr(std::move(expr)) {}

        std::string ToString(int indent = 0) const override {
            std::string result = std::string(indent, ' ') + "User type:\n";
            result += typeExpr->ToString(indent + 1);
            return result;
        }

        void print(int indent = 0) override {
            std::cout << ToString(indent) << "\n";
        }

        void Accept(ExpressionVisitor& visitor) override;
    };

    struct PointerTypeExpr : public TypeExpr {
        std::unique_ptr<TypeExpr> pointee;
        // One kind rather than three independent flags. Flags that can disagree
        // are what let a qualifier go missing: two of them said what the
        // pointer was and every reader had to combine them the same way.
        OwnershipKind ownership = OwnershipKind::Unique;

        PointerTypeExpr(std::unique_ptr<TypeExpr> pointee, OwnershipKind ownership)
            : pointee(std::move(pointee)), ownership(ownership) {}

        bool IsRaw() const { return ownership == OwnershipKind::Raw; }
        bool IsWeak() const { return ownership == OwnershipKind::Weak; }
        bool IsSubscriber() const { return ownership == OwnershipKind::Sub; }

        std::string ToString(int indent = 0) const override {
            std::string kind;
            switch (ownership) {
            case OwnershipKind::Raw: kind = "Raw pointer type:\n"; break;
            case OwnershipKind::Weak: kind = "Weak managed pointer type:\n"; break;
            case OwnershipKind::Sub: kind = "Subscriber pointer type:\n"; break;
            default: kind = "Managed pointer type:\n"; break;
            }
            return std::string(indent, ' ') + kind +
                (pointee ? pointee->ToString(indent + 1) : std::string(indent + 1, ' ') + "<missing>");
        }

        void print(int indent = 0) override { std::cout << ToString(indent) << "\n"; }
        void Accept(ExpressionVisitor& visitor) override;
    };

    struct ArrayTypeExpr : public TypeExpr {
        std::unique_ptr<TypeExpr> element;

        explicit ArrayTypeExpr(std::unique_ptr<TypeExpr> element)
            : element(std::move(element)) {}

        std::string ToString(int indent = 0) const override {
            return std::string(indent, ' ') + "Array type:\n" +
                (element ? element->ToString(indent + 1) : std::string(indent + 1, ' ') + "<missing>");
        }

        void print(int indent = 0) override { std::cout << ToString(indent) << "\n"; }
        void Accept(ExpressionVisitor& visitor) override;
    };
}
