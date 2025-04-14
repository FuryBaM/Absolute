#pragma once

namespace Absolute {
    struct MemberAccessExpr : Expression {
        std::unique_ptr<Expression> base;  // К чему применяем `.`
        std::string member;                // Имя члена

        MemberAccessExpr(std::unique_ptr<Expression> base, std::string member)
            : base(std::move(base)), member(std::move(member)) {
        }

        IdentifierExpr* GetIdentifier();

        std::string ToString(int indent = 0) const override {
            return std::string(indent, ' ') + "Member Access: " + member + "\n" +
                base->ToString(indent + 1);
        }

        void print(int indent = 0) override {
            std::cout << ToString(indent) << "\n";
        }

        void Accept(ExpressionVisitor& visitor) override;
    };
}