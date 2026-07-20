#include "parser_pch.h"
#include "parser.h"

namespace Absolute{
    std::unique_ptr<CastExpr> Parser::ParseCastExpr(std::unique_ptr<Expression> base)
    {
        const std::string operation = CurrentToken() ? CurrentToken()->value : std::string{};
        Consume(TokenType::KEYWORD, operation);
        std::unique_ptr<TypeExpr> type = ParseType();
        return std::make_unique<CastExpr>(operation, std::move(type), std::move(base));
    }
}
