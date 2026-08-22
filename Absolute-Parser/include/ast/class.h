#pragma once

namespace Absolute {
    struct ClassDeclStmt : Statement {
        std::string name;
        std::vector<std::string> parents;
        std::vector<Token> templateParams;
        // Parallel to templateParams: the requirement written in front of each
        // parameter's name, or empty. See Parser::ParseTemplateParameters.
        std::vector<std::string> templateConstraints;
        std::unique_ptr<Statement> body;

        ClassDeclStmt(std::string name, std::vector<std::string> parents, std::vector<Token> templateParams, std::unique_ptr<Statement> body)
            : name(std::move(name)), parents(std::move(parents)), templateParams(templateParams), body(std::move(body)) {
        }

        void print(int indent = 0) override {
            std::cout << std::string(indent, ' ') << "Class Declaration: " << name;

            if (!templateParams.empty()) {
                std::cout << " <";
                for (size_t i = 0; i < templateParams.size(); i++) {
                    if (i > 0) std::cout << ", ";
                    std::cout << templateParams[i].value;
                }
                std::cout << ">";
            }

            // Выводим модификаторы, если есть
            if (!modifiers.empty()) {
                std::cout << " [";
                for (size_t i = 0; i < modifiers.size(); i++) {
                    if (i > 0) std::cout << ", ";
                    std::cout << modifiers[i].value;
                }
                std::cout << "]";
            }

            if (!parents.empty()) {
                std::cout << " : ";

                for (const auto& parent : parents) {
                    std::cout << parent << " ";
                }
            }

            std::cout << "\n";
            body->print(indent + 1);
        }

        void Accept(StatementVisitor& visitor) override;
    };
}
