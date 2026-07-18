#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<ArrayExpr> Parser::ParseArrayValues() {
        std::vector<std::unique_ptr<Expression>> values;

        Consume(TokenType::BRACKET, "{");  // `{`

        while (RequireCurrent("an array value or '}'")->value != "}") {
            if (CurrentToken()->value == "{") {
                values.push_back(ParseArrayValues());  // Рекурсивный вызов для вложенного массива
            }
            else {
                values.push_back(ParseExpression());  // Парсим обычное значение
            }

            // Проверяем `,` между значениями
            if (RequireCurrent("',' or '}'")->value == ",") {
                Consume(TokenType::DELIMITER);  // `,`
            }
            else if (CurrentToken()->value == "}") {
                break;
            }
            else {
                ReportSyntaxError(CurrentToken(), "Expected '.' or '}'.");
                std::exit(EXIT_FAILURE);
                break;
            }
        }

        Consume(TokenType::BRACKET, "}");  // `}`
        return std::make_unique<ArrayExpr>(std::vector<std::unique_ptr<Expression>>(), std::move(values));
    }

    std::unique_ptr<Expression> Parser::ParseArrayAccess(std::unique_ptr<Expression> base) {
        if (!CurrentToken() || CurrentToken()->value != "[") {
            return base; // Нет скобок — возвращаем обычное выражение
        }

        std::vector<std::unique_ptr<Expression>> indexes;

        while (CurrentToken() && CurrentToken()->value == "[") {
            Consume(TokenType::BRACKET, "[");
            std::unique_ptr<Expression> index;
            if (RequireCurrent("an array index, ':' or ']'")->value != "]" &&
                CurrentToken()->value != ":") {
                index = ParseExpression();
            }
            if (CurrentToken() && CurrentToken()->type == TokenType::OPERATOR &&
                CurrentToken()->value == ":") {
                if (!indexes.empty()) {
                    ReportSyntaxError(CurrentToken(), "Slices are supported only for one-dimensional arrays");
                    throw std::runtime_error("Invalid multidimensional slice");
                }
                Consume(TokenType::OPERATOR, ":");
                std::unique_ptr<Expression> end;
                if (RequireCurrent("a slice end or ']'")->value != "]") end = ParseExpression();
                Consume(TokenType::BRACKET, "]");
                auto slice = std::make_unique<SliceExpr>(
                    std::move(base), std::move(index), std::move(end));
                if (CurrentToken() && CurrentToken()->value == "[")
                    return ParseArrayAccess(std::move(slice));
                return slice;
            }
            if (index) {
                indexes.push_back(std::move(index));
            }
            else {
                indexes.push_back(nullptr);
            }
            Consume(TokenType::BRACKET, "]");
        }

        return std::make_unique<ArrayAccessExpr>(std::move(base), std::move(indexes));
    }
}
