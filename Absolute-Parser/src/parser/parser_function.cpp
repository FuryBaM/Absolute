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

        if (CurrentToken() && IsEndOfStatement(*CurrentToken())) {
            ReportSyntaxError(CurrentToken(), "A function body is required; use extern \"C\" for a native declaration");
            throw std::runtime_error("Function declaration without a body");
        }

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

    std::unique_ptr<FunctionDeclStmt> Parser::ParseExternalFunctionDeclaration()
    {
        std::vector<Token> modifiers = this->modifiers;
        Consume(TokenType::KEYWORD, "extern");
        Token* abiToken = Consume(TokenType::STRING);
        if (!abiToken || abiToken->value != "\"C\"") {
            ReportSyntaxError(abiToken, "Only extern \"C\" is supported");
            throw std::runtime_error("Unsupported external ABI");
        }

        Token* returnType = Consume(TokenType::KEYWORD);
        if (!returnType || !IsPrimitiveType(returnType->value) || returnType->value == "auto" ||
            returnType->value == "dynamic") {
            ReportSyntaxError(returnType, "An extern function requires a concrete primitive return type");
            throw std::runtime_error("Invalid extern return type");
        }
        Token* identifier = Consume(TokenType::IDENTIFIER);
        auto parameters = ParseParameters();
        for (const auto& parameter : parameters) {
            if (parameter && parameter->value) {
                ReportSyntaxError(identifier, "Extern function parameters cannot have default values");
                throw std::runtime_error("Invalid extern parameter");
            }
        }
        Consume(TokenType::DELIMITER, ";");

        auto statement = std::make_unique<FunctionDeclStmt>(
            std::make_unique<Token>(*returnType), std::make_unique<Token>(*identifier),
            std::move(parameters), nullptr);
        statement->externalAbi = "C";
        statement->modifiers = std::move(modifiers);
        return statement;
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
