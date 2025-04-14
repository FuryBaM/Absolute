#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<FunctionCallExpr> Parser::ParseFunctionCallExpr(std::unique_ptr<Expression> base)
    {
        Token* bracket = CurrentToken();
        if (bracket && bracket->type == TokenType::BRACKET)
        {
            std::vector<std::unique_ptr<Expression>> arguments = ParseArguments();
            return std::make_unique<FunctionCallExpr>(std::move(base), std::move(arguments));
        }
        ReportSyntaxError(bracket, "Expected bracket '('");
        std::exit(EXIT_FAILURE);
        return nullptr;
    }

    std::unique_ptr<FunctionDeclStmt> Parser::ParseFunctionDeclaration()
    {
        std::vector<Token> modifiers = this->modifiers;
        Token* ret = Consume(TokenType::KEYWORD);
        Token* identifier = Consume(TokenType::IDENTIFIER);

        std::vector<std::unique_ptr<VarDeclExpr>> parameters = ParseParameters();

        // Парсим тело функции
        std::unique_ptr<Statement> body = ParseStatement();

        auto stmt = std::make_unique<FunctionDeclStmt>(
            std::make_unique<Token>(*ret),   // Создаём `unique_ptr` на копию токена
            std::make_unique<Token>(*identifier),
            std::move(parameters),
            std::move(body)
        );
        stmt->modifiers = modifiers;
        return stmt;
    }

    std::unique_ptr<ReturnStmt> Parser::ParseReturnStmt()
    {
	    Token* token = CurrentToken();
	    if (token && token->type == TokenType::KEYWORD && token->value == "return") {
		    Consume(TokenType::KEYWORD);
		    std::unique_ptr<Expression> expr = ParseExpression();
		    Consume(TokenType::DELIMITER);
		    return std::make_unique<ReturnStmt>(std::move(expr));
        }
        return nullptr;
    }
}