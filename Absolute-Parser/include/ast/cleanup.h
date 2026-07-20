#pragma once

namespace Absolute {
    struct DeferStmt : Statement {
        std::unique_ptr<CompoundStmt> body;

        explicit DeferStmt(std::unique_ptr<CompoundStmt> body)
            : body(std::move(body)) {
        }

        void print(int indent = 0) override {
            std::cout << std::string(indent, ' ') << "Defer statement:\n";
            if (body) body->print(indent + 1);
        }

        void Accept(StatementVisitor& visitor) override;
    };
}
