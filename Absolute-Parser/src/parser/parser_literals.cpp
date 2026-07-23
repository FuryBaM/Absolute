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
            case '$': result.push_back('$'); break;
            default:
                throw std::runtime_error(
                    "Unsupported escape sequence: \\" + std::string(1, value[index]));
            }
        }
        return result;
    }

    std::unique_ptr<Expression> Parser::ParseStringLiteralExpr() {
        Token* token = CurrentToken();
        if (token && token->type == TokenType::STRING) {
            std::string rawContent = token->value.substr(1, token->value.size() - 2); // Unquote

            bool hasInterpolation = false;
            for (size_t i = 0; i < rawContent.size(); ++i) {
                if (rawContent[i] == '\\' && i + 1 < rawContent.size()) {
                    i++;
                    continue;
                }
                if (rawContent[i] == '$' && i + 1 < rawContent.size() && rawContent[i + 1] == '{') {
                    hasInterpolation = true;
                    break;
                }
            }

            if (!hasInterpolation) {
                std::string value = UnescapeString(rawContent);
                Consume(TokenType::STRING);
                return std::make_unique<StringLiteralExpr>(value);
            }

            // String interpolation desugaring to format(...)
            std::string formatTemplate;
            std::vector<std::string> exprStrings;
            std::string currentSegment;

            size_t i = 0;
            while (i < rawContent.size()) {
                if (rawContent[i] == '\\' && i + 1 < rawContent.size()) {
                    if (rawContent[i + 1] == '$') {
                        currentSegment += '$';
                        i += 2;
                        continue;
                    }
                    currentSegment += '\\';
                    currentSegment += rawContent[i + 1];
                    i += 2;
                    continue;
                }
                if (rawContent[i] == '$' && i + 1 < rawContent.size() && rawContent[i + 1] == '{') {
                    formatTemplate += UnescapeString(currentSegment);
                    currentSegment.clear();
                    formatTemplate += "{}";

                    i += 2; // Skip ${
                    size_t startExpr = i;
                    int braceDepth = 1;
                    while (i < rawContent.size() && braceDepth > 0) {
                        if (rawContent[i] == '{') {
                            braceDepth++;
                        } else if (rawContent[i] == '}') {
                            braceDepth--;
                            if (braceDepth == 0) break;
                        }
                        i++;
                    }
                    if (braceDepth != 0) {
                        ReportSyntaxError(token, "Unmatched '{' in string interpolation");
                        std::exit(EXIT_FAILURE);
                    }
                    std::string exprCode = rawContent.substr(startExpr, i - startExpr);
                    exprStrings.push_back(exprCode);
                    i++; // Skip closing }
                    continue;
                }
                currentSegment += rawContent[i];
                i++;
            }
            if (!currentSegment.empty()) {
                formatTemplate += UnescapeString(currentSegment);
            }

            Consume(TokenType::STRING);

            std::vector<std::unique_ptr<Expression>> formatCallArgs;
            formatCallArgs.push_back(std::make_unique<StringLiteralExpr>(formatTemplate));

            for (const std::string& exprCode : exprStrings) {
                std::vector<Token> exprTokens = lexer(exprCode);
                if (exprTokens.empty()) {
                    ReportSyntaxError(token, "Empty expression in string interpolation");
                    std::exit(EXIT_FAILURE);
                }
                Parser exprParser(std::move(exprTokens));
                std::unique_ptr<Expression> parsedExpr = exprParser.ParseExpression();
                if (!parsedExpr) {
                    ReportSyntaxError(token, "Failed to parse expression in string interpolation");
                    std::exit(EXIT_FAILURE);
                }
                formatCallArgs.push_back(std::move(parsedExpr));
            }

            auto formatCallee = std::make_unique<IdentifierExpr>("format");
            return std::make_unique<FunctionCallExpr>(std::move(formatCallee), std::move(formatCallArgs));
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
