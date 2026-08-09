#pragma once

namespace Absolute {
    struct InterfaceDeclStmt : Statement {
        std::string name;
        std::vector<Token> templateParams;
        std::vector<std::string> parents;
        std::vector<std::unique_ptr<FunctionDeclStmt>> methods;
        std::vector<std::unique_ptr<PropertyDeclStmt>> properties;
        std::vector<std::unique_ptr<IndexerDeclStmt>> indexers;
        std::vector<std::unique_ptr<VarDeclStmt>> staticFields;

        InterfaceDeclStmt(std::string name, std::vector<Token> templateParams,
            std::vector<std::string> parents,
            std::vector<std::unique_ptr<FunctionDeclStmt>> methods,
            std::vector<std::unique_ptr<PropertyDeclStmt>> properties = {},
            std::vector<std::unique_ptr<IndexerDeclStmt>> indexers = {},
            std::vector<std::unique_ptr<VarDeclStmt>> staticFields = {})
            : name(std::move(name)), templateParams(std::move(templateParams)),
              parents(std::move(parents)), methods(std::move(methods)),
              properties(std::move(properties)), indexers(std::move(indexers)),
              staticFields(std::move(staticFields)) {
        }

        void Accept(StatementVisitor& visitor) override;
    };
}
