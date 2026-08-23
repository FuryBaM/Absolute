#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<EnumDeclStmt> Parser::ParseEnumDecl() {
        std::vector<Token> modifiers = this->modifiers;
        std::vector<Attribute> attributes = this->attributes;
        std::vector<EnumMemberDecl> members;

        Consume(TokenType::KEYWORD, "enum");
        Token* name = Consume(TokenType::IDENTIFIER);
        Consume(TokenType::BRACKET, "{"); // Открывающая `{`

        bool expectComma = false; // Флаг, ожидаем ли запятую перед следующим элементом

        while (CurrentToken()->value != "}") {
            if (expectComma) {
                if (CurrentToken()->value != ",") {
                    ReportSyntaxError(CurrentToken(), "Expected ',' between enum members, but found '" + CurrentToken()->value + "'");
                    throw std::runtime_error("Expected ',' between enum members, but found '");
                }
                Consume(TokenType::DELIMITER, ",");
            }

            Token* member = Consume(TokenType::IDENTIFIER);
            EnumMemberDecl declared;
            declared.name = member->value;
            declared.sourceFile = sourceFile;
            declared.line = member->line;
            declared.column = member->column;

            // `Name = 404`, with an optional sign. A number and nothing else:
            // an expression here would need constant folding, and the member a
            // program means to name is always written out.
            if (CurrentToken()->value == "=" && CurrentToken()->type == TokenType::OPERATOR) {
                Consume(TokenType::OPERATOR, "=");
                bool negative = false;
                if (CurrentToken()->value == "-" || CurrentToken()->value == "+") {
                    negative = CurrentToken()->value == "-";
                    Consume(TokenType::OPERATOR);
                }
                Token* number = CurrentToken();
                if (!number || number->type != TokenType::NUMBER ||
                    IsFloatingLiteral(number->value)) {
                    ReportSyntaxError(number, "Enum member '" + declared.name +
                        "' must be given an integer, but found '" +
                        (number ? number->value : std::string("end of file")) + "'");
                    throw std::runtime_error("Enum member must be given an integer");
                }
                Consume(TokenType::NUMBER);
                // The magnitude is checked against the enum's width by the
                // analyzer, where the message carries the file and the line;
                // only a value no 64-bit integer can hold is caught here.
                try {
                    const unsigned long long magnitude = std::stoull(number->value);
                    const unsigned long long limit = negative
                        ? 9223372036854775808ull : 9223372036854775807ull;
                    if (magnitude > limit) throw std::out_of_range(number->value);
                    // Through the unsigned value, so the minimum of the width
                    // -- whose magnitude a signed integer cannot hold -- is
                    // converted rather than overflowed.
                    declared.value = negative
                        ? static_cast<long long>(~magnitude + 1ull)
                        : static_cast<long long>(magnitude);
                }
                catch (const std::exception&) {
                    ReportSyntaxError(number, "Enum member '" + declared.name +
                        "' is given " + (negative ? "-" : "") + number->value +
                        ", which no integer can hold");
                    throw std::runtime_error("Enum member value is out of range");
                }
                declared.hasValue = true;
            }
            members.push_back(std::move(declared));

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
                    throw std::runtime_error("Unexpected keyword '");
                }
            }
            else {
                ReportSyntaxError(next, "Expected 'enum' or 'group', but found '" + next->value + "'");
                throw std::runtime_error("Expected 'enum' or 'group', but found '");
            }

        }

        Consume(TokenType::BRACKET, "}"); // Закрывающая `}`

        auto stmt = std::make_unique<GroupDeclStmt>(name->value, std::move(enums), std::move(groups));
        stmt->modifiers = modifiers;
        stmt->attributes = std::move(attributes);
        return stmt;
    }
}
