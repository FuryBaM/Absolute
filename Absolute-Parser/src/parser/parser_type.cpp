#include "parser_pch.h"
#include "parser.h"

namespace Absolute{
    std::unique_ptr<TypeExpr> Parser::ParseType()
    {
        OwnershipKind ownership = OwnershipKind::Unique;
        if (CurrentToken() && CurrentToken()->type == TokenType::KEYWORD &&
            (CurrentToken()->value == "raw" || CurrentToken()->value == "weak")) {
            ownership = CurrentToken()->value == "raw" ? OwnershipKind::Raw
                                                       : OwnershipKind::Weak;
            Consume(TokenType::KEYWORD, CurrentToken()->value);
        }
        // `sub` is contextual rather than a keyword: it is already an ordinary
        // identifier in existing programs -- a local named `sub` in the
        // standard library, and a test that asserts in so many words that it is
        // one. It qualifies a type only where a type is expected and something
        // that can start a type follows it, which is the only position where it
        // could mean anything.
        else if (CurrentToken() && CurrentToken()->type == TokenType::IDENTIFIER &&
            CurrentToken()->value == "sub" && PeekToken() &&
            (PeekToken()->type == TokenType::IDENTIFIER ||
                (PeekToken()->type == TokenType::KEYWORD &&
                    IsPrimitiveType(PeekToken()->value)))) {
            ownership = OwnershipKind::Sub;
            Consume(TokenType::IDENTIFIER);
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
        base = ParsePointerSuffix(std::move(base), ownership);
        while (CurrentToken() && CurrentToken()->type == TokenType::BRACKET &&
            CurrentToken()->value == "[" && PeekToken() &&
            PeekToken()->type == TokenType::BRACKET && PeekToken()->value == "]") {
            Consume(TokenType::BRACKET, "[");
            Consume(TokenType::BRACKET, "]");
            base = std::make_unique<ArrayTypeExpr>(std::move(base));
        }
        return base;
    }

    std::unique_ptr<TypeExpr> Parser::ParsePointerSuffix(
        std::unique_ptr<TypeExpr> base, OwnershipKind ownership)
    {
        bool foundPointer = false;
        while (CurrentToken() && CurrentToken()->type == TokenType::OPERATOR && CurrentToken()->value == "*") {
            Consume(TokenType::OPERATOR, "*");
            base = std::make_unique<PointerTypeExpr>(std::move(base), ownership);
            // Only the innermost star carries the qualifier; a pointer to a
            // qualified pointer is an ordinary owner of it.
            ownership = OwnershipKind::Unique;
            foundPointer = true;
        }
        if (ownership != OwnershipKind::Unique && !foundPointer) {
            // `sub T` with no star: the name may be a generic parameter, and
            // whether there is a pointer there at all is not something this
            // file can know. The node carries the qualifier and the analyzer
            // decides -- it is the one that knows which names stand for
            // themselves. A qualifier on anything else is still refused, just
            // with a message that can say what the name turned out to be.
            if (dynamic_cast<UserTypeExpr*>(base.get()))
                return std::make_unique<PointerTypeExpr>(
                    std::move(base), ownership, true);
            std::string written(CanonicalOwnershipPrefix(ownership));
            if (!written.empty()) written.pop_back();
            ReportSyntaxError(CurrentToken(),
                "'" + written + "' must qualify a pointer type");
            throw std::runtime_error("Invalid pointer qualifier");
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
        throw std::runtime_error("Expected primitive type");
        return nullptr;
    }
}
