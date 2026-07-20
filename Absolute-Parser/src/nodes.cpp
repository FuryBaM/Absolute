#include "parser_pch.h"
#include "nodes.h"
#include "expression_visitor.h"
#include "statement_visitor.h"

namespace Absolute {
    void PrimitiveTypeExpr::Accept(ExpressionVisitor& visitor) {
        visitor.Visit(this);
    }

    void UserTypeExpr::Accept(ExpressionVisitor& visitor) {
        visitor.Visit(this);
    }

    void PointerTypeExpr::Accept(ExpressionVisitor& visitor) {
        visitor.Visit(this);
    }

    void ArrayTypeExpr::Accept(ExpressionVisitor& visitor) {
        visitor.Visit(this);
    }

    void IdentifierExpr::Accept(ExpressionVisitor& visitor) {
        visitor.Visit(this);
    }

    void FunctionCallExpr::Accept(ExpressionVisitor& visitor) {
        visitor.Visit(this);
    }

    void ArrayAccessExpr::Accept(ExpressionVisitor& visitor) {
        visitor.Visit(this);
    }

    void SliceExpr::Accept(ExpressionVisitor& visitor) {
        visitor.Visit(this);
    }

    IdentifierExpr* GetIdentifierBase(Expression* base) {
        if (!base) return nullptr;

        BaseIdentifierVisitor visitor;
        base->Accept(visitor); // Çàïóñêàåì âèçèòîð

        return visitor.identifierExpr; // Âîçâðàùàåì íàéäåííûé IdentifierExpr
    }

    IdentifierExpr* ArrayAccessExpr::GetIdentifier() {
        return GetIdentifierBase(base.get());
    }

    IdentifierExpr* FunctionCallExpr::GetIdentifier() {
        return GetIdentifierBase(base.get());
    }

    IdentifierExpr* MemberAccessExpr::GetIdentifier() {
        return GetIdentifierBase(base.get());
    }

    IdentifierExpr* TemplateExpr::GetIdentifier() {
        return GetIdentifierBase(base.get());
    }

    void BinaryExpr::Accept(ExpressionVisitor& visitor) {
        visitor.Visit(this);
    }

    void TernaryExpr::Accept(ExpressionVisitor& visitor) {
        visitor.Visit(this);
    }

    void NullExpr::Accept(ExpressionVisitor& visitor) {
        visitor.Visit(this);
    }

    void BooleanLiteralExpr::Accept(ExpressionVisitor& visitor) {
        visitor.Visit(this);
    }

    void NumberLiteralExpr::Accept(ExpressionVisitor& visitor) {
        visitor.Visit(this);
    }

    void StringLiteralExpr::Accept(ExpressionVisitor& visitor) {
        visitor.Visit(this);
    }

    void CharLiteralExpr::Accept(ExpressionVisitor& visitor) {
        visitor.Visit(this);
    }

    void ArrayExpr::Accept(ExpressionVisitor& visitor) {
        visitor.Visit(this);
    }

    void AssignmentExpr::Accept(ExpressionVisitor& visitor) {
        visitor.Visit(this);
    }

    void VarDeclExpr::Accept(ExpressionVisitor& visitor) {
        visitor.Visit(this);
    }

    void MemberAccessExpr::Accept(ExpressionVisitor& visitor) {
        visitor.Visit(this);
    }

    void CastExpr::Accept(ExpressionVisitor& visitor) {
        visitor.Visit(this);
    }

    void ConstructorCallExpr::Accept(ExpressionVisitor& visitor) {
        visitor.Visit(this);
    }

    void DestructorCallExpr::Accept(ExpressionVisitor& visitor) {
        visitor.Visit(this);
    }

    void InstanceDeclExpr::Accept(ExpressionVisitor& visitor) {
        visitor.Visit(this);
    }

    void PrefixUnaryExpr::Accept(ExpressionVisitor& visitor) {
        visitor.Visit(this);
    }

    void PostfixUnaryExpr::Accept(ExpressionVisitor& visitor) {
        visitor.Visit(this);
    }

    void TemplateExpr::Accept(ExpressionVisitor& visitor) {
        visitor.Visit(this);
    }

    void SingleStatement::Accept(StatementVisitor& visitor) {
        visitor.Visit(this);
    }

    void CompoundStmt::Accept(StatementVisitor& visitor) {
        visitor.Visit(this);
    }

    void FunctionCallStmt::Accept(StatementVisitor& visitor) {
        visitor.Visit(this);
    }

    void FunctionDeclStmt::Accept(StatementVisitor& visitor) {
        visitor.Visit(this);
    }

    void ReturnStmt::Accept(StatementVisitor& visitor) {
        visitor.Visit(this);
    }

    void AssignmentStmt::Accept(StatementVisitor& visitor) {
        visitor.Visit(this);
    }

    void VarDeclStmt::Accept(StatementVisitor& visitor) {
        visitor.Visit(this);
    }

    void StructDeclStmt::Accept(StatementVisitor& visitor) {
        visitor.Visit(this);
    }

    void ClassDeclStmt::Accept(StatementVisitor& visitor) {
        visitor.Visit(this);
    }

    void InterfaceDeclStmt::Accept(StatementVisitor& visitor) {
        visitor.Visit(this);
    }

    void ConstructorDeclStmt::Accept(StatementVisitor& visitor) {
        visitor.Visit(this);
    }

    void EnumDeclStmt::Accept(StatementVisitor& visitor) {
        visitor.Visit(this);
    }

    void GroupDeclStmt::Accept(StatementVisitor& visitor) {
        visitor.Visit(this);
    }

    void IfStmt::Accept(StatementVisitor& visitor) {
        visitor.Visit(this);
    }

    void SwitchStmt::Accept(StatementVisitor& visitor) {
        visitor.Visit(this);
    }

    void ThrowStmt::Accept(StatementVisitor& visitor) {
        visitor.Visit(this);
    }

    void TryStmt::Accept(StatementVisitor& visitor) {
        visitor.Visit(this);
    }

    void DeferStmt::Accept(StatementVisitor& visitor) {
        visitor.Visit(this);
    }

    void ForStmt::Accept(StatementVisitor& visitor) {
        visitor.Visit(this);
    }

    void WhileStmt::Accept(StatementVisitor& visitor) {
        visitor.Visit(this);
    }

    void DoWhileStmt::Accept(StatementVisitor& visitor) {
        visitor.Visit(this);
    }

    void ForEachStmt::Accept(StatementVisitor& visitor) {
        visitor.Visit(this);
    }

    void ContinueStmt::Accept(StatementVisitor& visitor) {
        visitor.Visit(this);
    }

    void BreakStmt::Accept(StatementVisitor& visitor) {
        visitor.Visit(this);
    }

    void ImportStmt::Accept(StatementVisitor& visitor) {
        visitor.Visit(this);
    }

    void NamespaceDeclStmt::Accept(StatementVisitor& visitor) {
        visitor.Visit(this);
    }

    void OpaquePluginStmt::Accept(StatementVisitor& visitor) {
        visitor.Visit(this);
    }
}
