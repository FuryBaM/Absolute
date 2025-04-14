#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<ConstructorCallExpr> Parser::ParseConstructorCall()
    {
        Consume(TokenType::KEYWORD, "new");
        std::unique_ptr<Expression> identifier = ParseIdentifierExpr();
        return std::make_unique<ConstructorCallExpr>(std::move(identifier));
    }

    std::unique_ptr<ConstructorDeclStmt> Parser::ParseConstructor()
    {
        std::vector<Token> modifiers = this->modifiers;
        Token* name = Consume(TokenType::IDENTIFIER);

        std::vector<std::unique_ptr<VarDeclExpr>> parameters = ParseParameters();

        std::unique_ptr<Statement> body = ParseStatement();
        auto stmt = std::make_unique<ConstructorDeclStmt>(std::make_unique<Token>(*name), std::move(parameters), std::move(body));
        stmt->modifiers = modifiers;
        return stmt;
    }
}