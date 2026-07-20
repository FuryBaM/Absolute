#pragma once

namespace Absolute {
    struct SwitchStmt : Statement {
        struct Case {
            std::unique_ptr<Expression> label;
            std::unique_ptr<CompoundStmt> body;

            Case(std::unique_ptr<Expression> label, std::unique_ptr<CompoundStmt> body)
                : label(std::move(label)), body(std::move(body)) {
            }
        };

        std::unique_ptr<Expression> value;
        std::vector<Case> cases;
        std::unique_ptr<CompoundStmt> defaultCase;
        bool exhaustive = false;

        SwitchStmt(std::unique_ptr<Expression> value, std::vector<Case> cases,
            std::unique_ptr<CompoundStmt> defaultCase, bool exhaustive)
            : value(std::move(value)), cases(std::move(cases)),
              defaultCase(std::move(defaultCase)), exhaustive(exhaustive) {
        }

        void print(int indent = 0) override {
            std::cout << std::string(indent, ' ')
                << (exhaustive ? "Match statement:\n" : "Switch statement:\n");
            if (value) value->print(indent + 1);
            for (const auto& branch : cases) {
                std::cout << std::string(indent + 1, ' ') << "Case:\n";
                if (branch.label) branch.label->print(indent + 2);
                if (branch.body) branch.body->print(indent + 2);
            }
            if (defaultCase) {
                std::cout << std::string(indent + 1, ' ') << "Default:\n";
                defaultCase->print(indent + 2);
            }
        }

        void Accept(StatementVisitor& visitor) override;
    };
}
