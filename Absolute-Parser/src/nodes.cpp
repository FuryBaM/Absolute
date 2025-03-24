#include "pch.h"
#include "nodes.h"
#include "expression_visitor.h"

void IdentifierExpr::Accept(ExpressionVisitor& visitor) {
    visitor.Visit(this);
}

void FunctionCallExpr::Accept(ExpressionVisitor& visitor) {
    visitor.Visit(this);
}

void ArrayAccessExpr::Accept(ExpressionVisitor& visitor) {
    visitor.Visit(this);
}

IdentifierExpr* GetIdentifierBase(Expression* base) {
    if (!base) return nullptr;

    BaseIdentifierVisitor visitor;
    base->Accept(visitor); // Запускаем визитор

    return visitor.identifierExpr; // Возвращаем найденный IdentifierExpr
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

