#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<StructDeclStmt> Parser::ParseStructDecl()
    {
        std::vector<Token> declarationModifiers = modifiers;
        Consume(TokenType::KEYWORD, "struct");
        Token* identifier = Consume(TokenType::IDENTIFIER);

        EnterScope(ScopeType::Struct, identifier->value);
        std::unique_ptr<CompoundStmt> body;
        try {
            body = ParseCompoundStatement();
        }
        catch (...) {
            ExitScope();
            throw;
        }
        ExitScope();

        auto statement = std::make_unique<StructDeclStmt>(
            identifier->value, std::move(body->statements));
        statement->modifiers = std::move(declarationModifiers);
        return statement;
    }
}
