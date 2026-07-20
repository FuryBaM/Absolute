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

        std::vector<Token> templateParams;
        if (CurrentToken() && CurrentToken()->value == "<") {
            Consume(TokenType::OPERATOR, "<");
            while (RequireCurrent("a template parameter or '>'")->value != ">") {
                if (CurrentToken()->type == TokenType::IDENTIFIER) {
                    templateParams.push_back(*Consume(TokenType::IDENTIFIER));
                }
                else {
                    ReportSyntaxError(CurrentToken(), "Expected type name in template expression");
                    std::exit(EXIT_FAILURE);
                }

                if (RequireCurrent("',' or '>'")->value == ",") {
                    Consume(TokenType::DELIMITER, ",");
                }
                else if (CurrentToken()->value != ">") {
                    ReportSyntaxError(CurrentToken(), "Expected ',' or '>' in template expression");
                    std::exit(EXIT_FAILURE);
                }
            }
            Consume(TokenType::OPERATOR, ">");
        }

        // Проверяем, есть ли наследование или реализация интерфейсов
        if (CurrentToken() && CurrentToken()->value == ":") {
            Consume(TokenType::OPERATOR, ":"); // :

            while (true) {
                Token* parent = Consume(TokenType::IDENTIFIER);
                parents.push_back(parent->value);

                Token* nextToken = CurrentToken();
                if (nextToken && nextToken->value == ",") {
                    Consume(TokenType::DELIMITER, ",");
                }
                else if (nextToken && nextToken->value == "{") {
                    break; // Всё ок, начинаем тело класса
                }
                else {
                    ReportSyntaxError(CurrentToken(), "Expected ',' or '{' after parent class");
                    std::exit(EXIT_FAILURE);
                }
            }
        }
        EnterScope(ScopeType::Class, identifier->value);
        std::unique_ptr<Statement> body = ParseCompoundStatement();
        ExitScope();
        auto stmt = std::make_unique<ClassDeclStmt>(identifier->value, std::move(parents), templateParams, std::move(body));
        stmt->modifiers = modifiers;
        stmt->attributes = std::move(attributes);
        return stmt;
    }
}
