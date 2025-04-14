#pragma once

namespace Absolute {
    struct AssignmentExpr : Expression {
        std::unique_ptr<Expression> target;
        std::string op;
        std::unique_ptr<Expression> value;

        AssignmentExpr(std::unique_ptr<Expression> target, std::string op, std::unique_ptr<Expression> value)
            : target(std::move(target)), op(std::move(op)), value(std::move(value)) {
        }

        std::string ToString(int indent = 0) const override {
            std::string padding(indent, ' ');
            std::string result = padding + "Assignment:\n";

            result += padding + " Target:\n";
            result += target->ToString(indent + 2) + "\n";

            result += padding + " Operator: " + op + "\n";

            result += padding + " Value:\n";
            result += value->ToString(indent + 2);

            return result;
        }

        void print(int indent = 0) override {
            std::cout << ToString(indent) + "\n";
        }

        void Accept(ExpressionVisitor& visitor) override;
    };

    struct AssignmentStmt : Statement {
        std::unique_ptr<AssignmentExpr> expr;

        explicit AssignmentStmt(std::unique_ptr<AssignmentExpr> expr)
            : expr(std::move(expr)) {
        }

        std::string ToString(int indent = 0) const override {
            return expr->ToString(indent);
        }

        void print(int indent = 0) override {
            std::cout << expr->ToString(indent) + "\n";
        }
    };
}