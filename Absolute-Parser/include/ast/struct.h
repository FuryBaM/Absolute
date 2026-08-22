#pragma once

namespace Absolute {
    struct StructDeclStmt : Statement {
        std::string name;
        std::vector<Token> templateParams;
        // Parallel to templateParams: the requirement written in front of each
        // parameter's name, or empty. See Parser::ParseTemplateParameters.
        std::vector<std::string> templateConstraints;
        std::vector<std::unique_ptr<Statement>> members;

        StructDeclStmt(std::string name, std::vector<Token> templateParams,
            std::vector<std::unique_ptr<Statement>> members)
            : name(std::move(name)), templateParams(std::move(templateParams)),
            members(std::move(members)) {
        }

        void Accept(StatementVisitor& visitor) override;
    };
}
