#pragma once

namespace Absolute {
    // A lambda has an explicit parameter list and an expression body. Lambdas
    // intentionally start captureless; a closure ABI can be added later
    // without changing the function-value type used by callers.
    struct LambdaExpr : Expression {
        std::vector<std::unique_ptr<VarDeclExpr>> parameters;
        std::unique_ptr<Expression> body;

        LambdaExpr(std::vector<std::unique_ptr<VarDeclExpr>> parameters,
            std::unique_ptr<Expression> body)
            : parameters(std::move(parameters)), body(std::move(body)) {
        }

        std::string ToString(int indent = 0) const override {
            std::string result = std::string(indent, ' ') + "Lambda";
            for (const auto& parameter : parameters)
                if (parameter) result += "\n" + parameter->ToString(indent + 1);
            if (body) result += "\n" + body->ToString(indent + 1);
            return result;
        }

        void Accept(ExpressionVisitor& visitor) override;
    };
}
