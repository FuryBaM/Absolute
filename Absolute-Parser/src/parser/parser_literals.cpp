#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<Expression> Parser::ParseLiteralExpr() {
        Token* token = CurrentToken();
        // Число (42, 3.14)
        if (token->type == TokenType::NUMBER) {
            return ParseNumberLiteralExpr();
        }
        // **Строка** ("Hello world")
        if (token->type == TokenType::STRING) {
            return ParseStringLiteralExpr();
        }
        // **Символ** ('A')
        if (token->type == TokenType::CHAR) {
            return ParseCharLiteralExpr();
        }
        ReportSyntaxError(token, "Expected literal");
        std::exit(EXIT_FAILURE);
        return nullptr;
    }

    std::unique_ptr<BooleanLiteralExpr> Parser::ParseBooleanLiteralExpr() {
        Token* booleanToken = CurrentToken();
        if (booleanToken->value == "true" || booleanToken->value == "false") {
            Consume(TokenType::KEYWORD);
            return std::make_unique<BooleanLiteralExpr>(booleanToken->value == "true");
        }
        ReportSyntaxError(booleanToken, "Expected number literal");
        std::exit(EXIT_FAILURE);
        return nullptr;
    }

    std::unique_ptr<NumberLiteralExpr> Parser::ParseNumberLiteralExpr() {
        Token* numberToken = CurrentToken();
        if (numberToken && numberToken->type == TokenType::NUMBER) {
            Consume(TokenType::NUMBER);
            return std::make_unique<NumberLiteralExpr>(numberToken->value);
        }
        ReportSyntaxError(numberToken, "Expected number literal");
        std::exit(EXIT_FAILURE);
        return nullptr;
    }

    std::unique_ptr<StringLiteralExpr> Parser::ParseStringLiteralExpr() {
        Token* token = CurrentToken();
        if (token && token->type == TokenType::STRING) {
            std::string value = token->value.substr(1, token->value.size() - 2); // Убираем кавычки
            Consume(TokenType::STRING);
            return std::make_unique<StringLiteralExpr>(value);
        }
        ReportSyntaxError(token, "Expected string literal");
        std::exit(EXIT_FAILURE);
        return nullptr;
    }

    std::unique_ptr<CharLiteralExpr> Parser::ParseCharLiteralExpr() {
        Token* token = CurrentToken();
        if (token && token->type == TokenType::CHAR) {
            Consume(TokenType::CHAR);
            return std::make_unique<CharLiteralExpr>(token->value[1]); // Символ внутри кавычек
        }
        ReportSyntaxError(token, "Expected char literal");
        std::exit(EXIT_FAILURE);
        return nullptr;
    }
}