#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    void Parser::ReportSyntaxError(const Token* token, const std::string& message)
    {
        std::cerr << "Syntax Error: " << message;

        if (token)
        {
            std::cerr << " at line " << token->line
                << ", column " << token->column
                << " (token: '" << token->value << "')";
        }

        std::cerr << std::endl;
    }

    Token* Parser::Consume(TokenType tokenType)
    {
        Token* token = CurrentToken();
        if (token && token->type == tokenType) {
            pos++;
        }
        else {
            std::string errorMsg = "Unexpected token: " + (token ? token->value : "EOF");
            errorMsg += ", expected: '" + TokenTypeToString(tokenType) + "'";
            ReportSyntaxError(token, errorMsg);
            std::exit(EXIT_FAILURE);
        }
        return token;
    }

    Token* Parser::Consume(TokenType tokenType, const std::string& expectedValue) {
        Token* token = CurrentToken();

        // Проверяем, соответствует ли expectedValue типу токена
        if (!IsValidTokenValue(tokenType, expectedValue)) {
            throw std::invalid_argument("Invalid expected value '" + expectedValue +
                "' for token type " + std::to_string(static_cast<int>(tokenType)));
        }

        if (token && token->type == tokenType && token->value == expectedValue) {
            pos++;
        }
        else {
            std::string errorMsg = "Unexpected token: " + (token ? token->value : "EOF");
            errorMsg += ", expected: '" + expectedValue + "'";
            ReportSyntaxError(token, errorMsg);
            std::exit(EXIT_FAILURE);
        }
        return token;
    }

    void Parser::ConsumeTemplateClose()
    {
        if (CurrentToken()->value == ">>") {
            tokens[pos].value = ">";  // Первый '>'
            Token twin(TokenType::OPERATOR, ">", CurrentToken()->line, CurrentToken()->column + 1);
            tokens.insert(tokens.begin() + pos + 1, twin);  // Второй '>'
        }

        Consume(TokenType::OPERATOR, ">");
    }
}