#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<Expression> Parser::ParseTernaryExpr(std::unique_ptr<Expression> condition) {
        Consume(TokenType::OPERATOR, "?"); // Пропускаем "?"

        auto trueExpr = ParseExpression();
        if (!trueExpr) return nullptr;

        if (!Consume(TokenType::OPERATOR, ":")) {
            ReportSyntaxError(CurrentToken(), "Expected ':' in ternary expression.");
            return nullptr;
        }

        auto falseExpr = ParseExpression();
        if (!falseExpr) return nullptr;

        return std::make_unique<TernaryExpr>(std::move(condition), std::move(trueExpr), std::move(falseExpr));
    }
}