#pragma once
#include "variable.h"
#include "statement_visitor.h"
#include <unordered_set>

namespace Absolute {
    class ANALYZER_API SymbolTable {
        std::vector<std::unordered_set<std::string>> localScopes;

    public:
        std::unordered_map<std::string, std::unique_ptr<Type>> variables;

        void EnterScope() {
            localScopes.emplace_back();
        }

        void ExitScope() {
            if (!localScopes.empty()) localScopes.pop_back();
        }

        bool Declare(std::string name, std::unique_ptr<Type> type) {
            if (!localScopes.empty()) {
                return localScopes.back().insert(std::move(name)).second;
            }

            return variables.emplace(std::move(name), std::move(type)).second;
        }
    };

    class ANALYZER_API Analyzer : public ExpressionVisitor, public StatementVisitor {
        std::vector<Program*> programs;
        SymbolTable table;

    public:
        explicit Analyzer(std::vector<Program*> programs)
            : programs(std::move(programs)) {
        }

        void Analyze() {
            for (Program* program : programs) {
                if (program) AnalyzeProgram(*program);
            }
        }
        void PrintVariables() {
            for (const auto& [name, varType] : table.variables) {
                std::cout << name << " " << varType->GetName() << "\n";
            }
        }

        void Visit(PrimitiveTypeExpr* expr) override;
        void Visit(UserTypeExpr* expr) override;
        void Visit(IdentifierExpr* expr) override;
        void Visit(FunctionCallExpr* expr) override;
        void Visit(ArrayAccessExpr* expr) override;
        void Visit(BinaryExpr* expr) override;
        void Visit(TernaryExpr* expr) override;
        void Visit(NullExpr* expr) override;
        void Visit(BooleanLiteralExpr* expr) override;
        void Visit(NumberLiteralExpr* expr) override;
        void Visit(StringLiteralExpr* expr) override;
        void Visit(CharLiteralExpr* expr) override;
        void Visit(ArrayExpr* expr) override;
        void Visit(AssignmentExpr* expr) override;
        void Visit(VarDeclExpr* expr) override;
        void Visit(MemberAccessExpr* expr) override;
        void Visit(CastExpr* expr) override;
        void Visit(ConstructorCallExpr* expr) override;
        void Visit(DestructorCallExpr* expr) override;
        void Visit(InstanceDeclExpr* expr) override;
        void Visit(PrefixUnaryExpr* expr) override;
        void Visit(PostfixUnaryExpr* expr) override;
        void Visit(TemplateExpr* expr) override;

        void Visit(SingleStatement* stmt) override;
        void Visit(CompoundStmt* stmt) override;
        void Visit(FunctionCallStmt* stmt) override;
        void Visit(FunctionDeclStmt* stmt) override;
        void Visit(ReturnStmt* stmt) override;
        void Visit(AssignmentStmt* stmt) override;
        void Visit(VarDeclStmt* stmt) override;
        void Visit(StructDeclStmt* stmt) override;
        void Visit(ClassDeclStmt* stmt) override;
        void Visit(ConstructorDeclStmt* stmt) override;
        void Visit(EnumDeclStmt* stmt) override;
        void Visit(GroupDeclStmt* stmt) override;
        void Visit(IfStmt* stmt) override;
        void Visit(ForStmt* stmt) override;
        void Visit(WhileStmt* stmt) override;
        void Visit(DoWhileStmt* stmt) override;
        void Visit(ForEachStmt* stmt) override;
        void Visit(ContinueStmt* stmt) override;
        void Visit(BreakStmt* stmt) override;

    private:
        void AnalyzeProgram(Program& program);
    };
}
