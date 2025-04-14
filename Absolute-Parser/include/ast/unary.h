#pragma once

struct PrefixUnaryExpr : Expression {
    std::string op;
    std::unique_ptr<Expression> operand;

    PrefixUnaryExpr(std::string op, std::unique_ptr<Expression> operand)
        : op(std::move(op)), operand(std::move(operand)) {
    }

    std::string ToString(int indent = 0) const override {
        std::string result = std::string(indent, ' ') + "Prefix Unary expression ";

        result += op + ":\n";
        result += operand->ToString(indent + 1);
        return result;
    }

    void print(int indent = 0) override {
        std::cout << ToString(indent) << "\n";
    }

    void Accept(ExpressionVisitor& visitor) override;
};

struct PostfixUnaryExpr : Expression {
    std::string op;
    std::unique_ptr<Expression> operand;

    PostfixUnaryExpr(std::string op, std::unique_ptr<Expression> operand)
        : op(std::move(op)), operand(std::move(operand)) {
    }

    std::string ToString(int indent = 0) const override {
        std::string result = std::string(indent, ' ') + "Postfix Unary expression: ";

        result += op; // Повторяем оператор `repeats` раз
        if (operand) {
            result += operand->ToString(0);
        }
        return result;
    }

    void print(int indent = 0) override {
        std::cout << ToString(indent) << "\n";
    }

    void Accept(ExpressionVisitor& visitor) override;
};