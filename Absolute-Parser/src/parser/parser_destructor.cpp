#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<DestructorCallExpr> Parser::ParseDestructorCall()
    {
        Consume(TokenType::KEYWORD, "delete");
        std::unique_ptr<Expression> target = ParsePrimaryExpr();
        return std::make_unique<DestructorCallExpr>(std::move(target));
    }
}