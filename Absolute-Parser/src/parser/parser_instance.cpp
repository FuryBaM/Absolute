#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<InstanceDeclExpr> Parser::ParseInstanceDeclExpr()
    {
        std::unique_ptr<UserTypeExpr> constructType = std::make_unique<UserTypeExpr>(ParseIdentifierExpr());
        std::unique_ptr<Expression> identifierName = ParsePrimaryExpr();
        std::unique_ptr<Expression> initializer = nullptr;
        if (CurrentToken()->value == "=") {
            Consume(TokenType::OPERATOR, "=");
            initializer = ParseExpression();
        }
        return std::make_unique<InstanceDeclExpr>(std::move(constructType), std::move(identifierName), std::move(initializer));
    }

    std::unique_ptr<SingleStatement> Parser::ParseInstanceDeclStmt()
    {
        std::vector<Token> modifiers = this->modifiers;
        std::unique_ptr<InstanceDeclExpr> instanceDecl = ParseInstanceDeclExpr();
        auto stmt = std::make_unique<SingleStatement>(std::move(instanceDecl));
        stmt->modifiers = modifiers;
        return stmt;
    }
}