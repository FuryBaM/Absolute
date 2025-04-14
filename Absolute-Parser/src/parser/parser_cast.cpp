#include "parser_pch.h"
#include "parser.h"

namespace Absolute{
    std::unique_ptr<CastExpr> Parser::ParseCastExpr(std::unique_ptr<Expression> base)
    {
        Consume(TokenType::KEYWORD, "as");
        std::unique_ptr<TypeExpr> type = ParseType();
        return std::make_unique<CastExpr>(std::move(type), std::move(base));
    }
}