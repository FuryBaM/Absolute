#include "analyzer_pch.h"
#include "analyzer.h"

namespace Absolute {
    namespace {
        class PrimitiveTypeVisitor final : public BaseIdentifierVisitor {
        public:
            std::optional<PrimitiveTypeEnum> type;

            void Visit(PrimitiveTypeExpr* expr) override {
                type = PrimitiveStringToEnum(expr->type);
            }
        };

        class SymbolScope final {
            SymbolTable& table;

        public:
            explicit SymbolScope(SymbolTable& table) : table(table) {
                table.EnterScope();
            }

            ~SymbolScope() {
                table.ExitScope();
            }
        };

        template <typename T, typename Visitor>
        void AcceptIfPresent(const std::unique_ptr<T>& node, Visitor& visitor) {
            if (node) node->Accept(visitor);
        }

        template <typename T, typename Visitor>
        void AcceptAll(const std::vector<std::unique_ptr<T>>& nodes, Visitor& visitor) {
            for (const auto& node : nodes) {
                AcceptIfPresent(node, visitor);
            }
        }
    }

    void Analyzer::AnalyzeProgram(Program& program) {
        AcceptAll(program.statements, *this);
    }

    void Analyzer::Visit(PrimitiveTypeExpr* expr) {
        (void)expr;
    }

    void Analyzer::Visit(UserTypeExpr* expr) {
        AcceptIfPresent(expr->typeExpr, *this);
    }

    void Analyzer::Visit(IdentifierExpr* expr) {
        (void)expr;
    }

    void Analyzer::Visit(FunctionCallExpr* expr) {
        AcceptIfPresent(expr->base, *this);
        AcceptAll(expr->arguments, *this);
    }

    void Analyzer::Visit(ArrayAccessExpr* expr) {
        AcceptIfPresent(expr->base, *this);
        AcceptAll(expr->indexes, *this);
    }

    void Analyzer::Visit(BinaryExpr* expr) {
        AcceptIfPresent(expr->left, *this);
        AcceptIfPresent(expr->right, *this);
    }

    void Analyzer::Visit(TernaryExpr* expr) {
        AcceptIfPresent(expr->condition, *this);
        AcceptIfPresent(expr->trueExpr, *this);
        AcceptIfPresent(expr->falseExpr, *this);
    }

    void Analyzer::Visit(NullExpr* expr) {
        (void)expr;
    }

    void Analyzer::Visit(BooleanLiteralExpr* expr) {
        (void)expr;
    }

    void Analyzer::Visit(NumberLiteralExpr* expr) {
        (void)expr;
    }

    void Analyzer::Visit(StringLiteralExpr* expr) {
        (void)expr;
    }

    void Analyzer::Visit(CharLiteralExpr* expr) {
        (void)expr;
    }

    void Analyzer::Visit(ArrayExpr* expr) {
        AcceptAll(expr->sizes, *this);
        AcceptAll(expr->values, *this);
    }

    void Analyzer::Visit(AssignmentExpr* expr) {
        std::cout << "It is assignment\n";
        AcceptIfPresent(expr->target, *this);
        AcceptIfPresent(expr->value, *this);
    }

    void Analyzer::Visit(VarDeclExpr* expr) {
        BaseIdentifierVisitor identifierVisitor;
        AcceptIfPresent(expr->name, identifierVisitor);

        PrimitiveTypeVisitor typeVisitor;
        AcceptIfPresent(expr->type, typeVisitor);

        const IdentifierExpr* identifier = identifierVisitor.identifierExpr;
        if (!identifier) {
            std::cerr << "Error: expected identifier of primitive type.\n";
        }
        else if (!typeVisitor.type) {
            std::cerr << "Error: expected primitive type, but received another.\n";
        }
        else if (!table.Declare(
            identifier->name,
            std::make_unique<PrimitiveType>(*typeVisitor.type))) {
            std::cerr << "Error: variable '" << identifier->name
                << "' already exists in this scope!\n";
        }

        AcceptIfPresent(expr->value, *this);
    }

    void Analyzer::Visit(MemberAccessExpr* expr) {
        AcceptIfPresent(expr->base, *this);
    }

    void Analyzer::Visit(CastExpr* expr) {
        AcceptIfPresent(expr->typeName, *this);
        AcceptIfPresent(expr->base, *this);
    }

    void Analyzer::Visit(ConstructorCallExpr* expr) {
        AcceptIfPresent(expr->constructName, *this);
    }

    void Analyzer::Visit(DestructorCallExpr* expr) {
        AcceptIfPresent(expr->target, *this);
    }

    void Analyzer::Visit(InstanceDeclExpr* expr) {
        AcceptIfPresent(expr->constructType, *this);
        AcceptIfPresent(expr->identifierName, *this);
        AcceptIfPresent(expr->value, *this);
    }

    void Analyzer::Visit(PrefixUnaryExpr* expr) {
        AcceptIfPresent(expr->operand, *this);
    }

    void Analyzer::Visit(PostfixUnaryExpr* expr) {
        AcceptIfPresent(expr->operand, *this);
    }

    void Analyzer::Visit(TemplateExpr* expr) {
        AcceptIfPresent(expr->base, *this);
        AcceptAll(expr->types, *this);
    }

    void Analyzer::Visit(SingleStatement* stmt) {
        AcceptIfPresent(stmt->expr, *this);
    }

    void Analyzer::Visit(CompoundStmt* stmt) {
        SymbolScope scope(table);
        AcceptAll(stmt->statements, *this);
    }

    void Analyzer::Visit(FunctionCallStmt* stmt) {
        AcceptIfPresent(stmt->value, *this);
    }

    void Analyzer::Visit(FunctionDeclStmt* stmt) {
        SymbolScope scope(table);
        AcceptAll(stmt->parameters, *this);
        AcceptIfPresent(stmt->body, *this);
    }

    void Analyzer::Visit(ReturnStmt* stmt) {
        AcceptIfPresent(stmt->expr, *this);
    }

    void Analyzer::Visit(AssignmentStmt* stmt) {
        AcceptIfPresent(stmt->expr, *this);
    }

    void Analyzer::Visit(VarDeclStmt* stmt) {
        AcceptIfPresent(stmt->expr, *this);
    }

    void Analyzer::Visit(StructDeclStmt* stmt) {
        SymbolScope scope(table);
        AcceptAll(stmt->members, *this);
    }

    void Analyzer::Visit(ClassDeclStmt* stmt) {
        SymbolScope scope(table);
        AcceptIfPresent(stmt->body, *this);
    }

    void Analyzer::Visit(ConstructorDeclStmt* stmt) {
        SymbolScope scope(table);
        AcceptAll(stmt->parameters, *this);
        AcceptIfPresent(stmt->body, *this);
    }

    void Analyzer::Visit(EnumDeclStmt* stmt) {
        (void)stmt;
    }

    void Analyzer::Visit(GroupDeclStmt* stmt) {
        AcceptAll(stmt->enums, *this);
        AcceptAll(stmt->subgroups, *this);
    }

    void Analyzer::Visit(IfStmt* stmt) {
        for (auto& branch : stmt->branches) {
            AcceptIfPresent(branch.condition, *this);
            AcceptIfPresent(branch.body, *this);
        }
        AcceptIfPresent(stmt->elseBranch, *this);
    }

    void Analyzer::Visit(ForStmt* stmt) {
        SymbolScope scope(table);
        AcceptAll(stmt->init, *this);
        AcceptIfPresent(stmt->condition, *this);
        AcceptAll(stmt->update, *this);
        AcceptIfPresent(stmt->body, *this);
    }

    void Analyzer::Visit(WhileStmt* stmt) {
        SymbolScope scope(table);
        AcceptIfPresent(stmt->condition, *this);
        AcceptIfPresent(stmt->body, *this);
    }

    void Analyzer::Visit(DoWhileStmt* stmt) {
        SymbolScope scope(table);
        AcceptIfPresent(stmt->body, *this);
        AcceptIfPresent(stmt->condition, *this);
    }

    void Analyzer::Visit(ForEachStmt* stmt) {
        SymbolScope scope(table);
        AcceptIfPresent(stmt->var, *this);
        AcceptIfPresent(stmt->iterable, *this);
        AcceptIfPresent(stmt->body, *this);
    }

    void Analyzer::Visit(ContinueStmt* stmt) {
        (void)stmt;
    }

    void Analyzer::Visit(BreakStmt* stmt) {
        (void)stmt;
    }
}
