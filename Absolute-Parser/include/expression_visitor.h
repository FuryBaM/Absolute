#pragma once
#include "syntax_tree.h"
namespace Absolute {
    class ExpressionVisitor {
    public:
        virtual void Visit(PrimitiveTypeExpr* expr) = 0;
        virtual void Visit(UserTypeExpr* expr) = 0;
        virtual void Visit(IdentifierExpr* expr) = 0;
        virtual void Visit(FunctionCallExpr* expr) = 0;
        virtual void Visit(ArrayAccessExpr* expr) = 0;
        virtual void Visit(BinaryExpr* expr) = 0;
        virtual void Visit(TernaryExpr* expr) = 0;
        virtual void Visit(NullExpr* expr) = 0;
        virtual void Visit(BooleanLiteralExpr* expr) = 0;
        virtual void Visit(NumberLiteralExpr* expr) = 0;
        virtual void Visit(StringLiteralExpr* expr) = 0;
        virtual void Visit(CharLiteralExpr* expr) = 0;
        virtual void Visit(ArrayExpr* expr) = 0;
        virtual void Visit(AssignmentExpr* expr) = 0;
        virtual void Visit(VarDeclExpr* expr) = 0;
        virtual void Visit(MemberAccessExpr* expr) = 0;
        virtual void Visit(CastExpr* expr) = 0;
        virtual void Visit(ConstructorCallExpr* expr) = 0;
        virtual void Visit(DestructorCallExpr* expr) = 0;
        virtual void Visit(InstanceDeclExpr* expr) = 0;
        virtual void Visit(PrefixUnaryExpr* expr) = 0;
        virtual void Visit(PostfixUnaryExpr* expr) = 0;
        virtual void Visit(TemplateExpr* expr) = 0;
    };

    class BaseIdentifierVisitor : public ExpressionVisitor {
    public:
        IdentifierExpr* identifierExpr = nullptr;

        void Visit(PrimitiveTypeExpr* expr) override {
            expr->Accept(*this);
        }

        void Visit(UserTypeExpr* expr) override {
            expr->Accept(*this);
        }

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

        void Visit(TernaryExpr* expr) override {
            expr->condition->Accept(*this);
            expr->trueExpr->Accept(*this);
            expr->falseExpr->Accept(*this);
        }

        void Visit(NullExpr* expr) override {
            expr->Accept(*this);
        }

        void Visit(BooleanLiteralExpr* expr) override {
            expr->Accept(*this);
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

        void Visit(CastExpr* expr) override {
            if (expr->base) {
                expr->base->Accept(*this); // Обход базового выражения
            }
        }

        void Visit(ConstructorCallExpr* expr) override {
            expr->Accept(*this);
        }

        void Visit(DestructorCallExpr* expr) override {
            expr->Accept(*this);
        }

        void Visit(InstanceDeclExpr* expr) override {
            expr->Accept(*this);
        }

        void Visit(PrefixUnaryExpr* expr) override {
            expr->operand->Accept(*this);
        }

        void Visit(PostfixUnaryExpr* expr) override {
            expr->operand->Accept(*this);
        }

        void Visit(TemplateExpr* expr) override {
            expr->base->Accept(*this);
        }
    };
}
