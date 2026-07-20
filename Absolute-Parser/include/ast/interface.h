#pragma once

namespace Absolute {
    struct InterfaceDeclStmt : Statement {
        std::string name;
        std::vector<std::string> parents;
        std::vector<std::unique_ptr<FunctionDeclStmt>> methods;
        std::vector<std::unique_ptr<PropertyDeclStmt>> properties;
        std::vector<std::unique_ptr<IndexerDeclStmt>> indexers;

        InterfaceDeclStmt(std::string name, std::vector<std::string> parents,
            std::vector<std::unique_ptr<FunctionDeclStmt>> methods,
            std::vector<std::unique_ptr<PropertyDeclStmt>> properties = {},
            std::vector<std::unique_ptr<IndexerDeclStmt>> indexers = {})
            : name(std::move(name)), parents(std::move(parents)), methods(std::move(methods)),
              properties(std::move(properties)), indexers(std::move(indexers)) {
        }

        void Accept(StatementVisitor& visitor) override;
    };
}
