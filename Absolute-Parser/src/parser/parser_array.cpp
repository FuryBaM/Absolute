#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<ArrayExpr> Parser::ParseArrayValues() {
        std::vector<std::unique_ptr<Expression>> values;

        Consume(TokenType::BRACKET, "{");  // `{`

        while (CurrentToken()->value != "}") {
            if (CurrentToken()->value == "{") {
                values.push_back(ParseArrayValues());  // Рекурсивный вызов для вложенного массива
            }
            else {
                values.push_back(ParseExpression());  // Парсим обычное значение
            }

            // Проверяем `,` между значениями
            if (CurrentToken()->value == ",") {
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
        if (CurrentToken()->value != "[") {
            return base; // Нет скобок — возвращаем обычное выражение
        }

        std::vector<std::unique_ptr<Expression>> indexes;

        while (CurrentToken()->value == "[") {
            Consume(TokenType::BRACKET, "[");
            if (CurrentToken()->value != "]") {
                indexes.push_back(ParseExpression());
            }
            else {
                indexes.push_back(nullptr);
            }
            Consume(TokenType::BRACKET, "]");
        }

        return std::make_unique<ArrayAccessExpr>(std::move(base), std::move(indexes));
    }
}