#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<ConstructorCallExpr> Parser::ParseConstructorCall()
    {
        Consume(TokenType::KEYWORD, "new");
        bool raw = false;
        if (CurrentToken() && CurrentToken()->type == TokenType::KEYWORD && CurrentToken()->value == "raw") {
            Consume(TokenType::KEYWORD, "raw");
            raw = true;
        }

        std::unique_ptr<Expression> type;
        if (CurrentToken() && CurrentToken()->type == TokenType::KEYWORD && IsPrimitiveType(CurrentToken()->value))
            type = ParsePrimitiveType();
        else
            type = ParseIdentifierExpr(true);

        std::vector<std::unique_ptr<Expression>> arguments;
        if (CurrentToken() && CurrentToken()->type == TokenType::BRACKET && CurrentToken()->value == "(")
            arguments = ParseArguments();
        return std::make_unique<ConstructorCallExpr>(std::move(type), std::move(arguments), raw);
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
