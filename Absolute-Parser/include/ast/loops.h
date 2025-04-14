#pragma once

namespace Absolute {
    struct ForStmt : Statement {
        std::vector<std::unique_ptr<Expression>> init;  // Может быть несколько инициализаций
        std::unique_ptr<Expression> condition;
        std::vector<std::unique_ptr<Expression>> update; // Может быть несколько обновлений
        std::unique_ptr<Statement> body;

        ForStmt(std::vector<std::unique_ptr<Expression>> init,
            std::unique_ptr<Expression> condition,
            std::vector<std::unique_ptr<Expression>> update,
            std::unique_ptr<Statement> body)
            : init(std::move(init)), condition(std::move(condition)),
            update(std::move(update)), body(std::move(body)) {
        }

        void print(int indent = 0) override {
            std::cout << std::string(indent, ' ') << "For statement:\n";

            for (const auto& expr : init) {
                if (expr) expr->print(indent + 1);
            }

            if (condition) condition->print(indent + 1);

            for (const auto& expr : update) {
                if (expr) expr->print(indent + 1);
            }

            if (body) body->print(indent + 1);
        }
    };

    struct WhileStmt : Statement {
        std::unique_ptr<Expression> condition;
        std::unique_ptr<Statement> body;

        WhileStmt(std::unique_ptr<Expression> condition, std::unique_ptr<Statement> body)
            : condition(std::move(condition)), body(std::move(body)) {
        }
        void print(int indent = 0) override {
            std::cout << std::string(indent, ' ') << "While statement:\n";
            if (condition) condition->print(indent + 1);
            if (body) body->print(indent + 1);
        }
    };

    struct DoWhileStmt : Statement {
        std::unique_ptr<Statement> body;
        std::unique_ptr<Expression> condition;

        DoWhileStmt(std::unique_ptr<Statement> body, std::unique_ptr<Expression> condition)
            : body(std::move(body)), condition(std::move(condition)) {
        }
        void print(int indent = 0) override {
            std::cout << std::string(indent, ' ') << "Do-while statement:\n";
            if (body) body->print(indent + 1);
            if (condition) condition->print(indent + 1);
        }
    };

    struct ForEachStmt : Statement {
        std::unique_ptr<VarDeclExpr> var;
        std::unique_ptr<Expression> iterable;
        std::unique_ptr<Statement> body;
        ForEachStmt(std::unique_ptr<VarDeclExpr> var, std::unique_ptr<Expression> iterable, std::unique_ptr<Statement> body)
            : var(std::move(var)), iterable(std::move(iterable)), body(std::move(body)) {
        };
        void print(int indent = 0) override {
            std::cout << std::string(indent, ' ') << "For-each statement:\n";
            if (var) var->print(indent + 1);
            if (iterable) iterable->print(indent + 1);
            if (body) body->print(indent + 1);
        }
    };

    struct ContinueStmt : Statement {
        explicit ContinueStmt() {}
        void print(int indent = 0) override {
            std::cout << std::string(indent, ' ') << "Continue statement" << "\n";
        }
    };

    struct BreakStmt : Statement {
        explicit BreakStmt() {}
        void print(int indent = 0) override {
            std::cout << std::string(indent, ' ') << "Break statement" << "\n";
        }
    };
}