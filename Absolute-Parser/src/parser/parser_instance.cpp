#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<InstanceDeclExpr> Parser::ParseInstanceDeclExpr()
    {
        std::unique_ptr<UserTypeExpr> constructType = std::make_unique<UserTypeExpr>(ParseIdentifierExpr(true));
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
        std::vector<Attribute> attributes = this->attributes;
        std::unique_ptr<InstanceDeclExpr> instanceDecl = ParseInstanceDeclExpr();
        instanceDecl->isConst = std::any_of(modifiers.begin(), modifiers.end(),
            [](const Token& modifier) { return modifier.value == "const"; });
        instanceDecl->isStatic = std::any_of(modifiers.begin(), modifiers.end(),
            [](const Token& modifier) { return modifier.value == "static"; });
        auto stmt = std::make_unique<SingleStatement>(std::move(instanceDecl));
        stmt->modifiers = modifiers;
        stmt->attributes = std::move(attributes);
        return stmt;
    }
}
