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

    static std::string UnescapeString(const std::string& value) {
        std::string result;
        result.reserve(value.size());
        for (size_t index = 0; index < value.size(); ++index) {
            const char current = value[index];
            if (current != '\\') {
                result.push_back(current);
                continue;
            }
            if (++index >= value.size())
                throw std::runtime_error("Incomplete escape sequence in literal");
            switch (value[index]) {
            case '0': result.push_back('\0'); break;
            case 'a': result.push_back('\a'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'v': result.push_back('\v'); break;
            case '\\': result.push_back('\\'); break;
            case '"': result.push_back('"'); break;
            case '\'': result.push_back('\''); break;
            default:
                throw std::runtime_error(
                    "Unsupported escape sequence: \\" + std::string(1, value[index]));
            }
        }
        return result;
    }

    std::unique_ptr<StringLiteralExpr> Parser::ParseStringLiteralExpr() {
        Token* token = CurrentToken();
        if (token && token->type == TokenType::STRING) {
            std::string value = token->value.substr(1, token->value.size() - 2); // Убираем кавычки
            value = UnescapeString(value);
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
            const std::string value = UnescapeString(
                token->value.substr(1, token->value.size() - 2));
            if (value.size() != 1) {
                ReportSyntaxError(token, "A char literal must contain exactly one character");
                throw std::runtime_error("Invalid char literal");
            }
            Consume(TokenType::CHAR);
            return std::make_unique<CharLiteralExpr>(value.front());
        }
        ReportSyntaxError(token, "Expected char literal");
        std::exit(EXIT_FAILURE);
        return nullptr;
    }
}
