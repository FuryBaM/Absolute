#pragma once

namespace Absolute {
    struct FunctionCallExpr : Expression {
        std::unique_ptr<Expression> base;
        std::vector<std::unique_ptr<Expression>> arguments;

        FunctionCallExpr(std::unique_ptr<Expression> base, std::vector<std::unique_ptr<Expression>> args)
            : base(std::move(base)), arguments(std::move(args)) {
        }

        IdentifierExpr* GetIdentifier();

        std::string ToString(int indent = 0) const override {
            std::string result = std::string(indent, ' ') + "Function call:";

            result += "\n" + base->ToString(indent + 1);

            if (!arguments.empty()) {
                result += "\n" + std::string(indent + 1, ' ') + "Arguments:";
                for (const auto& arg : arguments) {
                    result += "\n" + arg->ToString(indent + 2);
                }
            }
            return result;
        }


        void print(int indent = 0) override {
            std::cout << ToString(indent) << "\n";
        }

        void Accept(ExpressionVisitor& visitor) override;
    };

    struct FunctionCallStmt : Statement {
        std::string name;
        std::unique_ptr<FunctionCallExpr> value;

        explicit FunctionCallStmt(std::string name,
            std::unique_ptr<FunctionCallExpr> value)
            : name(std::move(name)), value(std::move(value)) {
        }

        void print(int indent = 0) override {
            std::string prefix(indent, ' ');
            std::cout << prefix << "Function Call";

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
            if (value) value->print(indent + 1);
        }

        void Accept(StatementVisitor& visitor) override;
    };

    struct FunctionDeclStmt : Statement {
        std::unique_ptr<Token> returnType;
        std::unique_ptr<Token> name;
        std::vector<std::unique_ptr<VarDeclExpr>> parameters;
        std::unique_ptr<Statement> body;
        std::string externalAbi;

        FunctionDeclStmt(std::unique_ptr<Token> returnType, std::unique_ptr<Token> name,
            std::vector<std::unique_ptr<VarDeclExpr>> params,
            std::unique_ptr<Statement> body)
            : returnType(std::move(returnType)),
            name(std::move(name)),
            parameters(std::move(params)),
            body(std::move(body)) {
        }

        bool IsExternal() const { return !externalAbi.empty(); }

        void print(int indent = 0) override {
            std::cout << std::string(indent, ' ') << "Function declaration: ";
            if (IsExternal()) std::cout << "extern \"" << externalAbi << "\" ";
            std::cout << returnType->value << " " << name->value;
            // Выводим модификаторы, если есть
            if (!modifiers.empty()) {
                std::cout << " [";
                for (size_t i = 0; i < modifiers.size(); i++) {
                    if (i > 0) std::cout << ", ";
                    std::cout << modifiers[i].value;
                }
                std::cout << "]";
            }

            std::cout << "\n";
            if (parameters.size()) {
                std::cout << std::string(indent + 1, ' ') << "Function parameters: " << "\n";
                for (const auto& param : parameters) {
                    param.get()->print(indent + 2);
                }
            }
            if (body) body->print(indent + 1);
        }

        void Accept(StatementVisitor& visitor) override;
    };

    struct ReturnStmt : Statement {
        std::unique_ptr<Expression> expr;
        explicit ReturnStmt(std::unique_ptr<Expression> expr) : expr(std::move(expr)) {}
        void print(int indent = 0) override {
            std::cout << std::string(indent, ' ') << "Return statement: " << "\n";
            if (expr) expr->print(indent + 1);
        }

        void Accept(StatementVisitor& visitor) override;
    };
}
