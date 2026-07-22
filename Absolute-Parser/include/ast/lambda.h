#pragma once

namespace Absolute {
    // Lambdas support either a compact expression body or a statement body.
    // Statement bodies may carry an explicit return type; otherwise the
    // surrounding func<...> context supplies it.
    struct LambdaExpr : Expression {
        std::vector<std::unique_ptr<VarDeclExpr>> parameters;
        std::unique_ptr<TypeExpr> returnType;
        std::unique_ptr<Expression> expressionBody;
        std::unique_ptr<Statement> statementBody;

        LambdaExpr(std::vector<std::unique_ptr<VarDeclExpr>> parameters,
            std::unique_ptr<Expression> body)
            : parameters(std::move(parameters)), expressionBody(std::move(body)) {
        }

        LambdaExpr(std::vector<std::unique_ptr<VarDeclExpr>> parameters,
            std::unique_ptr<TypeExpr> returnType, std::unique_ptr<Statement> body)
            : parameters(std::move(parameters)), returnType(std::move(returnType)),
              statementBody(std::move(body)) {
        }

        bool IsExpressionBody() const { return expressionBody != nullptr; }

        std::string ToString(int indent = 0) const override {
            std::string result = std::string(indent, ' ') + "Lambda";
            for (const auto& parameter : parameters)
                if (parameter) result += "\n" + parameter->ToString(indent + 1);
            if (returnType) result += "\n" + returnType->ToString(indent + 1);
            if (expressionBody) result += "\n" + expressionBody->ToString(indent + 1);
            if (statementBody) result += "\n" + statementBody->ToString(indent + 1);
            return result;
        }

        void Accept(ExpressionVisitor& visitor) override;
    };
}
