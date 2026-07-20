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
        else {
            Token* identifier = Consume(TokenType::IDENTIFIER);
            type = std::make_unique<IdentifierExpr>(identifier->value);
            while (CurrentToken()) {
                if (CurrentToken()->value == "<") {
                    size_t close = 0;
                    if (!IsTemplateArgumentList(pos, &close)) break;
                    type = std::make_unique<UserTypeExpr>(std::move(type));
                    type = ParseTemplateExpr(std::move(type));
                }
                else if (CurrentToken()->type == TokenType::DELIMITER &&
                    CurrentToken()->value == ".") {
                    type = ParseMemberAccess(std::move(type));
                }
                else break;
            }
        }

        std::vector<std::unique_ptr<Expression>> arguments;
        if (CurrentToken() && CurrentToken()->type == TokenType::BRACKET && CurrentToken()->value == "(")
            arguments = ParseArguments();
        return std::make_unique<ConstructorCallExpr>(std::move(type), std::move(arguments), raw);
    }

    std::unique_ptr<ConstructorDeclStmt> Parser::ParseConstructor()
    {
        std::vector<Token> modifiers = this->modifiers;
        std::vector<Attribute> attributes = this->attributes;
        Token* name = Consume(TokenType::IDENTIFIER);

        std::vector<std::unique_ptr<VarDeclExpr>> parameters = ParseParameters();

        std::unique_ptr<Statement> body = ParseStatement();
        auto stmt = std::make_unique<ConstructorDeclStmt>(std::make_unique<Token>(*name), std::move(parameters), std::move(body));
        stmt->modifiers = modifiers;
        stmt->attributes = std::move(attributes);
        return stmt;
    }
}
