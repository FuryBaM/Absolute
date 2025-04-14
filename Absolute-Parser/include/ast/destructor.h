#pragma once

namespace Absolute {
    struct DestructorCallExpr : Expression {
        std::unique_ptr<Expression> target;

        DestructorCallExpr(std::unique_ptr<Expression> target)
            : target(std::move(target)) {
        }

        std::string ToString(int indent = 0) const override {
            std::string result = std::string(indent, ' ') + "Desctructor call:\n";
            if (target) result += target->ToString(indent + 1);
            return result;
        }

        void print(int indent = 0) override {
            std::cout << ToString(indent) + "\n";
        }

        void Accept(ExpressionVisitor& visitor) override;
    };
}