#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<TypeAliasStmt> Parser::ParseTypeAlias() {
        Consume(TokenType::KEYWORD, "using");
        Token* name = Consume(TokenType::IDENTIFIER);
        Consume(TokenType::OPERATOR, "=");
        std::unique_ptr<TypeExpr> target = ParseType();
        Consume(TokenType::DELIMITER, ";");
        auto statement = std::make_unique<TypeAliasStmt>(name->value, std::move(target));
        statement->attributes = attributes;
        statement->modifiers = modifiers;
        return statement;
    }
}
