#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<Expression> Parser::ParseMemberAccess(std::unique_ptr<Expression> base) {
        Consume(TokenType::DELIMITER, "."); // Потребляем `.`

        Token* memberToken = CurrentToken();
        if (!memberToken || memberToken->type != TokenType::IDENTIFIER) {
            ReportSyntaxError(memberToken, "Expected identifier after '.'");
            std::exit(EXIT_FAILURE);
            return nullptr;
        }

        std::string memberName = memberToken->value;
        Consume(TokenType::IDENTIFIER); // Потребляем имя члена

        return std::make_unique<MemberAccessExpr>(std::move(base), memberName);
    }
}