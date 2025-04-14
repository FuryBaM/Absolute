#pragma once

namespace Absolute {
    struct TemplateExpr : Expression {
        std::unique_ptr<Expression> base;
        std::vector<std::unique_ptr<Expression>> types;

        TemplateExpr(std::unique_ptr<Expression> base, std::vector<std::unique_ptr<Expression>> types) :
            base(std::move(base)), types(std::move(types)) {
        }

        IdentifierExpr* GetIdentifier();

        std::string ToString(int indent = 0) const override {
            std::string result = std::string(indent, ' ') + "Template:";
            if (!types.empty()) {
                for (const auto& type : types) {
                    result += "\n" + type->ToString(indent + 1);
                }
            }
            result += ":\n" + base->ToString(indent + 1);
            return result;
        }

        void print(int indent = 0) override {
            std::cout << ToString(indent) << "\n";
        }

        void Accept(ExpressionVisitor& visitor) override;
    };
}