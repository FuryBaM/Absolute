#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<ConstructorCallExpr> Parser::ParseConstructorCall()
    {
        Consume(TokenType::KEYWORD, "new");
        bool raw = false;
        if (CurrentToken() && CurrentToken()->type == TokenType::KEYWORD && CurrentToken()->value == "raw") {
            Consume(TokenType::KEYWORD, "raw");
            raw = true;
        }

        std::unique_ptr<Expression> type;
        if (CurrentToken() && CurrentToken()->type == TokenType::KEYWORD && IsPrimitiveType(CurrentToken()->value))
            type = ParsePrimitiveType();
        else {
            Token* identifier = Consume(TokenType::IDENTIFIER);
            type = std::make_unique<IdentifierExpr>(identifier->value);
            while (CurrentToken()) {
                if (CurrentToken()->value == "<") {
                    size_t close = 0;
                    if (!IsTemplateArgumentList(pos, &close)) break;
                    type = std::make_unique<UserTypeExpr>(std::move(type));
                    type = ParseTemplateExpr(std::move(type));
                }
                else if (CurrentToken()->type == TokenType::DELIMITER &&
                    CurrentToken()->value == ".") {
                    type = ParseMemberAccess(std::move(type));
                }
                else break;
            }
        }

        // `new Cell*[n]`: the star belongs to the element type. Without this the
        // star was parsed as multiplication and the `[` after it as the start of
        // an expression, so an array of pointers could only be written as a
        // literal with a fixed element count -- while the type itself was
        // otherwise usable everywhere.
        //
        // Consumed only when an array suffix actually follows, so no expression
        // that could be a multiplication changes meaning. A leading `raw`
        // qualifies the element pointer here rather than the allocation, which
        // is what `raw Cell*[]` means as a declared type.
        size_t stars = 0;
        while (PeekToken(stars) && PeekToken(stars)->type == TokenType::OPERATOR &&
            PeekToken(stars)->value == "*") ++stars;
        if (stars > 0 && PeekToken(stars) &&
            PeekToken(stars)->type == TokenType::BRACKET &&
            PeekToken(stars)->value == "[") {
            for (size_t star = 0; star < stars; ++star) {
                Consume(TokenType::OPERATOR, "*");
                auto pointee = std::unique_ptr<TypeExpr>(
                    static_cast<TypeExpr*>(type.release()));
                type = std::make_unique<PointerTypeExpr>(
                    std::move(pointee), raw, false, false);
                raw = false;
            }
        }

        std::vector<std::unique_ptr<Expression>> arguments;
        if (CurrentToken() && CurrentToken()->type == TokenType::BRACKET && CurrentToken()->value == "[") {
            Consume(TokenType::BRACKET, "[");
            if (CurrentToken() && CurrentToken()->value != "]") {
                arguments.push_back(ParseExpression());
            }
            Consume(TokenType::BRACKET, "]");
            auto typeExpr = std::unique_ptr<TypeExpr>(static_cast<TypeExpr*>(type.release()));
            type = std::make_unique<ArrayTypeExpr>(std::move(typeExpr));
        }
        else if (CurrentToken() && CurrentToken()->type == TokenType::BRACKET && CurrentToken()->value == "(")
            arguments = ParseArguments();
        return std::make_unique<ConstructorCallExpr>(std::move(type), std::move(arguments), raw);
    }

    std::unique_ptr<ConstructorDeclStmt> Parser::ParseConstructor()
    {
        std::vector<Token> modifiers = this->modifiers;
        std::vector<Attribute> attributes = this->attributes;
        Token* name = Consume(TokenType::IDENTIFIER);

        std::vector<std::unique_ptr<VarDeclExpr>> parameters = ParseParameters();

        if (CurrentToken() && CurrentToken()->type == TokenType::KEYWORD &&
            CurrentToken()->value == "const") {
            modifiers.push_back(*Consume(TokenType::KEYWORD, "const"));
        }

        std::unique_ptr<Statement> body = ParseStatement();
        auto stmt = std::make_unique<ConstructorDeclStmt>(std::make_unique<Token>(*name), std::move(parameters), std::move(body));
        if (auto* compound = dynamic_cast<CompoundStmt*>(stmt->body.get());
            compound && !compound->statements.empty()) {
            auto* first = dynamic_cast<SingleStatement*>(compound->statements.front().get());
            auto* call = first ? dynamic_cast<FunctionCallExpr*>(first->expr.get()) : nullptr;
            auto* identifier = call ? dynamic_cast<IdentifierExpr*>(call->base.get()) : nullptr;
            if (identifier && identifier->name == "base") {
                stmt->hasExplicitBaseCall = true;
                stmt->baseArguments = std::move(call->arguments);
                compound->statements.erase(compound->statements.begin());
            }
        }
        stmt->modifiers = modifiers;
        stmt->attributes = std::move(attributes);
        return stmt;
    }
}
