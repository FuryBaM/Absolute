#pragma once
#include "nodes.h"
class ExpressionVisitor {
public:
    virtual void Visit(IdentifierExpr* expr) = 0;
    virtual void Visit(FunctionCallExpr* expr) = 0;
    virtual void Visit(ArrayAccessExpr* expr) = 0;
    virtual void Visit(BinaryExpr* expr) = 0;
    virtual void Visit(NumberLiteralExpr* expr) = 0;
    virtual void Visit(StringLiteralExpr* expr) = 0;
    virtual void Visit(CharLiteralExpr* expr) = 0;
    virtual void Visit(ArrayExpr* expr) = 0;
    virtual void Visit(AssignmentExpr* expr) = 0;
    virtual void Visit(VarDeclExpr* expr) = 0;
    virtual void Visit(MemberAccessExpr* expr) = 0;
    virtual void Visit(ConstructorCallExpr* expr) = 0;
    virtual void Visit(InstanceDeclExpr* expr) = 0;
};

class BaseIdentifierVisitor : public ExpressionVisitor {
public:
    IdentifierExpr* identifierExpr = nullptr;

    void Visit(IdentifierExpr* expr) override {
        identifierExpr = expr;
    }

    void Visit(FunctionCallExpr* expr) override {
        expr->base->Accept(*this);
    }

    void Visit(ArrayAccessExpr* expr) override {
        expr->base->Accept(*this);
    }

    void Visit(BinaryExpr* expr) override {
        expr->left->Accept(*this);  // Рекурсивно ищем в левом поддереве
        expr->right->Accept(*this); // Рекурсивно ищем в правом поддереве
    }

    void Visit(NumberLiteralExpr* expr) override {
        expr->Accept(*this);
    }

    void Visit(StringLiteralExpr* expr) override {
        expr->Accept(*this);
    }

    void Visit(CharLiteralExpr* expr) override {
        expr->Accept(*this);
    }

    void Visit(ArrayExpr* expr) override {
        expr->Accept(*this);
    }

    void Visit(AssignmentExpr* expr) override {
        expr->Accept(*this);
    }

    void Visit(VarDeclExpr* expr) override {
        expr->Accept(*this);
    }

    void Visit(MemberAccessExpr* expr) override {
        if (expr->base) {
            expr->base->Accept(*this); // Обход базового выражения
        }
    }

    void Visit(ConstructorCallExpr* expr) override {
        expr->Accept(*this);
    }

    void Visit(InstanceDeclExpr* expr) override {
        expr->Accept(*this);
    }
};

