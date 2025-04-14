#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<ClassDeclStmt> Parser::ParseClassDecl()
    {
        std::vector<Token> modifiers = this->modifiers;
        std::vector<std::string> parents;

        Consume(TokenType::KEYWORD, "class"); // class
        Token* identifier = Consume(TokenType::IDENTIFIER); // имя класса

        std::vector<Token> templateParams;
        if (CurrentToken()->type == TokenType::DOLLAR) {
            Consume(TokenType::DOLLAR);
            Consume(TokenType::OPERATOR, "<");
            while (CurrentToken()->value != ">") {
                if (CurrentToken()->type == TokenType::IDENTIFIER) {
                    templateParams.push_back(*Consume(CurrentToken()->type));
                }
                else {
                    ReportSyntaxError(CurrentToken(), "Expected type name in template expression");
                    std::exit(EXIT_FAILURE);
                }

                if (CurrentToken()->value == ",") {
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
        if (CurrentToken()->value == ":") {
            Consume(TokenType::OPERATOR, ":"); // :

            while (true) {
                Token* parent = Consume(TokenType::IDENTIFIER);
                parents.push_back(parent->value);

                Token* nextToken = CurrentToken();
                if (nextToken->value == ",") {
                    Consume(TokenType::DELIMITER, ",");
                }
                else if (nextToken->value == "{") {
                    break; // Всё ок, начинаем тело класса
                }
                else {
                    ReportSyntaxError(CurrentToken(), "Expected ',' or '{' after parent class, but found '" + nextToken->value + "'");
                    std::exit(EXIT_FAILURE);
                }
            }
        }
        scopeStack.push_back(ScopeType::Class);
        EnterScope(ScopeType::Class, identifier->value);
        std::unique_ptr<Statement> body = ParseCompoundStatement();
        ExitScope();
        auto stmt = std::make_unique<ClassDeclStmt>(identifier->value, std::move(parents), templateParams, std::move(body));
        stmt->modifiers = modifiers;
        return stmt;
    }
}