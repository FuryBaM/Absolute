#pragma once

namespace Absolute {
    struct CastExpr : Expression {
        std::unique_ptr<Expression> typeName;
        std::unique_ptr<Expression> base;

        CastExpr(std::unique_ptr<Expression> typeName,
            std::unique_ptr<Expression> base) :
            typeName(std::move(typeName)), base(std::move(base)) {
        }

        std::string ToString(int indent = 0) const override {
            std::string result = std::string(indent, ' ') + "Cast expression:\n";
            result += std::string(indent + 1, ' ') + "Type:\n";
            result += typeName->ToString(indent + 2) + "\n";
            result += std::string(indent + 1, ' ') + "Value:\n";
            result += base->ToString(indent + 2);
            return result;
        }

        void print(int indent = 0) override {
            std::cout << ToString(indent) << "\n";
        }

        void Accept(ExpressionVisitor& visitor) override;
    };
}