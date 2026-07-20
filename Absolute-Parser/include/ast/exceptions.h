#pragma once

namespace Absolute {
    struct ThrowStmt : Statement {
        std::unique_ptr<Expression> value;

        explicit ThrowStmt(std::unique_ptr<Expression> value)
            : value(std::move(value)) {
        }

        void print(int indent = 0) override {
            std::cout << std::string(indent, ' ') << "Throw statement:\n";
            if (value) value->print(indent + 1);
        }

        void Accept(StatementVisitor& visitor) override;
    };

    struct TryStmt : Statement {
        struct CatchClause {
            std::unique_ptr<VarDeclExpr> parameter;
            std::unique_ptr<CompoundStmt> body;

            CatchClause(std::unique_ptr<VarDeclExpr> parameter,
                std::unique_ptr<CompoundStmt> body)
                : parameter(std::move(parameter)), body(std::move(body)) {
            }
        };

        std::unique_ptr<CompoundStmt> body;
        std::vector<CatchClause> catches;
        std::unique_ptr<CompoundStmt> finallyBody;

        TryStmt(std::unique_ptr<CompoundStmt> body, std::vector<CatchClause> catches,
            std::unique_ptr<CompoundStmt> finallyBody)
            : body(std::move(body)), catches(std::move(catches)),
              finallyBody(std::move(finallyBody)) {
        }

        void print(int indent = 0) override {
            std::cout << std::string(indent, ' ') << "Try statement:\n";
            if (body) body->print(indent + 1);
            for (const auto& clause : catches) {
                std::cout << std::string(indent + 1, ' ') << "Catch:\n";
                if (clause.parameter) clause.parameter->print(indent + 2);
                if (clause.body) clause.body->print(indent + 2);
            }
            if (finallyBody) {
                std::cout << std::string(indent + 1, ' ') << "Finally:\n";
                finallyBody->print(indent + 2);
            }
        }

        void Accept(StatementVisitor& visitor) override;
    };
}
