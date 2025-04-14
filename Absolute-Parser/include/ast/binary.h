#pragma once

struct BinaryExpr : Expression {
    std::string op;
    std::unique_ptr<Expression> left, right;

    BinaryExpr(std::string op, std::unique_ptr<Expression> left, std::unique_ptr<Expression> right)
        : op(std::move(op)), left(std::move(left)), right(std::move(right)) {
    }

    std::string ToString(int indent = 0) const override {
        std::string result = std::string(indent, ' ') + "Binary expression (" + op + "):\n";
        if (left) result += left->ToString(indent + 1) + "\n";
        if (right) result += right->ToString(indent + 1);
        return result;
    }

    void print(int indent = 0) override {
        std::cout << ToString(indent) << "\n";
    }
    void Accept(ExpressionVisitor& visitor) override;
};