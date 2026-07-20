#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<EnumDeclStmt> Parser::ParseEnumDecl() {
        std::vector<Token> modifiers = this->modifiers;
        std::vector<Attribute> attributes = this->attributes;
        std::vector<std::string> members;

        Consume(TokenType::KEYWORD, "enum");
        Token* name = Consume(TokenType::IDENTIFIER);
        Consume(TokenType::BRACKET, "{"); // Открывающая `{`

        bool expectComma = false; // Флаг, ожидаем ли запятую перед следующим элементом

        while (CurrentToken()->value != "}") {
            if (expectComma) {
                if (CurrentToken()->value != ",") {
                    ReportSyntaxError(CurrentToken(), "Expected ',' between enum members, but found '" + CurrentToken()->value + "'");
                    std::exit(EXIT_FAILURE);
                }
                Consume(TokenType::DELIMITER, ",");
            }

            Token* member = Consume(TokenType::IDENTIFIER);
            members.push_back(member->value);

            expectComma = true; // После идентификатора теперь ожидаем запятую
        }

        Consume(TokenType::BRACKET, "}"); // Закрывающая `}`

        auto stmt = std::make_unique<EnumDeclStmt>(name->value, members);
        stmt->modifiers = modifiers;
        stmt->attributes = std::move(attributes);
        return stmt;
    }

    std::unique_ptr<GroupDeclStmt> Parser::ParseGroupDecl() {
        std::vector<Token> modifiers = this->modifiers;
        std::vector<Attribute> attributes = this->attributes;
        std::vector<std::unique_ptr<EnumDeclStmt>> enums;
        std::vector<std::unique_ptr<GroupDeclStmt>> groups;

        Consume(TokenType::KEYWORD, "group");
        Token* name = Consume(TokenType::IDENTIFIER);
        Consume(TokenType::BRACKET, "{"); // Открывающая `{`

        while (CurrentToken()->value != "}") {
            Token* next = CurrentToken();

            if (next->type == TokenType::KEYWORD) {
                if (next->value == "enum") {
                    enums.push_back(ParseEnumDecl());
                }
                else if (next->value == "group") {
                    groups.push_back(ParseGroupDecl());
                }
                else {
                    ReportSyntaxError(next, "Unexpected keyword '" + next->value + "' in group declaration");
                    std::exit(EXIT_FAILURE);
                }
            }
            else {
                ReportSyntaxError(next, "Expected 'enum' or 'group', but found '" + next->value + "'");
                std::exit(EXIT_FAILURE);
            }

        }

        Consume(TokenType::BRACKET, "}"); // Закрывающая `}`

        auto stmt = std::make_unique<GroupDeclStmt>(name->value, std::move(enums), std::move(groups));
        stmt->modifiers = modifiers;
        stmt->attributes = std::move(attributes);
        return stmt;
    }
}
