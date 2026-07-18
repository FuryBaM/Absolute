#pragma once

namespace Absolute {
    struct StructDeclStmt : Statement {
        std::string name;
        std::vector<std::unique_ptr<Statement>> members;

        StructDeclStmt(std::string name, std::vector<std::unique_ptr<Statement>> members)
            : name(std::move(name)), members(std::move(members)) {
        }

        void Accept(StatementVisitor& visitor) override;
    };
}
