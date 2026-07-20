#pragma once

#include "expression_visitor.h"
#include "statement_visitor.h"

#include <memory>
#include <string>

namespace Absolute {
    class Analyzer;

    class CodeGenerator final : public ExpressionVisitor, public StatementVisitor {
    public:
        explicit CodeGenerator(const Analyzer* analyzer = nullptr);
        ~CodeGenerator() override;

        CodeGenerator(const CodeGenerator&) = delete;
        CodeGenerator& operator=(const CodeGenerator&) = delete;

        std::string Generate(Program& program, const std::string& moduleName);
        void GenerateObject(Program& program, const std::string& moduleName, const std::string& outputPath);

        void Visit(PrimitiveTypeExpr* expr) override;
        void Visit(UserTypeExpr* expr) override;
        void Visit(PointerTypeExpr* expr) override;
        void Visit(ArrayTypeExpr* expr) override;
        void Visit(IdentifierExpr* expr) override;
        void Visit(FunctionCallExpr* expr) override;
        void Visit(ArrayAccessExpr* expr) override;
        void Visit(SliceExpr* expr) override;
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
        void Visit(InterfaceDeclStmt* stmt) override;
        void Visit(ConstructorDeclStmt* stmt) override;
        void Visit(EnumDeclStmt* stmt) override;
        void Visit(GroupDeclStmt* stmt) override;
        void Visit(IfStmt* stmt) override;
        void Visit(SwitchStmt* stmt) override;
        void Visit(ThrowStmt* stmt) override;
        void Visit(TryStmt* stmt) override;
        void Visit(ForStmt* stmt) override;
        void Visit(WhileStmt* stmt) override;
        void Visit(DoWhileStmt* stmt) override;
        void Visit(ForEachStmt* stmt) override;
        void Visit(ContinueStmt* stmt) override;
        void Visit(BreakStmt* stmt) override;
        void Visit(ImportStmt* stmt) override;
        void Visit(NamespaceDeclStmt* stmt) override;
        void Visit(OpaquePluginStmt* stmt) override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
