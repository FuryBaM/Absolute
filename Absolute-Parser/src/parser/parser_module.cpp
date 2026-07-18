#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::string Parser::ParseQualifiedName() {
        Token* first = CurrentToken();
        if (!first || first->type != TokenType::IDENTIFIER) {
            ReportSyntaxError(first, "Expected a namespace identifier");
            throw std::runtime_error("Invalid qualified name");
        }

        std::string result = Consume(TokenType::IDENTIFIER)->value;
        while (CurrentToken() && CurrentToken()->type == TokenType::DELIMITER && CurrentToken()->value == ".") {
            Consume(TokenType::DELIMITER, ".");
            Token* part = CurrentToken();
            if (!part || part->type != TokenType::IDENTIFIER) {
                ReportSyntaxError(part, "Expected an identifier after '.'");
                throw std::runtime_error("Invalid qualified name");
            }
            result += "." + Consume(TokenType::IDENTIFIER)->value;
        }
        return result;
    }

    std::unique_ptr<ImportStmt> Parser::ParseImport() {
        Consume(TokenType::KEYWORD, "import");
        Token* target = RequireCurrent("an import target");
        bool isFile = target->type == TokenType::STRING;
        std::string value;
        if (isFile) {
            value = target->value.substr(1, target->value.size() - 2);
            Consume(TokenType::STRING);
        }
        else value = ParseQualifiedName();
        Consume(TokenType::DELIMITER, ";");
        return std::make_unique<ImportStmt>(std::move(value), isFile);
    }

    std::unique_ptr<NamespaceDeclStmt> Parser::ParseNamespace() {
        Consume(TokenType::KEYWORD, "namespace");
        std::string name = ParseQualifiedName();
        return std::make_unique<NamespaceDeclStmt>(std::move(name), ParseCompoundStatement());
    }
}
