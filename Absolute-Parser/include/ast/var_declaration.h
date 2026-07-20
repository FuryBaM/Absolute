#pragma once

namespace Absolute {
    struct VarDeclExpr : Expression {
        std::unique_ptr<Expression> type;
        std::unique_ptr<Expression> name;
        std::unique_ptr<Expression> value;
        bool isConst = false;
        bool isStatic = false;

        explicit VarDeclExpr(std::unique_ptr<Expression> type, std::unique_ptr<Expression> name, std::unique_ptr<Expression> value)
            : type(std::move(type)), name(std::move(name)), value(std::move(value)) {
        }

        std::string ToString(int indent = 0) const override {
            std::string result = std::string(indent, ' ') + "Variable declaration: " + type->ToString(0) + "\n";
            result += std::string(indent + 1, ' ') + "Name:\n" + name->ToString(indent + 2);

            if (value) result += "\n" + std::string(indent + 1, ' ') + "Value:\n" + value->ToString(indent + 2);
            return result;
        }

        void print(int indent = 0) override {
            std::cout << ToString(indent) << "\n";
        }

        void Accept(ExpressionVisitor& visitor) override;
    };

    struct VarDeclStmt : Statement {
        std::unique_ptr<VarDeclExpr> expr;

        explicit VarDeclStmt(std::unique_ptr<VarDeclExpr> expr)
            : expr(std::move(expr)) {
        }

        void print(int indent = 0) override {
            std::string prefix(indent, ' ');
            std::cout << prefix << "Variable Declaration Statement";

            // Выводим модификаторы, если есть
            if (!modifiers.empty()) {
                std::cout << " [";
                for (size_t i = 0; i < modifiers.size(); i++) {
                    if (i > 0) std::cout << ", ";
                    std::cout << modifiers[i].value;
                }
                std::cout << "]";
            }

            std::cout << ":\n";

            // Выводим само выражение
            if (expr) expr->print(indent + 1);
        }

        void Accept(StatementVisitor& visitor) override;
    };
}
