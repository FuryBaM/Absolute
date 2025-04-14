#include "parser_pch.h"
#include "parser.h"

namespace Absolute{
    std::unique_ptr<TypeExpr> Parser::ParseType()
    {
        if (CurrentToken()->type == TokenType::IDENTIFIER) {
            return std::make_unique<UserTypeExpr>(ParseIdentifierExpr());
        }
        else if (CurrentToken()->type == TokenType::KEYWORD) {
            return ParsePrimitiveType();
        }
        return nullptr;
    }

    std::unique_ptr<PrimitiveTypeExpr> Parser::ParsePrimitiveType()
    {
        Token* type = CurrentToken();
        if (IsPrimitiveType(type->value)) {
            Consume(TokenType::KEYWORD);
            return std::make_unique<PrimitiveTypeExpr>(type->value);
        }
        ReportSyntaxError(type, "Expected primitive type");
        std::exit(EXIT_FAILURE);
        return nullptr;
    }
}