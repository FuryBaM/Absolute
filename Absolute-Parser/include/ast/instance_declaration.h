#pragma once

namespace Absolute {
    struct InstanceDeclExpr : Expression {
        std::unique_ptr<Expression> constructType;
        std::unique_ptr<Expression> identifierName;
        std::unique_ptr<Expression> value;
        bool isConst = false;
        bool isStatic = false;

        InstanceDeclExpr(std::unique_ptr<Expression> constructType,
            std::unique_ptr<Expression> identifierName,
            std::unique_ptr<Expression> value)
            : constructType(std::move(constructType)), identifierName(std::move(identifierName)), value(std::move(value)) {
        }

        std::string ToString(int indent = 0) const override {
            std::string result = std::string(indent, ' ') + "Instance declaration:\n";
            result += std::string(indent + 1, ' ') + "Type:\n";
            result += constructType->ToString(indent + 2) + "\n";
            result += std::string(indent + 1, ' ') + "Identifier:\n";
            result += identifierName->ToString(indent + 2);
            if (value) {
                result += "\n" + std::string(indent + 1, ' ') + "Value:\n";
                result += value->ToString(indent + 2);
            }
            return result;
        }

        void print(int indent = 0) override {
            std::cout << ToString(indent) + "\n";
        }

        void Accept(ExpressionVisitor& visitor) override;
    };
}
