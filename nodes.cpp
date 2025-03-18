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

IdentifierExpr* ArrayAccessExpr::GetIdentifier() {
    if (!base) return nullptr;

    BaseIdentifierVisitor visitor;
    base->Accept(visitor); // Запускаем визитор

    return visitor.identifierExpr; // Возвращаем найденный IdentifierExpr
}

IdentifierExpr* FunctionCallExpr::GetIdentifier() {
    if (!base) return nullptr;

    BaseIdentifierVisitor visitor;
    base->Accept(visitor); // Запускаем визитор

    return visitor.identifierExpr; // Возвращаем найденный IdentifierExpr
}

IdentifierExpr* MemberAccessExpr::GetIdentifier() {
    if (!base) return nullptr;

    BaseIdentifierVisitor visitor;
    base->Accept(visitor); // Запускаем визитор

    return visitor.identifierExpr; // Возвращаем найденный IdentifierExpr
}

void BinaryExpr::Accept(ExpressionVisitor& visitor) {
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

void InstanceDeclExpr::Accept(ExpressionVisitor& visitor) {
    visitor.Visit(this);
}