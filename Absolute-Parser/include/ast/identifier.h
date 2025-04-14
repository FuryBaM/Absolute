#pragma once

struct IdentifierExpr : Expression {
    std::string name;
    explicit IdentifierExpr(std::string name) : name(std::move(name)) {}
    IdentifierExpr(IdentifierExpr& other) : name(std::move(other.name)) {}

    std::string ToString(int indent = 0) const override {
        return std::string(indent, ' ') + "Identifier: " + name;
    }

    void print(int indent = 0) override {
        std::cout << ToString(indent) << "\n";
    }

    void Accept(ExpressionVisitor& visitor) override;
};