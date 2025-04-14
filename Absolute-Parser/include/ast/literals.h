#pragma once

namespace Absolute {
    struct NullExpr : Expression {
        NullExpr() = default;

        std::string ToString(int indent = 0) const override {
            std::string result = std::string(indent, ' ') + "Null expression";
            return result;
        }

        void print(int indent = 0) override {
            std::cout << ToString(indent) << "\n";
        }

        void Accept(ExpressionVisitor& visitor) override;
    };

    struct BooleanLiteralExpr : Expression {
        bool value;

        BooleanLiteralExpr(bool value) : value(value) {
        }

        std::string ToString(int indent = 0) const override {
            std::string result = std::string(indent, ' ') + "Boolean literal: " + (value ? "true" : "false");
            return result;
        }

        void print(int indent = 0) override {
            std::cout << ToString(indent) << "\n";
        }

        void Accept(ExpressionVisitor& visitor) override;
    };

    struct NumberLiteralExpr : Expression {
        std::string value;
        explicit NumberLiteralExpr(std::string value) : value(std::move(value)) {}

        std::string ToString(int indent = 0) const override {
            return std::string(indent, ' ') + "Number literal: " + value;
        }

        void print(int indent = 0) override {
            std::cout << ToString(indent) << "\n";
        }

        void Accept(ExpressionVisitor& visitor) override;
    };

    struct StringLiteralExpr : Expression {
        std::string value;
        explicit StringLiteralExpr(std::string value) : value(std::move(value)) {}

        std::string ToString(int indent = 0) const override {
            return std::string(indent, ' ') + "String literal: " + value;
        }

        void print(int indent = 0) override {
            std::cout << ToString(indent) << "\n";
        }

        void Accept(ExpressionVisitor& visitor) override;
    };

    struct CharLiteralExpr : Expression {
        char value;
        explicit CharLiteralExpr(char value) : value(value) {}

        std::string ToString(int indent = 0) const override {
            return std::string(indent, ' ') + "Char literal: " + value;
        }

        void print(int indent = 0) override {
            std::cout << ToString(indent) << "\n";
        }

        void Accept(ExpressionVisitor& visitor) override;
    };
}