#pragma once

namespace Absolute {
    struct InterfaceDeclStmt : Statement {
        std::string name;
        std::vector<std::string> parents;
        std::vector<std::unique_ptr<FunctionDeclStmt>> methods;

        InterfaceDeclStmt(std::string name, std::vector<std::string> parents,
            std::vector<std::unique_ptr<FunctionDeclStmt>> methods)
            : name(std::move(name)), parents(std::move(parents)), methods(std::move(methods)) {
        }

        void Accept(StatementVisitor& visitor) override;
    };
}
