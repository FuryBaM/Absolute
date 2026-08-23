#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<StructDeclStmt> Parser::ParseStructDecl()
    {
        std::vector<Token> declarationModifiers = modifiers;
        std::vector<Attribute> declarationAttributes = attributes;
        Consume(TokenType::KEYWORD, "struct");
        Token* identifier = Consume(TokenType::IDENTIFIER);
        std::vector<std::string> templateConstraints;
        std::vector<Token> templateParams =
            ParseTemplateParameters(&templateConstraints);

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
            identifier->value, std::move(templateParams), std::move(body->statements));
        statement->templateConstraints = std::move(templateConstraints);
        statement->modifiers = std::move(declarationModifiers);
        statement->attributes = std::move(declarationAttributes);
        return statement;
    }
}
