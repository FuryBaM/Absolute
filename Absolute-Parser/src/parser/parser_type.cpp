#include "parser_pch.h"
#include "parser.h"

namespace Absolute{
    std::unique_ptr<TypeExpr> Parser::ParseType()
    {
        bool raw = false;
        if (CurrentToken() && CurrentToken()->type == TokenType::KEYWORD && CurrentToken()->value == "raw") {
            Consume(TokenType::KEYWORD, "raw");
            raw = true;
        }
        Token* current = RequireCurrent("a type");
        std::unique_ptr<TypeExpr> base;
        if (current->type == TokenType::IDENTIFIER) {
            std::unique_ptr<Expression> name = std::make_unique<IdentifierExpr>(
                Consume(TokenType::IDENTIFIER)->value);
            while (CurrentToken()) {
                if (CurrentToken()->type == TokenType::DELIMITER && CurrentToken()->value == ".")
                    name = ParseMemberAccess(std::move(name));
                else if (CurrentToken()->type == TokenType::OPERATOR && CurrentToken()->value == "<")
                    name = ParseTemplateExpr(std::move(name));
                else break;
            }
            base = std::make_unique<UserTypeExpr>(std::move(name));
        }
        else if (current->type == TokenType::KEYWORD) {
            base = ParsePrimitiveType();
        }
        if (!base) return nullptr;
        base = ParsePointerSuffix(std::move(base), raw);
        while (CurrentToken() && CurrentToken()->type == TokenType::BRACKET &&
            CurrentToken()->value == "[" && PeekToken() &&
            PeekToken()->type == TokenType::BRACKET && PeekToken()->value == "]") {
            Consume(TokenType::BRACKET, "[");
            Consume(TokenType::BRACKET, "]");
            base = std::make_unique<ArrayTypeExpr>(std::move(base));
        }
        return base;
    }

    std::unique_ptr<TypeExpr> Parser::ParsePointerSuffix(std::unique_ptr<TypeExpr> base, bool raw)
    {
        bool foundPointer = false;
        while (CurrentToken() && CurrentToken()->type == TokenType::OPERATOR && CurrentToken()->value == "*") {
            Consume(TokenType::OPERATOR, "*");
            base = std::make_unique<PointerTypeExpr>(std::move(base), raw);
            raw = false;
            foundPointer = true;
        }
        if (raw && !foundPointer) {
            ReportSyntaxError(CurrentToken(), "'raw' must qualify a pointer type");
            throw std::runtime_error("Invalid raw type");
        }
        return base;
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
