#include "parser_pch.h"
#include "parser.h"
#include "ast/array.h"

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
                throw std::runtime_error("Expected '.' or '}'.");
                break;
            }
        }

        Consume(TokenType::BRACKET, "}");  // `}`
        return std::make_unique<ArrayExpr>(std::vector<std::unique_ptr<Expression>>(), std::move(values));
    }

    std::unique_ptr<Expression> Parser::ParseArrayAccess(std::unique_ptr<Expression> base) {
        if (!CurrentToken() || CurrentToken()->value != "[") {
            return base;
        }

        while (CurrentToken() && CurrentToken()->value == "[") {
            Consume(TokenType::BRACKET, "[");
            std::vector<SliceRange> sliceRanges;
            bool containsSlice = false;

            while (true) {
                std::unique_ptr<Expression> firstExpr;
                if (RequireCurrent("an array index, ':' or ']'")->value != "]" &&
                    CurrentToken()->value != "," && CurrentToken()->value != ":") {
                    firstExpr = ParseExpression();
                }

                if (CurrentToken() && CurrentToken()->type == TokenType::OPERATOR &&
                    CurrentToken()->value == ":") {
                    containsSlice = true;
                    Consume(TokenType::OPERATOR, ":");
                    std::unique_ptr<Expression> endExpr;
                    if (RequireCurrent("a slice end, ',' or ']'")->value != "]" &&
                        CurrentToken()->value != ",") {
                        endExpr = ParseExpression();
                    }
                    sliceRanges.emplace_back(std::move(firstExpr), std::move(endExpr), true);
                } else {
                    sliceRanges.emplace_back(std::move(firstExpr), nullptr, false);
                }

                if (CurrentToken() && CurrentToken()->value == ",") {
                    Consume(TokenType::DELIMITER, ",");
                } else {
                    break;
                }
            }

            Consume(TokenType::BRACKET, "]");

            if (containsSlice) {
                base = std::make_unique<SliceExpr>(std::move(base), std::move(sliceRanges));
            } else {
                std::vector<std::unique_ptr<Expression>> indexes;
                indexes.reserve(sliceRanges.size());
                for (auto& range : sliceRanges) {
                    indexes.push_back(std::move(range.begin));
                }
                base = std::make_unique<ArrayAccessExpr>(std::move(base), std::move(indexes));
            }
        }

        return base;
    }
}
