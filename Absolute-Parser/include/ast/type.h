#pragma once

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
        bool raw = false;

        PointerTypeExpr(std::unique_ptr<TypeExpr> pointee, bool raw)
            : pointee(std::move(pointee)), raw(raw) {}

        std::string ToString(int indent = 0) const override {
            return std::string(indent, ' ') + (raw ? "Raw pointer type:\n" : "Managed pointer type:\n") +
                (pointee ? pointee->ToString(indent + 1) : std::string(indent + 1, ' ') + "<missing>");
        }

        void print(int indent = 0) override { std::cout << ToString(indent) << "\n"; }
        void Accept(ExpressionVisitor& visitor) override;
    };
}
