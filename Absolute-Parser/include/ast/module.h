#pragma once

namespace Absolute {
    struct ImportStmt : Statement {
        std::string target;
        bool isFile = false;

        ImportStmt(std::string target, bool isFile)
            : target(std::move(target)), isFile(isFile) {
        }

        std::string ToString(int indent = 0) const override {
            return std::string(indent, ' ') + "Import " + (isFile ? "file: " : "namespace: ") + target;
        }

        void Accept(StatementVisitor& visitor) override;
    };

    struct NamespaceDeclStmt : Statement {
        std::string name;
        std::unique_ptr<CompoundStmt> body;

        NamespaceDeclStmt(std::string name, std::unique_ptr<CompoundStmt> body)
            : name(std::move(name)), body(std::move(body)) {
        }

        std::string ToString(int indent = 0) const override {
            std::string result = std::string(indent, ' ') + "Namespace: " + name;
            if (body) result += "\n" + body->ToString(indent + 1);
            return result;
        }

        void print(int indent = 0) override {
            std::cout << std::string(indent, ' ') << "Namespace: " << name << '\n';
            if (body) body->print(indent + 1);
        }

        void Accept(StatementVisitor& visitor) override;
    };
}
