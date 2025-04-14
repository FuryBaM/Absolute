#pragma once

namespace Absolute {
    struct TernaryExpr : Expression {
        std::unique_ptr<Expression> condition;
        std::unique_ptr<Expression> trueExpr;
        std::unique_ptr<Expression> falseExpr;

        TernaryExpr(std::unique_ptr<Expression> condition,
            std::unique_ptr<Expression> trueExpr,
            std::unique_ptr<Expression> falseExpr) :
            condition(std::move(condition)), trueExpr(std::move(trueExpr)), falseExpr(std::move(falseExpr)) {
        }

        std::string ToString(int indent = 0) const override {
            std::string result = std::string(indent, ' ') + "Ternary expression:";
            if (condition) result += "\n" + std::string(indent + 1, ' ') + "Condition:\n" + condition->ToString(indent + 2);
            if (condition) result += "\n" + std::string(indent + 1, ' ') + "True:\n" + trueExpr->ToString(indent + 2);
            if (condition) result += "\n" + std::string(indent + 1, ' ') + "False:\n" + falseExpr->ToString(indent + 2);
            return result;
        }

        void print(int indent = 0) override {
            std::cout << ToString(indent) << "\n";
        }

        void Accept(ExpressionVisitor& visitor) override;
    };
}