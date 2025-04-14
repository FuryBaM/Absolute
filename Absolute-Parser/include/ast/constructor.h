#pragma once

struct ConstructorCallExpr : Expression {
    std::unique_ptr<Expression> constructName;

    ConstructorCallExpr(std::unique_ptr<Expression> constructName)
        : constructName(std::move(constructName)) {
    }

    std::string ToString(int indent = 0) const override {
        std::string result = std::string(indent, ' ') + "Constructor call:\n";
        result += constructName->ToString(indent + 1);
        return result;
    }

    void print(int indent = 0) override {
        std::cout << ToString(indent) + "\n";
    }

    void Accept(ExpressionVisitor& visitor) override;
};

struct ConstructorDeclStmt : Statement {
    std::unique_ptr<Token> name;
    std::vector<std::unique_ptr<VarDeclExpr>> parameters;
    std::unique_ptr<Statement> body;

    ConstructorDeclStmt(std::unique_ptr<Token> name,
        std::vector<std::unique_ptr<VarDeclExpr>> parameters, std::unique_ptr<Statement> body)
        : name(std::move(name)), parameters(std::move(parameters)), body(std::move(body)) {
    }

    void print(int indent = 0) override {
        std::cout << std::string(indent, ' ') << "Constructor declaration: " << name->value;

        if (!modifiers.empty()) {
            std::cout << " [";
            for (size_t i = 0; i < modifiers.size(); i++) {
                if (i > 0) std::cout << ", ";
                std::cout << modifiers[i].value;
            }
            std::cout << "]";
        }

        std::cout << "\n";

        if (!parameters.empty()) {
            std::cout << std::string(indent + 1, ' ') << "Constructor parameters:\n";
            for (const auto& param : parameters) {
                param->print(indent + 2);
            }
        }

        if (body) body->print(indent + 1);
    }
};