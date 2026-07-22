#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::string Parser::ParseParentTypeName()
    {
        const size_t begin = pos;
        std::unique_ptr<TypeExpr> type = ParseType();
        if (!type) {
            ReportSyntaxError(CurrentToken(), "Expected a parent type");
            throw std::runtime_error("Invalid parent type");
        }
        if (dynamic_cast<PointerTypeExpr*>(type.get()) || dynamic_cast<ArrayTypeExpr*>(type.get())) {
            ReportSyntaxError(CurrentToken(), "A parent must be a named type");
            throw std::runtime_error("Invalid parent type");
        }
        std::string result;
        for (size_t index = begin; index < pos; ++index) result += tokens[index].value;
        return result;
    }

    bool Parser::IsTemplateArgumentList(size_t start, size_t* close) const
    {
        if (start >= tokens.size() || tokens[start].value != "<") return false;

        size_t depth = 1;
        bool expectType = true;

        for (size_t index = start + 1; index < tokens.size(); ++index) {
            const Token& token = tokens[index];

            if (token.value == "<") {
                if (expectType) return false;
                ++depth;
                expectType = true;
                continue;
            }

            if (token.value == ">" || token.value == ">>") {
                if (expectType) return false;

                const size_t closes = token.value == ">>" ? 2 : 1;
                if (closes >= depth) {
                    if (close) *close = index;
                    return true;
                }
                depth -= closes;
                continue;
            }

            if (token.value == ",") {
                if (expectType) return false;
                expectType = true;
                continue;
            }

            if (token.value == ".") {
                if (expectType) return false;
                expectType = true;
                continue;
            }

            const bool isTypeName = token.type == TokenType::IDENTIFIER ||
                (token.type == TokenType::KEYWORD && IsPrimitiveType(token.value));
            if (!isTypeName || !expectType) return false;
            expectType = false;
        }

        return false;
    }

    Token* Parser::RequireCurrent(const std::string& expectation)
    {
        Token* token = CurrentToken();
        if (!token) {
            ReportSyntaxError(nullptr, "Unexpected end of file; expected " + expectation);
            throw std::runtime_error("Unexpected end of file");
        }
        return token;
    }

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
            throw std::runtime_error(errorMsg);
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
            throw std::runtime_error(errorMsg);
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
