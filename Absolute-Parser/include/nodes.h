#ifndef ABSOLUTE_PARSER_NODES_H
#define ABSOLUTE_PARSER_NODES_H

#include <algorithm>

namespace Absolute {
    class ExpressionVisitor;
    class StatementVisitor;

    enum class AttributeValueKind {
        Identifier,
        String,
        Number,
        Character,
        Boolean
    };

    struct AttributeValue {
        AttributeValueKind kind = AttributeValueKind::Identifier;
        std::string text;
    };

    struct AttributeArgument {
        std::string name;
        AttributeValue value;
    };

    struct Attribute {
        std::string name;
        std::vector<AttributeArgument> arguments;
        int line = 0;
        int column = 0;
    };

    struct ASTNode {
        std::string sourceFile;
        int line = 0;
        int column = 0;

        virtual ~ASTNode() = default;

        virtual void print(int indent = 0) {
            std::cout << ToString(indent) << "\n";
        }

        virtual std::string ToString(int indent = 0) const {
            return std::string(indent, ' ') + "ASTNode";
        }
    };

    struct Expression : ASTNode {
        bool isOutArgument = false;
        bool isRefArgument = false;
        virtual void Accept(ExpressionVisitor& visitor) = 0;
    };

    struct Statement : ASTNode {
        std::vector<Token> modifiers;
        std::vector<Attribute> attributes;

        const Attribute* FindAttribute(const std::string& name) const {
            const auto found = std::find_if(attributes.begin(), attributes.end(),
                [&](const Attribute& attribute) { return attribute.name == name; });
            return found == attributes.end() ? nullptr : &*found;
        }

        virtual void Accept(StatementVisitor& visitor) = 0;
    };

    struct Program : ASTNode {
        std::vector<std::unique_ptr<Statement>> statements;
        explicit Program(std::vector<std::unique_ptr<Statement>> statements)
            : statements(std::move(statements)) {
        }

        void print(int = 0) override {
            for (const auto& stmt : statements) {
                if (stmt == nullptr) {
                    continue;
                }
                stmt->print();
            }
        }
    };

    struct SingleStatement : Statement {
        std::unique_ptr<Expression> expr;
        explicit SingleStatement(std::unique_ptr<Expression> expr)
            : expr(std::move(expr)) {
        }

        std::string ToString(int indent = 0) const override {
            std::string result = std::string(indent, ' ') + "Single statement";

            if (!modifiers.empty()) {
                result += " [";
                for (size_t i = 0; i < modifiers.size(); i++) {
                    if (i > 0) result += ", ";
                    result += modifiers[i].value;
                }
                result += "]";
            }

            result += ":\n";

            if (expr) {
                result += expr->ToString(indent + 1);
            }

            return result;
        }

        void print(int indent = 0) override {
            std::cout << ToString(indent) << "\n";
        }

        void Accept(StatementVisitor& visitor) override;
    };

    struct CompoundStmt : Statement {
        std::vector<std::unique_ptr<Statement>> statements;
        explicit CompoundStmt(std::vector<std::unique_ptr<Statement>> statements)
            : statements(std::move(statements)) {
        }
        void print(int indent = 0) override {
            std::cout << std::string(indent, ' ') << "Compound statement: " << "\n";
            for (const auto& stmt : statements) {
                if (stmt)
                    stmt->print(indent + 1);
            }
        }


        void Accept(StatementVisitor& visitor) override;
    };
}

#endif // ABSOLUTE_PARSER_NODES_H
