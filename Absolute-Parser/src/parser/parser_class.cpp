#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<ClassDeclStmt> Parser::ParseClassDecl()
    {
        std::vector<Token> modifiers = this->modifiers;
        std::vector<Attribute> attributes = this->attributes;
        std::vector<std::string> parents;

        Consume(TokenType::KEYWORD, "class"); // class
        Token* identifier = Consume(TokenType::IDENTIFIER); // имя класса

        std::vector<std::string> templateConstraints;
        std::vector<Token> templateParams =
            ParseTemplateParameters(&templateConstraints);

        // Проверяем, есть ли наследование или реализация интерфейсов
        if (CurrentToken() && CurrentToken()->value == ":") {
            Consume(TokenType::OPERATOR, ":"); // :

            while (true) {
                parents.push_back(ParseParentTypeName());

                Token* nextToken = CurrentToken();
                if (nextToken && nextToken->value == ",") {
                    Consume(TokenType::DELIMITER, ",");
                }
                else if (nextToken && nextToken->value == "{") {
                    break; // Всё ок, начинаем тело класса
                }
                else {
                    ReportSyntaxError(CurrentToken(), "Expected ',' or '{' after parent class");
                    throw std::runtime_error("Expected ',' or '{' after parent class");
                }
            }
        }
        EnterScope(ScopeType::Class, identifier->value);
        std::unique_ptr<Statement> body = ParseCompoundStatement();
        ExitScope();
        auto stmt = std::make_unique<ClassDeclStmt>(identifier->value, std::move(parents), templateParams, std::move(body));
        stmt->templateConstraints = std::move(templateConstraints);
        stmt->modifiers = modifiers;
        stmt->attributes = std::move(attributes);
        return stmt;
    }
}
